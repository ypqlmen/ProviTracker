import {workbookIdentity} from './identity.js';
let port = null;
let pending = null;
const loadedVersion = chrome.runtime.getManifest().version;
let reloadRequested = false;
let reloadTarget = null;
let status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
function reloadWhenIdle() {
  if (reloadTarget && !pending && !reloadRequested) {
    reloadRequested = true;
    chrome.runtime.reload();
  }
}
function poll() {
  if (port && !pending && !reloadRequested) port.postMessage({type:'poll', extensionVersion:loadedVersion});
}
function newerVersion(value) {
  if (typeof value !== 'string' || !/^\d+(\.\d+){0,3}$/.test(value)) return false;
  const a = value.split('.').map(Number), b = loadedVersion.split('.').map(Number);
  if (a.some(n => n > 65535)) return false;
  for (let i = 0; i < 4; i++) {
    if ((a[i] || 0) !== (b[i] || 0)) return (a[i] || 0) > (b[i] || 0);
  }
  return false;
}
function finish(job, result) {
  if (pending !== job) return;
  clearInterval(job.poll);
  clearTimeout(job.timeout);
  pending = null;
  status = result.success ? 'Forbindelsen virker. Denne prototype skriver endnu ikke salg.' : result.error;
  port?.postMessage({type:'result', requestId:job.requestId, ...result});
  reloadWhenIdle();
}
function connect() {
  if (port || reloadRequested) return;
  port = chrome.runtime.connectNative('dk.provitracker.masterark');
  port.onDisconnect.addListener(() => {
    void chrome.runtime.lastError;
    port = null;
    if (pending) finish(pending, {success:false, error:'Forbindelsen til Provi Tracker blev afbrudt. Prøv igen.'});
    status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
  });
  port.onMessage.addListener(async message => {
    if (message.type === 'reload-extension') {
      if (newerVersion(message.version)) { reloadTarget = message.version; reloadWhenIdle(); }
      return;
    }
    if (message.type === 'extension-update-error') {
      status = 'Udvidelsen kunne ikke opdateres. Åbn Provi Tracker igen, og prøv Kontrollér Chrome.';
      return;
    }
    if (message.type !== 'probe' || pending) return;
    const identity = workbookIdentity(message.workbookUrl);
    if (!identity) return;
    const job = pending = {requestId:message.requestId, identity, heard:false};
    try {
      const tabs = (await chrome.tabs.query({url:'https://5rmarketing-my.sharepoint.com/*'}))
        .filter(tab => workbookIdentity(tab.url) === identity && !tab.discarded);
      if (pending !== job) return;
      if (tabs.length !== 1) {
        finish(job, {success:false, error:tabs.length ? 'Masterarket er åbent flere gange. Lad kun én fane være åben.' : 'Åbn det gemte masterark i Chrome, og log ind.'});
        return;
      }
      job.tabId = tabs[0].id;
      // Reattach the current content script after an extension reload. Keep the workbook intact.
      const frames = await chrome.webNavigation.getAllFrames({tabId:job.tabId});
      if (pending !== job) return;
      const allowed = frames.filter(frame => {
        try { return ['https://5rmarketing-my.sharepoint.com', 'https://euc-excel.officeapps.live.com'].includes(new URL(frame.url).origin); }
        catch { return false; }
      });
      await Promise.allSettled(allowed.map(frame => chrome.scripting.executeScript({
        target:{tabId:job.tabId,frameIds:[frame.frameId]},files:['probe.js']})));
      if (pending !== job) return;
      const probe = () => {
        if (pending !== job) return;
        chrome.tabs.sendMessage(job.tabId, {type:'probe', requestId:job.requestId}).catch(() => {});
      };
      // Retry while Excel renders, without activating the tab or reloading the workbook.
      job.poll = setInterval(probe, 1000);
      job.timeout = setTimeout(() => finish(job, {success:false, error:job.heard
        ? 'Chrome svarede, men fanen Ark1 blev ikke fundet i Excel. Kontrollér at masterarket er færdigindlæst og viser Ark1, og prøv igen.'
        : 'Udvidelsen svarede ikke fra masterarket. Genindlæs Provi Tracker-udvidelsen under chrome://extensions og derefter masterarket.'}), 22000);
      probe();
    } catch {
      finish(job, {success:false, error:'Chrome-kontrollen blev afbrudt. Åbn masterarket, og prøv igen.'});
    }
  });
  poll();
}
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message?.type === 'status' && !sender.tab) { reply({status}); connect(); return; }
  const job = pending;
  if (message?.type !== 'probe-result' || !job || sender.tab?.id !== job.tabId ||
      message.requestId !== job.requestId || workbookIdentity(sender.tab.url) !== job.identity) return;
  job.heard = true;
  if (message.sheet !== 'Ark1') return;
  // Recheck the actual current tab, not a stale sender snapshot after navigation.
  chrome.tabs.get(job.tabId).then(tab => {
    if (pending !== job) return;
    if (workbookIdentity(tab.url) !== job.identity) {
      finish(job, {success:false, error:'Chrome-fanen skiftede til et andet ark under kontrollen. Prøv igen.'});
      return;
    }
    finish(job, {success:true, status:'chrome-connected', background:!tab.active});
  }).catch(() => finish(job, {success:false, error:'Masterarket blev lukket under kontrollen.'}));
});
setInterval(poll, 3000);
chrome.alarms.onAlarm.addListener(() => { connect(); poll(); });
chrome.runtime.onStartup.addListener(connect);
chrome.runtime.onInstalled.addListener(connect);
chrome.alarms.create('reconnect', {periodInMinutes:1});
connect();

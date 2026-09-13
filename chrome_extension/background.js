import {workbookIdentity} from './identity.js';
let port = null;
let pending = null;
let status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
function finish(job, result) {
  if (pending !== job) return;
  clearInterval(job.poll);
  clearTimeout(job.timeout);
  pending = null;
  status = result.success ? 'Forbindelsen virker. Denne prototype skriver endnu ikke salg.' : result.error;
  port?.postMessage({type:'result', requestId:job.requestId, ...result});
}
function connect() {
  if (port) return;
  port = chrome.runtime.connectNative('dk.provitracker.masterark');
  port.onDisconnect.addListener(() => {
    void chrome.runtime.lastError;
    port = null;
    if (pending) finish(pending, {success:false, error:'Forbindelsen til Provi Tracker blev afbrudt. Prøv igen.'});
    status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
  });
  port.onMessage.addListener(async message => {
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
  port.postMessage({type:'poll'});
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
setInterval(() => { if (port && !pending) port.postMessage({type:'poll'}); }, 3000);
chrome.alarms.onAlarm.addListener(connect);
chrome.runtime.onStartup.addListener(connect);
chrome.runtime.onInstalled.addListener(connect);
chrome.alarms.create('reconnect', {periodInMinutes:1});
connect();

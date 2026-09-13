import {workbookIdentity} from './identity.js';
let port = null;
let pending = null;
let status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
function connect() {
  if (port) return;
  port = chrome.runtime.connectNative('dk.provitracker.masterark');
  port.onDisconnect.addListener(() => {
    void chrome.runtime.lastError;
    port = null;
    status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
  });
  port.onMessage.addListener(async message => {
    if (message.type !== 'probe' || pending) return;
    const identity = workbookIdentity(message.workbookUrl);
    if (!identity) return;
    const tabs = (await chrome.tabs.query({url: 'https://5rmarketing-my.sharepoint.com/*'}))
      .filter(tab => workbookIdentity(tab.url) === identity && !tab.discarded);
    if (tabs.length !== 1) {
      port?.postMessage({type: 'result', requestId: message.requestId, success: false,
        error: tabs.length ? 'Masterarket er åbent flere gange. Lad kun én fane være åben.' : 'Åbn det gemte masterark i Chrome, og log ind.'});
      return;
    }
    pending = {requestId: message.requestId, tabId: tabs[0].id, identity};
    try { await chrome.tabs.sendMessage(tabs[0].id, {type:'probe', requestId:message.requestId}); } catch { /* Timeout explains missing content script. */ }
    setTimeout(() => {
      if (pending?.requestId !== message.requestId) return;
      pending = null;
      status = 'Excel svarede ikke. Genindlæs masterarket, og kontrollér login.';
      port?.postMessage({type:'result', requestId:message.requestId, success:false, error:status});
    }, 4000);
  });
  port.postMessage({type:'poll'});
}
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message?.type === 'status' && !sender.tab) { reply({status}); connect(); return; }
  if (message?.type !== 'probe-result' || !pending || sender.tab?.id !== pending.tabId ||
      message.requestId !== pending.requestId || message.sheet !== 'Ark1' ||
      workbookIdentity(sender.tab.url) !== pending.identity) return;
  const requestId = pending.requestId;
  pending = null;
  status = 'Forbindelsen virker. Denne prototype skriver endnu ikke salg.';
  port?.postMessage({type:'result', requestId, success:true, status:'chrome-connected', background: !sender.tab.active});
});
setInterval(() => { if (port && !pending) port.postMessage({type:'poll'}); }, 3000);
chrome.alarms.onAlarm.addListener(connect);
chrome.runtime.onStartup.addListener(connect);
chrome.runtime.onInstalled.addListener(connect);
chrome.alarms.create('reconnect', {periodInMinutes:1});
connect();

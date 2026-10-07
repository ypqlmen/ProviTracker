import {workbookIdentity} from './identity.js';
import {officeStep} from './office_ui.js';
import {validReceipt} from './receipt.js';
let port = null;
let pending = null;
const loadedVersion = chrome.runtime.getManifest().version;
let reloadRequested = false;
let reloadTarget = null;
let status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
const workbookOrigins = ['https://5rmarketing-my.sharepoint.com', 'https://euc-excel.officeapps.live.com'];
function originOf(url) {
  try { return new URL(url).origin; } catch { return null; }
}
function workbookFrames(frames) {
  const byId = new Map(frames.map(frame => [frame.frameId, frame]));
  function permitted(frame, seen = new Set()) {
    if (!frame || seen.has(frame.frameId)) return false;
    seen.add(frame.frameId);
    if (workbookOrigins.includes(originOf(frame.url))) return true;
    // Excel can use a blank/srcdoc frame. Follow only its permitted parent chain.
    return /^about:(blank|srcdoc)(?:[?#]|$)/.test(frame.url) && permitted(byId.get(frame.parentFrameId), seen);
  }
  return frames.filter(frame => permitted(frame));
}
// Only this verified workbook is protected from Memory Saver. This does not
// override Chrome's separate freezing policy; the guide supplies its site exception.
const protectionKey = id => 'masterTab.' + id;
async function protectWorkbook(tab, identity) {
  tab=await chrome.tabs.get(tab.id);
  if (workbookIdentity(tab.url)!==identity) throw new Error('Workbook changed');
  await releaseWorkbook(tab.id,tab);
  const key=protectionKey(tab.id);
  const saved=(await chrome.storage.session.get(key))[key];
  if (!saved) await chrome.storage.session.set({[key]:{identity,autoDiscardable:tab.autoDiscardable!==false}});
  await chrome.tabs.update(tab.id,{autoDiscardable:false});
}
async function releaseWorkbook(id, tab) {
  const key=protectionKey(id);
  const saved=(await chrome.storage.session.get(key))[key];
  if (!saved || (tab && workbookIdentity(tab.url)===saved.identity)) return;
  await chrome.storage.session.remove(key);
  if (tab) await chrome.tabs.update(id,{autoDiscardable:saved.autoDiscardable}).catch(()=>{});
}
function sleeping(job, tab) {
  if (!tab.discarded && !tab.frozen) return false;
  finish(job,{success:false,status:'chrome-sleeping',error:'Chrome har sat masterarket i dvale. Salget afventer automatisk. Tilføj 5rmarketing-my.sharepoint.com under Chrome → Indstillinger → Ydeevne → Hold altid disse websites aktive, og åbn masterark-fanen én gang.'});
  return true;
}
async function confirmWorkbook(job) {
  try {
    const tab = await chrome.tabs.get(job.tabId);
    if (pending !== job) return;
    if (workbookIdentity(tab.url) !== job.identity) {
      finish(job, {success:false, error:'Chrome-fanen skiftede til et andet ark under kontrollen. Prøv igen.'});
      return;
    }
    if (sleeping(job, tab)) return;
    if (job.type==='probe') finish(job, {success:true, status:'chrome-connected', background:!tab.active});
    else if (!job.connected) { job.connected=true; job.phase='open'; }
  } catch { finish(job, {success:false, error:'Masterarket blev lukket under kontrollen.'}); }
}
async function scanWorkbook(job) {
  if (pending !== job || job.scanning) return;
  job.scanning = true;
  try {
    const frames = await chrome.webNavigation.getAllFrames({tabId:job.tabId});
    if (pending !== job) return;
    const allowed = workbookFrames(frames || []);
    const readings = await Promise.allSettled(allowed.map(frame => chrome.scripting.executeScript({
      target:{tabId:job.tabId,frameIds:[frame.frameId]},files:['probe.js']})));
    if (pending !== job) return;
    job.excelBlocked = readings.some((reading, i) => reading.status === 'rejected'
      && originOf(allowed[i].url) === workbookOrigins[1]);
    for (const reading of readings) {
      if (reading.status !== 'fulfilled') continue;
      for (const injected of reading.value || []) {
        const result = injected.result;
        if (!result || !('sheet' in result)) continue;
        job.heard = true;
        if (result.origin === workbookOrigins[1]) job.excelRead = true;
        if (result.sheet === 'Ark1') { await confirmWorkbook(job); return; }
      }
    }
    // Compatibility with an already attached content script. Direct frame results
    // above do not depend on the page's content-message listeners surviving reload.
    chrome.tabs.sendMessage(job.tabId, {type:'probe', requestId:job.requestId}).catch(() => {});
  } catch { /* Frames can change during Excel boot; retry until the bounded deadline. */ }
  finally { job.scanning = false; }
}
const officeOrigin = 'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft';
function officeFrames(frames) {
  const byId=new Map(frames.map(f=>[f.frameId,f]));
  return frames.filter(frame=>{
    if(originOf(frame.url)!==officeOrigin || !/\/sdx\/fa000000043\/[^?#]+\/(index|dialog)\.html(?:[?#]|$)/i.test(frame.url)) return false;
    return originOf(byId.get(frame.parentFrameId)?.url)===workbookOrigins[1];
  });
}
async function scanOffice(job) {
  if(pending!==job || job.scanning) return;
  job.scanning=true;
  try {
    const tab=await chrome.tabs.get(job.tabId);
    if(workbookIdentity(tab.url)!==job.identity) { finish(job,{success:false,error:'Chrome-fanen skiftede masterark. Registreringen er stoppet; kontrollér arket før genforsøg.'});return; }
    if (sleeping(job, tab)) return;
    const frames=await chrome.webNavigation.getAllFrames({tabId:job.tabId}) || [];
    const office=officeFrames(frames);
    const excel=frames.filter(f=>originOf(f.url)===workbookOrigins[1]);
    // Inspect the pane first. Open the gallery only if the script pane is absent.
    const targets=office.length ? office : job.phase==='open' ? excel : [];
    for(const frame of targets) {
      if(pending!==job) return;
      let readings;
      try { readings=await chrome.scripting.executeScript({target:{tabId:job.tabId,frameIds:[frame.frameId]},func:officeStep,args:[job.phase,JSON.stringify(job.registration)]}); }
      catch { job.officeBlocked=true;continue; }
      for(const item of readings) {
        const result=item.result;
        if(!result) continue;
        if(result.error) { finish(job,{success:false,error:result.error});return; }
        if(result.ready) { job.phase='run';return; }
        if(result.started) { job.phase='parameter';return; }
        if(result.filled) { job.phase='submit';return; }
        if(result.submitted) { job.phase='receipt';return; }
        if(result.receipt && validReceipt(result.receipt,job.registration)) {
          const current=await chrome.tabs.get(job.tabId);
          if(workbookIdentity(current.url)!==job.identity) { finish(job,{success:false,error:'Masterarket skiftede under registreringen. Kontrollér salget manuelt.'});return; }
          finish(job,{...result.receipt,background:!current.active});return;
        }
      }
    }
  } catch { /* Frame recreation and transient Excel navigation are retried within the deadline. */ }
  finally { job.scanning=false; }
}
async function scan(job) {
  if (pending!==job) return;
  try {
    const tab=await chrome.tabs.get(job.tabId);
    if (pending!==job) return;
    if (workbookIdentity(tab.url)!==job.identity) {
      finish(job,{success:false,error:'Chrome-fanen skiftede masterark. Kontrollér arket før genforsøg.'});return;
    }
    // Check outside the injection lock: a suspended injected call can stay pending.
    if (sleeping(job,tab)) return;
    return job.connected ? scanOffice(job) : scanWorkbook(job);
  } catch { finish(job,{success:false,error:'Masterarket blev lukket. Åbn det igen, og prøv fra Ordrer.'}); }
}
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
  status = result.success ? (job.type==='probe' ? 'Forbindelsen til masterarket virker.' : 'Excel har bekræftet registreringen.') : result.error;
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
    if (!['probe','setup','register'].includes(message.type) || pending) return;
    const identity = workbookIdentity(message.workbookUrl);
    if (!identity) return;
    const registration=message.type==='setup' ? {isTest:true,requestId:message.requestId} : message.registration;
    if(message.type==='register' && (!registration || registration.requestId!==message.requestId || typeof registration.registrationId!=='string' || registration.isTest===true)) return;
    const job = pending = {requestId:message.requestId, identity, heard:false,type:message.type,registration};
    try {
      const tabs = (await chrome.tabs.query({url:'https://5rmarketing-my.sharepoint.com/*'}))
        .filter(tab => workbookIdentity(tab.url) === identity);
      if (pending !== job) return;
      if (tabs.length !== 1) {
        finish(job, {success:false, error:tabs.length ? 'Masterarket er åbent flere gange. Lad kun én fane være åben.' : 'Åbn det gemte masterark i Chrome, og log ind.'});
        return;
      }
      job.tabId = tabs[0].id;
      await protectWorkbook(tabs[0],identity);
      if (pending!==job || sleeping(job,await chrome.tabs.get(job.tabId))) return;
      // Re-read the current frames while Excel boots, including frames created
      // after the first scan. Never activate or reload the workbook tab.
      job.poll = setInterval(() => scan(job), 500);
      job.timeout = setTimeout(() => finish(job, {success:false, error:job.connected
        ? (job.officeBlocked ? 'Chrome-udvidelsen mangler adgang til Excels scriptpanel. Genindlæs udvidelsen i Chrome, og prøv igen.' : 'Excel bekræftede ikke registreringen. Åbn ProviTrackerSalesRegistrationV3 i kodeeditoren via Automatiser. Kontrollér arket før genforsøg.')
        : job.excelBlocked && !job.excelRead
        ? 'Chrome-udvidelsen har ikke adgang til Excel-rammen. Kontrollér udvidelsens webstedsadgang til euc-excel.officeapps.live.com i Chrome, og prøv igen.'
        : job.heard && !job.excelRead
        ? 'Chrome svarede fra SharePoint, men selve Excel-arket svarede ikke. Genindlæs masterarket i Chrome, kontrollér login, og prøv igen.'
        : job.heard
        ? 'Chrome svarede, men fanen Ark1 blev ikke fundet i Excel. Kontrollér at masterarket er færdigindlæst og viser Ark1, og prøv igen.'
        : 'Udvidelsen svarede ikke fra masterarket. Genindlæs Provi Tracker-udvidelsen under chrome://extensions og derefter masterarket.'}), message.type==='probe' ? 22000 : 150000);
      await scan(job);
    } catch {
      finish(job, {success:false, error:'Chrome-kontrollen blev afbrudt. Åbn masterarket, og prøv igen.'});
    }
  });
  poll();
}
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message?.type === 'status' && !sender.tab) { reply({status}); connect(); return; }
  const job = pending;
  if (message?.type !== 'probe-result' || !job || job.connected || sender.tab?.id !== job.tabId ||
      message.requestId !== job.requestId || workbookIdentity(sender.tab.url) !== job.identity) return;
  job.heard = true;
  if (message.sheet !== 'Ark1') return;
  confirmWorkbook(job);
});
chrome.tabs.onUpdated.addListener((id, change, tab) => {
  if (change.url) releaseWorkbook(id,tab).catch(()=>{});
  if (pending?.tabId===id && (change.discarded || change.frozen)) scan(pending);
});
chrome.tabs.onRemoved.addListener(id => {
  releaseWorkbook(id,null).catch(()=>{});
  if (pending?.tabId===id) finish(pending,{success:false,error:'Masterarket blev lukket. Åbn det igen, og prøv fra Ordrer.'});
});
setInterval(poll, 3000);
chrome.alarms.onAlarm.addListener(() => { connect(); poll(); });
chrome.runtime.onStartup.addListener(connect);
chrome.runtime.onInstalled.addListener(connect);
chrome.alarms.create('reconnect', {periodInMinutes:1});
connect();

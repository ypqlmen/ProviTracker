import {startIntramanager} from './intramanager.js';
import {workbookIdentity} from './identity.js';
import {installStep} from './office_setup.js';
import {officeStep} from './office_ui.js';
import {validReceipt} from './receipt.js';
import {traceStep,diagnosticReport,failureReason} from './diagnostics.js';
let port = null;
let pending = null;
const loadedVersion = chrome.runtime.getManifest().version;
let reloadRequested = false;
let reloadTarget = null;
let status = 'Åbn Provi Tracker, og vælg Kontrollér Chrome.';
const workbookOrigins = ['https://5rmarketing-my.sharepoint.com', 'https://euc-excel.officeapps.live.com'];
const loginOrigins = ['https://login.microsoftonline.com','https://login.microsoft.com','https://login.live.com'];
function originOf(url) {
  try { return new URL(url).origin; } catch { return null; }
}
const launchKey=identity=>'masterLaunch.'+identity;
async function tabUrl(tab) {
  if(tab.url) return tab.url;
  // Host permissions deliberately exclude login pages. Read only the main
  // navigation address to recognize a login redirect; never inject there.
  const frames=await chrome.webNavigation.getAllFrames({tabId:tab.id}) || [];
  return frames.find(frame=>frame.frameId===0)?.url;
}
async function selectWorkbook(job,url) {
  const tabs=(await chrome.tabs.query({})).filter(tab=>workbookIdentity(tab.url)===job.identity || workbookIdentity(tab.pendingUrl)===job.identity);
  if(tabs.length>1) { finish(job,{success:false,error:'Masterarket er åbent flere gange. Lad kun én fane være åben.'});return null; }
  let tab=tabs[0];
  const key=launchKey(job.identity);
  if(!tab) {
    const saved=(await chrome.storage.session.get(key))[key];
    if(saved && Number.isInteger(saved.tabId)) {
      try {
        const previous=await chrome.tabs.get(saved.tabId),address=await tabUrl(previous);
        if(workbookIdentity(address)===job.identity || workbookIdentity(previous.pendingUrl)===job.identity || loginOrigins.includes(originOf(address))) tab=previous;
      } catch { /* The previously opened tab was closed. */ }
      if(!tab) await chrome.storage.session.remove(key);
    }
  }
  if(!tab) { tab=await chrome.tabs.create({url,active:false});job.created=true; }
  if(pending!==job) return null;
  await chrome.storage.session.set({[key]:{tabId:tab.id}});
  return tab;
}
async function workbookAvailable(job,tab) {
  const address=await tabUrl(tab);
  if(pending!==job) return false;
  if(loginOrigins.includes(originOf(address))) {
    if(job.type==='install'){
      if(!job.loginSince){job.loginSince=Date.now();job.connected=false;job.protected=false;job.openSince=null;job.installFrameId=null;job.phase='login';status='Log ind i Microsoft i masterark-fanen. Opsætningen fortsætter automatisk.';port?.postMessage({type:'progress',requestId:job.requestId,phase:'login'});}
      return false;
    }
    finish(job,{success:false,status:'chrome-login-required',error:'Microsoft kræver login i masterark-fanen i Chrome. Salget afventer. Log ind og godkend; automatisk registrering prøver derefter igen.'});return false;
  }
  const next=workbookIdentity(tab.pendingUrl);
  if(next && next!==job.identity) { finish(job,{success:false,error:'Chrome-fanen skiftede masterark. Kontrollér arket før genforsøg.'});return false; }
  if(workbookIdentity(address)===job.identity) {
    if(job.loginSince){job.loginSince=null;job.phase='probe';job.progressPhase=null;}
    if(!job.protected) { await protectWorkbook(tab,job.identity);job.protected=true; }
    return true;
  }
  // Microsoft returns through the tenant's sign-in relay before reopening the workbook.
  // Recognize only that relay; never inject there or accept another workbook.
  if(job.loginSince && originOf(address)===workbookOrigins[0] && /\/(?:_forms\/|_layouts\/15\/authenticate\.aspx)/i.test(new URL(address).pathname))return false;
  // A newly created tab can still report about:blank while its saved URL loads.
  if(!job.connected && (next===job.identity || (job.created && tab.status==='loading' && (!address || address==='about:blank')))) return false;
  finish(job,{success:false,error:'Chrome-fanen skiftede til et andet ark under kontrollen. Prøv igen.'});return false;
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
// Protect only verified workbooks and the dedicated Intramanager tab.
const protectionKey = id => 'masterTab.' + id;
function keepDocumentAwake(release=false) {
  if(release){globalThis.proviBackgroundLease?.release?.();delete globalThis.proviBackgroundLease;return {released:true};}
  if(!navigator.locks)return {supported:false};
  if(!globalThis.proviBackgroundLease){
    const lease=globalThis.proviBackgroundLease={held:false};
    navigator.locks.request('ProviTracker.background.'+crypto.randomUUID(),()=>new Promise(resolve=>{
      lease.held=true;lease.release=()=>{lease.held=false;resolve();};
    })).catch(()=>{delete globalThis.proviBackgroundLease;});
  }
  return {supported:true,held:globalThis.proviBackgroundLease.held};
}
async function protectBackgroundTab(tab) {
  await chrome.tabs.update(tab.id,{autoDiscardable:false});
  if(tab.status==='loading' || tab.discarded || tab.frozen)return;
  await chrome.scripting.executeScript({target:{tabId:tab.id,frameIds:[0]},func:keepDocumentAwake}).catch(()=>{});
}
async function protectIntramanager(tab) {
  if(originOf(tab.url)!=='https://5r.intramanager.com')return;
  const key='intramanagerProtection.'+tab.id;
  const saved=(await chrome.storage.session.get(key))[key];
  if(!saved)await chrome.storage.session.set({[key]:{autoDiscardable:tab.autoDiscardable!==false}});
  await protectBackgroundTab(tab);
}
async function releaseIntramanager(id,tab) {
  const key='intramanagerProtection.'+id,saved=(await chrome.storage.session.get(key))[key];
  if(!saved || (tab && originOf(tab.url)==='https://5r.intramanager.com'))return;
  await chrome.storage.session.remove(key);
  if(tab)await chrome.tabs.update(id,{autoDiscardable:saved.autoDiscardable}).catch(()=>{});
}
async function intramanagerConnection() {
  if(!await chrome.permissions.contains({origins:['https://5r.intramanager.com/*']}))return {connected:false,state:'permission'};
  const id=(await chrome.storage.session.get('intramanagerTab')).intramanagerTab;
  if(!Number.isInteger(id))return {connected:false,state:'unverified'};
  try {
    const tab=await chrome.tabs.get(id);
    if(originOf(tab.url)!=='https://5r.intramanager.com' || tab.url.includes('/login/'))return {connected:false,state:'login'};
    if(tab.discarded || tab.frozen)return {connected:false,state:'sleeping'};
    if(tab.status==='loading')return {connected:false,state:'loading'};
    const result=await Promise.race([
      chrome.scripting.executeScript({target:{tabId:id,frameIds:[0]},func:readIntramanagerProfile}),
      new Promise(resolve=>setTimeout(()=>resolve([]),3000))]);
    const profileName=result?.[0]?.result;
    const account=(await chrome.storage.local.get('intramanagerAccount')).intramanagerAccount;
    const normalize=s=>String(s || '').trim().replace(/\s+/g,' ').toLocaleLowerCase('da');
    if(!account)return {connected:false,state:'unverified'};
    if(profileName && normalize(account)!==normalize(profileName))return {connected:false,state:'account'};
    return profileName?{connected:true,state:'connected',profileName}:{connected:false,state:'unverified'};
  }catch{return {connected:false,state:'unverified'};}
}
function readIntramanagerProfile(){return document.querySelector('#mobile-navigation img[alt]')?.getAttribute('alt')?.trim() || null;}
async function protectWorkbook(tab, identity) {
  tab=await chrome.tabs.get(tab.id);
  if (workbookIdentity(tab.url)!==identity) throw new Error('Workbook changed');
  await releaseWorkbook(tab.id,tab);
  const key=protectionKey(tab.id);
  const saved=(await chrome.storage.session.get(key))[key];
  if (!saved) await chrome.storage.session.set({[key]:{identity,autoDiscardable:tab.autoDiscardable!==false}});
  await protectBackgroundTab(tab);
}
async function releaseWorkbook(id, tab) {
  const key=protectionKey(id);
  const saved=(await chrome.storage.session.get(key))[key];
  if (!saved || (tab && workbookIdentity(tab.url)===saved.identity)) return;
  await chrome.storage.session.remove(key);
  if(!tab || (tab.url && !loginOrigins.includes(originOf(tab.url)))) await chrome.storage.session.remove(launchKey(saved.identity));
  if (tab) await chrome.tabs.update(id,{autoDiscardable:saved.autoDiscardable}).catch(()=>{});
}
async function restoreProtection() {
  const values=await chrome.storage.session.get(null);
  const tabs=await chrome.tabs.query({});
  for(const tab of tabs){
    if(values[protectionKey(tab.id)]?.identity===workbookIdentity(tab.url) && workbookIdentity(tab.url))await protectWorkbook(tab,workbookIdentity(tab.url));
    if(values.intramanagerTab===tab.id)await protectIntramanager(tab);
  }
}
function sleeping(job, tab) {
  if (!tab.discarded && !tab.frozen) return false;
  finish(job,{success:false,status:'chrome-sleeping',error:'Chrome har sat masterarket i dvale. Salget afventer automatisk. Åbn masterark-fanen én gang. Udvidelsen beskytter derefter fanen mod automatisk dvale.'});
  return true;
}
function validSheetName(name) {
  return typeof name==='string' && name.trim().length>0 && name.length<=31 && !/[\x00-\x1f]/.test(name) && name!=='_ProviTrackerReg';
}
async function confirmWorkbook(job, sheetName) {
  try {
    const tab = await chrome.tabs.get(job.tabId);
    if (pending !== job) return;
    if (!await workbookAvailable(job,tab)) return;
    if (sleeping(job, tab)) return;
    if (!validSheetName(sheetName)) return;
    if (job.type==='probe') finish(job, {success:true, status:'chrome-connected', worksheetName:sheetName, background:!tab.active});
    else if (!job.connected) {
      if (job.registration.worksheetName && job.registration.worksheetName!==sheetName) {
        finish(job,{success:false,error:'Salget tilhører arkfanen '+job.registration.worksheetName+'. Vælg den før genforsøg.'});return;
      }
      job.registration.worksheetName=sheetName;job.connected=true;job.phase=(job.type==='install' || job.autoUpdate)?'install-open':'open';
    }
  } catch { finish(job, {success:false, error:'Masterarket blev lukket under kontrollen.'}); }
}
async function scanWorkbook(job) {
  if (pending !== job || job.scanning) return;
  job.scanning = true;
  try {
    const frames = await chrome.webNavigation.getAllFrames({tabId:job.tabId});
    if (pending !== job) return;
    const allowed = workbookFrames(frames || []);
    traceStep(job,'workbook-frames',{frames:(frames || []).length,targets:allowed.length});
    job.inflight=allowed.length;
    const readings = await Promise.allSettled(allowed.map(frame => chrome.scripting.executeScript({
      target:{tabId:job.tabId,frameIds:[frame.frameId]},files:['probe.js']})));
    job.inflight=0;
    if (pending !== job) return;
    readings.forEach((reading,i)=>{if(reading.status==='rejected')traceStep(job,'rejected',{phase:'probe',frame:allowed[i].frameId,reason:failureReason(reading.reason)});});
    job.excelBlocked = readings.some((reading, i) => reading.status === 'rejected'
      && originOf(allowed[i].url) === workbookOrigins[1]);
    for (const reading of readings) {
      if (reading.status !== 'fulfilled') continue;
      for (const injected of reading.value || []) {
        const result = injected.result;
        if (!result || !('sheet' in result)) continue;
        job.heard = true;
        if (result.origin === workbookOrigins[1]) job.excelRead = true;
        if (validSheetName(result.sheet) && result.origin===workbookOrigins[1]) { await confirmWorkbook(job,result.sheet); return; }
      }
    }
    // Compatibility with an already attached content script. Direct frame results
    // above do not depend on the page's content-message listeners surviving reload.
    chrome.tabs.sendMessage(job.tabId, {type:'probe', requestId:job.requestId}).catch(() => {});
  } catch(error) { traceStep(job,'scan-error',{phase:'probe',reason:failureReason(error)}); }
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
    if(!await workbookAvailable(job,tab)) return;
    if (sleeping(job, tab)) return;
    const frames=await chrome.webNavigation.getAllFrames({tabId:job.tabId}) || [];
    const office=officeFrames(frames).filter(frame=>!job.installFrameId || frame.frameId===job.installFrameId || /\/dialog\.html(?:[?#]|$)/i.test(frame.url));
    if(job.installFrameId && !office.some(frame=>frame.frameId===job.installFrameId)){finish(job,{success:false,error:'Excel genindlæste scriptpanelet under opsætningen. Start opsætningen igen; eksisterende scripts bevares.'});return;}
    const excel=frames.filter(f=>originOf(f.url)===workbookOrigins[1]);
    // Inspect the pane first. Open the gallery only if the script pane is absent.
    const dialogs=office.filter(frame=>/\/dialog\.html(?:[?#]|$)/i.test(frame.url));
    const parameterPhase=['open','run','parameter','submit'].includes(job.phase);
    const installing=job.phase?.startsWith('install-');
    if(job.phase==='install-open'){
      job.openSince ??= Date.now();
      if(Date.now()-job.openSince>45000){finish(job,{success:false,error:'Office Scripts kunne ikke åbnes. Kontrollér at Automatiser → Få vist scripts → Seneste scripts kan åbnes i Excel.'});return;}
    }
    const targets=installing ? (job.phase==='install-open' ? [...office,...excel] : office) : parameterPhase && dialogs.length ? dialogs : office.length ? office : job.phase==='open' ? excel : [];
    job.officeMissing=targets.length===0;
    traceStep(job,'office-frames',{phase:job.phase,frames:frames.length,office:office.length,dialogs:dialogs.length,targets:targets.length});
    for(const frame of targets) {
      if(pending!==job) return;
      let readings;
      traceStep(job,'injection',{phase:job.phase,frame:frame.frameId});
      job.inflight=1;
      try { readings=await chrome.scripting.executeScript({target:{tabId:job.tabId,frameIds:[frame.frameId]},func:installing?installStep:officeStep,args:installing?[job.phase,job.source,job.scriptName,job.requestId]:[job.phase,JSON.stringify(job.registration),job.scriptName]}); }
      catch(error) { job.inflight=0;traceStep(job,'rejected',{phase:job.phase,frame:frame.frameId,reason:failureReason(error)});job.officeBlocked=true;continue; }
      job.inflight=0;
      job.officeBlocked=false;
      for(const item of readings) {
        const result=item.result;
        const outcome=!result?'none':result.error?'error':result.waiting?'waiting':result.receipt?'receipt':result.next?'next':result.installed?'installed':['parameters','refill','ready','started','filled','submitted'].find(key=>result[key]) || 'other';
        traceStep(job,'injected',{phase:job.phase,frame:frame.frameId,outcome,reason:result?.waiting,...result?.controls});
        if(!result) continue;
        if(result.error) { finish(job,{success:false,error:result.error});return; }
        if(result.next) {
          if(job.phase==='install-open' && result.next==='install-find')job.installFrameId=frame.frameId;
          job.phase=result.next;return;
        }
        if(result.installed) { job.installed=true;job.phase='open';return; }
        if(result.parameters) { job.phase='parameter';return; }
        if(result.waiting) { job.waiting=result.waiting;return; }
        job.waiting=null;
        if(result.refill) {
          // Hydration/recreated controls can reset the default several times.
          // Bound recovery by time, not three half-second scans. Never run a
          // field that still contains the default or another attempt's payload.
          job.parameterSince ??= Date.now();
          if(Date.now()-job.parameterSince>15000) { finish(job,{success:false,error:'Excel nulstiller stadig payloadJson-feltet. Salget er ikke bekræftet. Genindlæs masterarket, og prøv samme ordre igen.'});return; }
          job.phase='parameter';return;
        }
        if(result.ready) { job.phase='run';return; }
        if(result.started) { job.phase='parameter';return; }
        if(result.filled) { job.parameterSince ??= Date.now();job.phase='submit';return; }
        if(result.submitted) { job.phase='receipt';return; }
        if(result.receipt && validReceipt(result.receipt,job.registration)) {
          const current=await chrome.tabs.get(job.tabId);
          if(!await workbookAvailable(job,current)) return;
          if(job.type==='install' || job.autoUpdate){
            if(!job.installed)return;
            await chrome.storage.local.set({['masterScript.'+job.identity]:{name:job.scriptName}});
            if(job.autoUpdate){
              job.scriptUpdated=true;job.autoUpdate=false;job.registration={...job.saleAfterUpdate,worksheetName:job.registration.worksheetName};delete job.saleAfterUpdate;job.phase='open';job.parameterSince=null;
              traceStep(job,'script-updated',{phase:'open',codeMatched:true});return;
            }
          }
          finish(job,{...result.receipt,background:!current.active});return;
        }
      }
    }
  } catch(error) { traceStep(job,'scan-error',{phase:job.phase,reason:failureReason(error)}); }
  finally { job.scanning=false; }
}
async function scan(job) {
  if(job.type==='intramanager') return job.scan?.();
  if (pending!==job) return;
  try {
    const tab=await chrome.tabs.get(job.tabId);
    if (pending!==job) return;
    if((job.type==='install' || job.hadScriptUpdate) && job.progressPhase!==(job.phase || 'probe')){job.progressPhase=job.phase || 'probe';const elapsed=Math.max(0,Date.now()-job.startedAt);job.phaseTimeline ??=[];const previous=job.phaseTimeline.find(step=>step.phase===job.lastStage);if(previous)previous.lastMs=elapsed;let stage=job.phaseTimeline.find(step=>step.phase===job.progressPhase);if(!stage){stage={phase:job.progressPhase,firstMs:elapsed,lastMs:elapsed,visits:0};job.phaseTimeline.push(stage);}stage.visits++;stage.lastMs=elapsed;job.lastStage=job.progressPhase;status=({probe:'Finder masterarket…','install-open':'Åbner Office Scripts…','install-find':'Finder opsætningsscriptet…','install-name':'Navngiver scriptet…','install-write':'Indsætter koden…','install-save':'Gemmer scriptet…','install-reopen':'Genåbner det gemte script…','install-verify':'Kontrollerer koden…',open:'Åbner kontrollen…',run:'Starter kontrollen…',parameter:'Udfylder kontrollen…',submit:'Sender kontrollen…',receipt:'Afventer Excel…'})[job.progressPhase] || 'Opsætter masterarket…';port?.postMessage({type:'progress',requestId:job.requestId,phase:job.progressPhase});}
    traceStep(job,'tab',{active:tab.active===true,discarded:tab.discarded===true,frozen:tab.frozen===true,locked:job.scanning===true,inflight:job.inflight || 0});
    if (!await workbookAvailable(job,tab)) return;
    // Check outside the injection lock: a suspended injected call can stay pending.
    if (sleeping(job,tab)) return;
    return job.connected ? scanOffice(job) : scanWorkbook(job);
  } catch { finish(job,{success:false,error:job.type==='intramanager'?'Intramanager-fanen blev lukket. Prøv forbindelsen igen.':'Masterarket blev lukket. Åbn det igen, og prøv fra Ordrer.'}); }
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
  const lastStage=job.phaseTimeline?.find(step=>step.phase===job.lastStage);if(lastStage)lastStage.lastMs=Math.max(0,Date.now()-job.startedAt);
  traceStep(job,'finished',{phase:job.phase,outcome:result.success?'result':'error',locked:job.scanning===true,inflight:job.inflight || 0});
  if(job.type==='install' || job.hadScriptUpdate){result.diagnostics={...diagnosticReport({...job,type:'install'},loadedVersion),scriptUpdated:job.scriptUpdated===true};chrome.storage.local.set({lastSetup:result.diagnostics}).catch(()=>{});}
  if(!result.success && job.type!=='intramanager') chrome.storage.local.set({lastFailure:diagnosticReport(job,loadedVersion)}).catch(()=>{});
  if(job.type==='intramanager' && result.success && result.profileName)chrome.storage.local.set({intramanagerAccount:result.profileName}).catch(()=>{});
  pending = null;
  status = result.success ? (job.type==='intramanager' ? 'Intramanager er opdateret.' : job.type==='probe' ? 'Forbindelsen til masterarket virker.' : job.type==='install' ? 'Masterarket er opsat og kontrolleret uden at registrere salg.' : 'Excel har bekræftet registreringen.') : result.error;
  port?.postMessage({type:'result', requestId:job.requestId, worksheetName:job.registration?.worksheetName, extensionVersion:loadedVersion, ...result});
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
    if(pending && message.requestId && message.requestId!==pending.requestId && pending.expiresAt && pending.expiresAt*1000<Date.now())finish(pending,{success:false,error:'Den forrige Chrome-opgave udløb. Forbindelsen er frigivet.'});
    if(message.type==='intramanager' && !pending) {
      const job=pending={...message,startedAt:Date.now(),trace:[]};
      try {await startIntramanager(job,finish,()=>pending===job,{protectTab:protectIntramanager});} catch {finish(job,{success:false,error:'Chrome-forbindelsen til Intramanager kunne ikke starte.'});}
      return;
    }
    if (!['probe','setup','install','register'].includes(message.type) || pending) return;
    const identity = workbookIdentity(message.workbookUrl);
    if (!identity) return;
    const registration=['setup','install'].includes(message.type) ? {isTest:true,requestId:message.requestId} : message.registration;
    if(message.type==='register' && (!registration || registration.requestId!==message.requestId || typeof registration.registrationId!=='string' || registration.isTest===true)) return;
    const job = pending = {requestId:message.requestId,expiresAt:message.expiresAt,identity,heard:false,type:message.type,registration,startedAt:Date.now(),trace:[]};
    traceStep(job,'start',{phase:message.type});
    try {
      const key='masterScript.'+identity,saved=(await chrome.storage.local.get(key))[key];
      const managed=/^ProviTrackerAutoV4_[0-9a-f]{10}$/.test(saved?.name || '');
      job.scriptName=managed?saved.name:'ProviTrackerSalesRegistrationV4';
      if(job.type==='install' || (job.type==='register' && managed)){
        const response=await fetch(chrome.runtime.getURL('excel_online_sales_registration_v4.ts'));
        if(!response.ok)throw Error('Missing packaged script');
        job.source=await response.text();
        if(!job.source.includes('PROVITRACKER_RESULT:') || job.source.length<1000)throw Error('Invalid packaged script');
        const digest=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(job.source));
        const version=Array.from(new Uint8Array(digest)).map(n=>n.toString(16).padStart(2,'0')).join('').slice(0,10);
        const expected='ProviTrackerAutoV4_'+version;
        if(job.type==='install')job.scriptName=expected;
        else if(job.scriptName!==expected){
          job.autoUpdate=true;job.hadScriptUpdate=true;job.saleAfterUpdate=job.registration;
          job.registration={isTest:true,requestId:crypto.randomUUID(),...(job.saleAfterUpdate.worksheetName?{worksheetName:job.saleAfterUpdate.worksheetName}:{})};job.scriptName=expected;
        }
      }
      const tab=await selectWorkbook(job,message.workbookUrl);
      if (pending !== job || !tab) return;
      job.tabId = tab.id;
      // Re-read the current frames while Excel boots, including frames created
      // after the first scan. Never activate or reload the workbook tab.
      job.poll = setInterval(() => scan(job), 500);
      job.timeout = setTimeout(() => finish(job, {success:false, error:job.phase==='login' ? 'Microsoft-login blev ikke afsluttet inden for 10 minutter. Log ind og start opsætningen igen.' : job.phase?.startsWith('install-') ? 'Automatisk opsætning stoppede ved '+job.phase+'. Kontrollér det åbne scriptpanel i Excel, og prøv opsætningen igen.' : job.connected
        ? (job.officeBlocked ? 'Chrome-udvidelsen mangler adgang til Excels scriptpanel. Genindlæs udvidelsen i Chrome, og prøv igen.' : job.officeMissing ? 'Excels scriptvindue blev ikke fundet. Åbn ProviTrackerSalesRegistrationV4 i kodeeditoren, og prøv igen.' : job.waiting==='parameter-script' ? 'Excels parametervindue blev ikke færdigindlæst. Åbn ProviTrackerSalesRegistrationV4, og prøv samme ordre igen.' : job.waiting==='parameter-field' ? 'Excel viser parametervinduet, men payloadJson-feltet er ikke klar til indtastning. Genindlæs masterarket, og prøv igen.' : job.waiting==='parameter-run' ? 'Salgsdataene er indsat, men Excel har ikke aktiveret Kør. Genindlæs masterarket, og prøv samme ordre igen.' : (['parameter','submit','run'].includes(job.phase) ? 'Excel stoppede ved scriptets parametre. Luk parametervinduet, kontrollér at Chrome-udvidelsen er opdateret, og prøv igen fra Ordrer.' : 'Excel bekræftede ikke registreringen. Åbn ProviTrackerSalesRegistrationV4 i kodeeditoren via Automatiser. Kontrollér arket før genforsøg.'))
        : job.excelBlocked && !job.excelRead
        ? 'Chrome-udvidelsen har ikke adgang til Excel-rammen. Kontrollér udvidelsens webstedsadgang til euc-excel.officeapps.live.com i Chrome, og prøv igen.'
        : job.heard && !job.excelRead
        ? 'Chrome svarede fra SharePoint, men selve Excel-arket svarede ikke. Genindlæs masterarket i Chrome, kontrollér login, og prøv igen.'
        : job.heard
        ? 'Chrome svarede, men den valgte arkfane blev ikke fundet i Excel. Kontrollér at masterarket er færdigindlæst, vælg salgsarket, og prøv igen.'
        : 'Udvidelsen svarede ikke fra masterarket. Genindlæs Provi Tracker-udvidelsen under chrome://extensions og derefter masterarket.'}), message.type==='probe' ? 22000 : message.type==='install' ? 600000 : job.hadScriptUpdate ? 390000 : 150000);
      await scan(job);
    } catch {
      finish(job, {success:false, error:'Chrome-kontrollen blev afbrudt. Åbn masterarket, og prøv igen.'});
    }
  });
  poll();
}
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if(message?.type==='intramanager-status' && (!sender.tab || sender.url===chrome.runtime.getURL('popup.html'))){intramanagerConnection().then(reply);return true;}
  if (message?.type === 'status' && !sender.tab) { reply({status,extensionVersion:loadedVersion}); connect(); return; }
  const job = pending;
  if (message?.type !== 'probe-result' || !job || job.connected || sender.tab?.id !== job.tabId ||
      message.requestId !== job.requestId || workbookIdentity(sender.tab.url) !== job.identity) return;
  job.heard = true;
  if (!validSheetName(message.sheet) || originOf(sender.url)!==workbookOrigins[1]) return;
  confirmWorkbook(job,message.sheet);
});
chrome.tabs.onUpdated.addListener((id, change, tab) => {
  if(change.url){releaseWorkbook(id,tab).catch(()=>{});releaseIntramanager(id,tab).catch(()=>{});}
  if(change.status==='complete'){
    chrome.storage.session.get(protectionKey(id)).then(saved=>{if(saved[protectionKey(id)]?.identity===workbookIdentity(tab.url))protectWorkbook(tab,workbookIdentity(tab.url)).catch(()=>{});});
    chrome.storage.session.get('intramanagerTab').then(saved=>{if(saved.intramanagerTab===id)protectIntramanager(tab).catch(()=>{});});
  }
  if (pending?.tabId===id && (change.discarded || change.frozen)) scan(pending);
});
chrome.tabs.onRemoved.addListener(id => {
  releaseWorkbook(id,null).catch(()=>{});releaseIntramanager(id,null).catch(()=>{});
  if (pending?.tabId===id) finish(pending,{success:false,error:pending.type==='intramanager'?'Intramanager-fanen blev lukket. Prøv forbindelsen igen.':'Masterarket blev lukket. Åbn det igen, og prøv fra Ordrer.'});
});
setInterval(poll, 3000);
chrome.alarms.onAlarm.addListener(() => { connect(); poll();restoreProtection().catch(()=>{}); });
chrome.runtime.onStartup.addListener(()=>{connect();restoreProtection().catch(()=>{});});
chrome.runtime.onInstalled.addListener(()=>{connect();restoreProtection().catch(()=>{});});
chrome.alarms.create('reconnect', {periodInMinutes:1});
connect();

export function readIntramanagerPage(request) {
  if(location.origin!=='https://5r.intramanager.com' || location.pathname!=='/reports/history/') return null;
  const profile=document.querySelector('#mobile-navigation img[alt]')?.getAttribute('alt')?.trim();
  if(!profile) return {waiting:true};
  if(request.operation==='profile') return {success:true,profileName:profile};
  const normalize=s=>String(s || '').trim().replace(/\s+/g,' ').toLocaleLowerCase('da');
  if(!request.expectedProfile || normalize(profile)!==normalize(request.expectedProfile))
    return {error:'Intramanager er logget ind med en anden konto end den, du har valgt i Provi Tracker. Skift konto i Chrome.'};
  if(request.operation==='prepare') {
    const from=document.querySelector('#startdate'),to=document.querySelector('#stopdate'),form=document.querySelector('#form-search'),main=document.querySelector('#main');
    if(!from || !to || !form || !main)return {waiting:true};
    if(globalThis.proviIntramanagerQuery?.requestId===request.requestId)return {prepared:true};
    const submit=[...form.querySelectorAll('button[type="submit"]')].filter(el=>/^(Søg|Search)$/.test(el.textContent.trim()));
    if(submit.length!==1 || submit[0].disabled)return {waiting:true};
    for(const el of form.querySelectorAll('input[name="g[]"]:checked'))el.click();
    for(const [el,value]of [[from,request.fromDate],[to,request.toDate]]){
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(el,value);
      el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));
    }
    globalThis.proviIntramanagerQuery?.observer?.disconnect();
    const oldTable=main.querySelector('table'),oldBody=main.querySelector('tbody');
    const query=globalThis.proviIntramanagerQuery={requestId:request.requestId,ready:false};
    query.observer=new MutationObserver(()=>{
      const table=main.querySelector('table'),body=main.querySelector('tbody');
      if((table && (table!==oldTable || body!==oldBody)) || /intet blev fundet/i.test(main.textContent)){
        query.ready=true;query.observer.disconnect();
      }
    });
    query.observer.observe(main,{childList:true,subtree:true,characterData:true});
    submit[0].click();return {prepared:true};
  }
  if(request.requestId && (globalThis.proviIntramanagerQuery?.requestId!==request.requestId || !globalThis.proviIntramanagerQuery.ready))return {waiting:true};
  if(document.querySelector('#startdate')?.value!==request.fromDate || document.querySelector('#stopdate')?.value!==request.toDate) return {waiting:true};
  const main=document.querySelector('#main');if(!main) return {waiting:true};
  const noRows=/intet blev fundet/i.test(main.textContent);
  const headers=[...main.querySelectorAll('thead th,thead td')].map(el=>normalize(el.textContent));
  if(!noRows && (!headers[0]?.includes('projekt') || !headers[2]?.includes('løntimer') || headers[5]!=='grundløn' || headers[6]!=='bonus' || headers[7]!=='provision')) return {waiting:true};
  const money=text=>{
    text=String(text || '').trim().replace(/kr\.?/gi,'').replace(/\s/g,'').replace(/\./g,'').replace(',','.').replace('−','-');
    if(!text || text==='-') return 0;
    return /^-?\d+(?:\.\d+)?$/.test(text)?Number(text):null;
  };
  const hours=text=>{const match=String(text).match(/(\d+)\s*t\.\s*(\d+)\s*min\./);return match?Math.round((Number(match[1])+Number(match[2])/60)*100)/100:null;};
  const total={hours:0,phoneHours:0,sickHours:0,sickPay:0,cancellations:0};
  let count=0;
  for(const row of main.querySelectorAll('tbody tr')) {
    const cells=[...row.querySelectorAll('td')].map(c=>c.innerText.trim());if(cells.length<8) continue;
    const project=normalize(cells[0]);const amounts=[5,6,7].map(i=>money(cells[i]));
    if(amounts.some(n=>n===null)) return {error:'Intramanagers lønbeløb kunne ikke læses entydigt. Der er ikke gemt nye tal.'};
    const wage=amounts.reduce((a,b)=>a+b,0);
    if(/annullering|cancellation/.test(project)){total.cancellations+=wage;count++;continue;}
    const lines=cells[2].split(/\n/).map(s=>s.trim()).filter(Boolean),paid=hours(lines[0]);
    if(paid===null) return {error:'Intramanagers timer kunne ikke læses entydigt. Der er ikke gemt nye tal.'};
    if(/syg|fravær|fravaer|ferie|omsorg|orlov|garantiløn|garantiloen/.test(project)) {total.sickHours+=paid;total.sickPay+=wage;}
    else {total.hours+=paid;total.phoneHours+=hours(lines[1]) || 0;}
    count++;
  }
  if(!count && !noRows) return {waiting:true};
  for(const key in total) total[key]=Math.round(total[key]*100)/100;
  return {success:true,profileName:profile,periodFrom:request.fromDate,periodTo:request.toDate,payrollSchema:2,...total};
}

export async function startIntramanager(job,finish,current,{protectTab=async()=>{}}={}) {
  const origin='https://5r.intramanager.com/*',base='https://5r.intramanager.com/reports/history/';
  const done=result=>finish(job,result);
  if(!await chrome.permissions.contains({origins:[origin]})) {
    done({success:false,error:'Åbn Provi Tracker-udvidelsen i Chrome, vælg Forbind Intramanager, og tillad adgang til 5r.intramanager.com.'});return;
  }
  const tab=await selectIntramanagerTab({create:true});
  if(!current()) return;
  job.tabId=tab.id;job.phase='identity';
  let scanning=false;
  const scan=async()=>{
    if(!current() || scanning) return;scanning=true;
    try {
      const tab=await chrome.tabs.get(job.tabId);
      if((!tab.url || tab.url==='about:blank') && tab.status==='loading') return;
      if(!tab.url){done({success:false,error:'Intramanager-fanen skiftede websted. Prøv igen.'});return;}
      const url=new URL(tab.url);
      if(url.origin!=='https://5r.intramanager.com') {done({success:false,error:'Intramanager-fanen skiftede websted. Prøv forbindelsen igen.'});return;}
      if(url.pathname.startsWith('/login/')) {done({success:false,error:'Log ind på Intramanager i den nye Chrome-fane, og prøv igen.'});return;}
      await protectTab(tab);
      if((tab.frozen || tab.discarded) && !job.recoveredSleep){job.recoveredSleep=true;job.phase='identity';await chrome.tabs.update(tab.id,{url:base});return;}
      if(tab.frozen || tab.discarded) {done({success:false,error:'Åbn Intramanager-fanen én gang, og hold webstedet aktivt under Chromes ydeevneindstillinger.'});return;}
      if(tab.status==='loading') return;
      if(url.pathname!=='/reports/history/') {await chrome.tabs.update(tab.id,{url:base});return;}
      const argument=job.phase==='identity'?{operation:'profile'}:job.phase==='prepare'?{...job,operation:'prepare'}:job;
      const readings=await chrome.scripting.executeScript({target:{tabId:tab.id,frameIds:[0]},func:readIntramanagerPage,args:[{operation:argument.operation,expectedProfile:argument.expectedProfile,fromDate:argument.fromDate,toDate:argument.toDate,requestId:job.requestId}]});
      if(!current()) return;
      const result=readings[0]?.result;if(!result || result.waiting) return;
      if(result.error){done({success:false,error:result.error});return;}
      if(job.phase==='identity') {
        if(job.operation==='profile'){done(result);return;}
        const normalize=s=>String(s || '').trim().replace(/\s+/g,' ').toLocaleLowerCase('da');
        if(normalize(result.profileName)!==normalize(job.expectedProfile)) {done({success:false,error:'Chrome er logget ind med en anden Intramanager-konto. Kontrollér det fulde kontonavn i Indstillinger.'});return;}
        job.phase='prepare';return;
      }
      if(result.prepared){job.phase='results';return;}
      done(result);
    } catch { /* Normal page navigation can replace an execution context. */ }
    finally {scanning=false;}
  };
  job.scan=scan;job.poll=setInterval(scan,500);
  job.timeout=setTimeout(()=>done({success:false,error:job.phase==='results'?'Intramanager indlæste ikke resultaterne for den valgte periode. Kontrollér at Søg virker i timehistorikken. De tidligere tal er bevaret.':'Intramanager svarede ikke. Kontrollér login og webstedsadgang i Chrome.'}),45000);
  await scan();
}

// Tab IDs belong to a browser session. The confirmed account belongs to the
// extension installation and is retained by Chrome across reloads and updates.
let selectingTab=null;
export async function selectIntramanagerTab({create=false}={}) {
  if(selectingTab)return selectingTab;
  selectingTab=(async()=>{
    const base='https://5r.intramanager.com/reports/history/';
    const belongs=tab=>{try{return new URL(tab.url).origin==='https://5r.intramanager.com';}catch{return false;}};
    const saved=(await chrome.storage.session.get('intramanagerTab')).intramanagerTab;
    if(Number.isInteger(saved)){
      try{const tab=await chrome.tabs.get(saved);if(belongs(tab))return tab;}catch{/* Closed or from the previous browser session. */}
    }
    const tabs=(await chrome.tabs.query({url:'https://5r.intramanager.com/*'})).filter(belongs);
    // Reuse an existing history tab without changing the user's other pages.
    // The caller must verify the profile again before reading any payroll.
    let tab=tabs.filter(t=>new URL(t.url).pathname==='/reports/history/').sort((a,b)=>a.id-b.id)[0];
    if(!tab)tab=tabs.filter(t=>new URL(t.url).pathname.startsWith('/login/')).sort((a,b)=>a.id-b.id)[0];
    if(!tab && create)tab=await chrome.tabs.create({url:base,active:false});
    if(tab)await chrome.storage.session.set({intramanagerTab:tab.id});
    return tab || null;
  })();
  try{return await selectingTab;}finally{selectingTab=null;}
}

chrome.runtime.sendMessage({type:'status'}).then(result => {
  document.getElementById('status').textContent = result.status;
  document.getElementById('version').textContent = 'Udvidelse '+result.extensionVersion;
}).catch(() => { document.getElementById('status').textContent = 'Åbn Provi Tracker, og prøv igen.'; });
chrome.storage.local.get('lastFailure').then(({lastFailure})=>{
  const help=document.getElementById('diagnostic-help');
  if(!lastFailure) { help.textContent='Ingen fejloplysninger gemt endnu.';return; }
  help.textContent='Gemmer seneste fejls tekniske trin uden salgsoplysninger, link eller login. Filen sendes ikke automatisk.';
  const button=document.getElementById('diagnostics');button.disabled=false;
  button.addEventListener('click',()=>{
    const address=URL.createObjectURL(new Blob([JSON.stringify(lastFailure,null,2)],{type:'application/json'}));
    const link=document.createElement('a');link.href=address;link.download='ProviTracker-fejl.json';link.click();
    setTimeout(()=>URL.revokeObjectURL(address),10000);
  });
});

document.getElementById('intramanager').addEventListener('click',async()=>{
  const granted=await chrome.permissions.request({origins:['https://5r.intramanager.com/*']});
  document.getElementById('intramanager-status').textContent=granted?'Adgang givet. Log ind i Intramanager, og forbind kontoen i Provi Tracker.':'Adgang blev ikke givet. Din nuværende forbindelse er uændret.';
  if(granted) await chrome.tabs.create({url:'https://5r.intramanager.com/reports/history/',active:true});
});

chrome.storage.local.get('lastSetup').then(({lastSetup})=>{
  const help=document.getElementById('setup-help'),button=document.getElementById('setup-report');
  if(!lastSetup){help.textContent='Ingen automatisk opsætning kørt endnu.';return;}
  help.textContent='Hele det seneste opsætningsforløb med trin og varighed. Ingen salgsdata eller login medtages.';button.disabled=false;
  button.addEventListener('click',()=>{
    const url=URL.createObjectURL(new Blob([JSON.stringify(lastSetup,null,2)],{type:'application/json'}));
    const link=document.createElement('a');link.href=url;link.download='ProviTracker-opsaetning.json';link.click();setTimeout(()=>URL.revokeObjectURL(url),10000);
  });
});

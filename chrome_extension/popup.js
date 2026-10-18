chrome.runtime.sendMessage({type:'status'}).then(result => {
  document.getElementById('status').textContent = result.status;
  document.getElementById('version').textContent = 'Udvidelse '+result.extensionVersion;
}).catch(() => { document.getElementById('status').textContent = 'Åbn Provi Tracker, og prøv igen.'; });
async function refreshIntramanager() {
  const button=document.getElementById('intramanager'),label=document.getElementById('intramanager-status');
  try {
    const result=await chrome.runtime.sendMessage({type:'intramanager-status'});
    button.textContent=result.connected?'Intramanager forbundet':'Forbind Intramanager';
    button.classList.toggle('connected',result.connected===true);
    label.textContent=result.connected?'Forbundet som '+result.profileName+'.':({account:'Chrome er logget ind på en anden Intramanager-konto end den tilsluttede.',permission:'Tillad adgang til Intramanager for at forbinde.',login:'Log ind i Intramanager for at genoprette forbindelsen.',sleeping:'Intramanager er i dvale. Åbn fanen for at genoptage forbindelsen.',loading:'Intramanager indlæser…',unverified:'Forbind kontoen under Indstillinger → Intramanager i Provi Tracker.'})[result.state] || 'Forbindelsen kunne ikke kontrolleres.';
  }catch{label.textContent='Forbindelsen kunne ikke kontrolleres. Åbn Provi Tracker, og prøv igen.';}
}
refreshIntramanager();
setInterval(refreshIntramanager,3000);
document.getElementById('intramanager').addEventListener('click',async()=>{
  const granted=await chrome.permissions.request({origins:['https://5r.intramanager.com/*']});
  if(!granted){document.getElementById('intramanager-status').textContent='Adgang blev ikke givet.';return;}
  const tabs=await chrome.tabs.query({url:'https://5r.intramanager.com/*'});
  if(tabs.length)await chrome.tabs.update(tabs[0].id,{active:true});
  else await chrome.tabs.create({url:'https://5r.intramanager.com/reports/history/',active:true});
  await refreshIntramanager();
});

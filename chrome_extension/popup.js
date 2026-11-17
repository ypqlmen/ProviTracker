chrome.runtime.sendMessage({type:'status'}).then(result => {
  document.getElementById('status').textContent = result.status;
  document.getElementById('version').textContent = 'Udvidelse '+result.extensionVersion;
}).catch(() => { document.getElementById('status').textContent = 'Åbn Provi Tracker, og prøv igen.'; });

async function refreshOutlook(){
  const button=document.getElementById('outlook'),label=document.getElementById('outlook-status');
  try{const s=await chrome.runtime.sendMessage({type:'outlook-status'});button.textContent=s.bound?'Åbn Outlook-forbindelse':'Forbind Outlook';label.textContent=s.bound?'Tilknyttet '+s.senderEmail+'.':s.permission?'Forbind arbejdsmailen i Provi Tracker.':'Tillad Outlook-adgang for at sende salgs reg.';}
  catch{label.textContent='Åbn Provi Tracker for at forbinde Outlook.';}
}
refreshOutlook();setInterval(refreshOutlook,3000);
document.getElementById('outlook').addEventListener('click',async()=>{
  const granted=await chrome.permissions.request({origins:['https://outlook.office.com/*','https://outlook.cloud.microsoft/*','https://outlook.office365.com/*']});
  if(!granted){document.getElementById('outlook-status').textContent='Outlook-adgang blev ikke givet.';return;}
  await chrome.runtime.sendMessage({type:'outlook-connect'});await refreshOutlook();
});

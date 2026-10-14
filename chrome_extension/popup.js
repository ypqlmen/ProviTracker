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

chrome.runtime.sendMessage({type:'status'}).then(result => {
  document.getElementById('status').textContent = result.status;
}).catch(() => { document.getElementById('status').textContent = 'Åbn Provi Tracker, og prøv igen.'; });

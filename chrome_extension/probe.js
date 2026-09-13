// No cookies, customer data, cell values or authentication tokens are read.
chrome.runtime.onMessage.addListener(message => {
  if (message?.type !== 'probe' || typeof message.requestId !== 'string') return;
  const sheet = [...document.querySelectorAll('[role="tab"]')].some(el => {
    const label = el.getAttribute('aria-label') || el.textContent || '';
    return /^(Regneark |Sheet )?Ark1$/.test(label.trim()) && el.getClientRects().length > 0;
  });
  if (sheet) chrome.runtime.sendMessage({type: 'probe-result', requestId: message.requestId, sheet: 'Ark1'}).catch(() => {});
});

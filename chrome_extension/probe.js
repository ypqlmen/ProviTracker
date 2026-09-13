// Read only the sheet controls, including Excel's same-origin about:blank frame.
// No cookies, cell values, authentication tokens or arbitrary page text are read.
function inspectWorkbook(doc, visited = new Set()) {
  if (!doc || visited.has(doc)) return false;
  visited.add(doc);
  const sheet = [...doc.querySelectorAll('[role="tab"]')].some(el => {
    const label = el.getAttribute('aria-label') || el.textContent || '';
    return /^(Regneark |Sheet )?Ark1$/.test(label.trim()) && el.getClientRects().length > 0;
  });
  if (sheet) return true;
  for (const frame of doc.querySelectorAll('iframe')) {
    try { if (inspectWorkbook(frame.contentDocument, visited)) return true; }
    catch { /* Cross-origin frames are outside this script's access. */ }
  }
  return false;
}
chrome.runtime.onMessage.addListener(message => {
  if (message?.type !== 'probe' || typeof message.requestId !== 'string') return;
  chrome.runtime.sendMessage({type: 'probe-result', requestId: message.requestId,
    sheet: inspectWorkbook(document) ? 'Ark1' : null}).catch(() => {});
});

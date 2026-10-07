// Read only the sheet controls, including Excel's same-origin about:blank frame.
// No cookies, cell values, authentication tokens or arbitrary page text are read.
function inspectWorkbook(doc, visited = new Set()) {
  if (!doc || visited.has(doc)) return false;
  visited.add(doc);
  const sheet = [...doc.querySelectorAll('[role="tab"]')].some(el => {
    // Excel's actual sheet control has sheet-title. It remains identifiable when
    // a background tab is not painted; ribbon tabs or cell text cannot match it.
    const title = el.querySelector('[sheet-title]')?.getAttribute('sheet-title');
    if (title !== undefined && title !== null) return title === 'Ark1';
    const label = el.getAttribute('aria-label') || el.textContent || '';
    return /^(Regneark |Sheet )?Ark1$/.test(label.replace(/\s+/g, ' ').trim()) && el.getClientRects().length > 0;
  });
  if (sheet) return true;
  for (const frame of doc.querySelectorAll('iframe')) {
    try { if (inspectWorkbook(frame.contentDocument, visited)) return true; }
    catch { /* Cross-origin frames are outside this script's access. */ }
  }
  return false;
}
// executeScript can be called repeatedly for the same open workbook.
if (globalThis.proviWorkbookProbeHandler) chrome.runtime.onMessage.removeListener(globalThis.proviWorkbookProbeHandler);
globalThis.proviWorkbookProbeHandler = message => {
  if (message?.type !== 'probe' || typeof message.requestId !== 'string') return;
  chrome.runtime.sendMessage({type: 'probe-result', requestId: message.requestId,
    sheet: inspectWorkbook(document) ? 'Ark1' : null}).catch(() => {});
};
chrome.runtime.onMessage.addListener(globalThis.proviWorkbookProbeHandler);
// executeScript returns this result directly from the target frame. A reply
// from SharePoint alone is not evidence that the separate Excel frame was read.
({sheet: inspectWorkbook(document) ? 'Ark1' : null, origin: document.location?.origin || null});

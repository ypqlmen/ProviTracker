// Read only the sheet controls, including Excel's same-origin about:blank frame.
// No cookies, cell values, authentication tokens or arbitrary page text are read.
function inspectWorkbook(doc, visited = new Set()) {
  if (!doc || visited.has(doc)) return null;
  visited.add(doc);
  const names = new Set();
  for (const el of doc.querySelectorAll('[role="tab"]')) {
    if (el.getAttribute('aria-selected') !== 'true') continue;
    // Only Excel sheet controls count. Ribbon tabs have no sheet-title and
    // no localized Sheet/Regneark prefix. Background paint is not required.
    const title = el.querySelector('[sheet-title]')?.getAttribute('sheet-title');
    const label = (el.getAttribute('aria-label') || '').replace(/\s+/g, ' ').trim();
    const fallback = label.match(/^(?:Regneark |Sheet )(.+)$/);
    const name = title ?? (el.getClientRects().length > 0 ? fallback?.[1] : null);
    if (typeof name === 'string' && name.trim() && name !== '_ProviTrackerReg') names.add(name);
  }
  for (const frame of doc.querySelectorAll('iframe')) {
    try { const name=inspectWorkbook(frame.contentDocument, visited);if(name) names.add(name); }
    catch { /* Cross-origin frames are outside this script's access. */ }
  }
  return names.size === 1 ? [...names][0] : null;
}
// executeScript can be called repeatedly for the same open workbook.
if (globalThis.proviWorkbookProbeHandler) chrome.runtime.onMessage.removeListener(globalThis.proviWorkbookProbeHandler);
globalThis.proviWorkbookProbeHandler = message => {
  if (message?.type !== 'probe' || typeof message.requestId !== 'string') return;
  chrome.runtime.sendMessage({type: 'probe-result', requestId: message.requestId,
    sheet: inspectWorkbook(document)}).catch(() => {});
};
chrome.runtime.onMessage.addListener(globalThis.proviWorkbookProbeHandler);
// executeScript returns this result directly from the target frame. A reply
// from SharePoint alone is not evidence that the separate Excel frame was read.
({sheet: inspectWorkbook(document), origin: document.location?.origin || null});

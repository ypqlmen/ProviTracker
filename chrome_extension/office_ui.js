// Operate visible Office Scripts controls only. No cookies, tokens or private page APIs.
// Serialized by executeScript: all helpers deliberately live inside this function.
export function officeStep(phase, payloadJson) {
  const scriptName = 'ProviTrackerSalesRegistrationV3';
  const text = el => (el.getAttribute('aria-label') || el.textContent || '').replace(/\s+/g,' ').trim();
  const usable = el => !el.disabled && !el.closest('[aria-hidden="true"],[hidden]')
    && getComputedStyle(el).display !== 'none' && getComputedStyle(el).visibility !== 'hidden';
  const find = (selector, names) => {
    const matches = [...document.querySelectorAll(selector)].filter(el => usable(el) && names.includes(text(el)));
    return matches.length === 1 ? matches[0] : null;
  };
  const button = names => find('button,[role="button"]',names);
  const named = [...document.querySelectorAll('span,div,p,h1,h2,h3')].find(el => el.children.length===0 && text(el)===scriptName);
  const editor = document.querySelector('textarea[aria-label="editor"]');
  if (location.origin === 'https://euc-excel.officeapps.live.com') {
    if (phase !== 'open') return null;
    const menu = document.getElementById('OverflowPivotHeader-Automate');
    if (menu && usable(menu)) { menu.click(); return {progress:true}; }
    const tab = document.getElementById('Automate');
    if (!tab || tab.getAttribute('aria-selected') !== 'true') {
      const control = tab && usable(tab) ? tab : document.getElementById('pivotOverflowButton');
      if (control && usable(control)) control.click();
      return null;
    }
    const recent = find('[role="menuitem"],[role="menuitemradio"]',['Seneste scripts','Recent scripts']);
    if (recent) { recent.click(); return null; }
    const view = document.getElementById('AutomateGroupViewScripts');
    if (view && usable(view) && view.getAttribute('aria-expanded') !== 'true') view.click();
    return null;
  }
  if (location.origin !== 'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft') return null;
  if (phase === 'open' && /\/index\.html$/.test(location.pathname)) {
    if (editor && usable(editor)) return named ? {ready:true} : {error:'Et andet script er åbent i Excel. Åbn ProviTrackerSalesRegistrationV3 i kodeeditoren, og prøv igen.'};
    if (named) {
      for (let ancestor=named.parentElement, depth=0; ancestor && depth<7; ancestor=ancestor.parentElement,depth++) {
        const edit = [...ancestor.querySelectorAll('button,[role="button"]')].filter(el=>usable(el)&&['Rediger','Edit'].includes(text(el)));
        if (edit.length===1) { edit[0].click(); return null; }
        if (edit.length>1) break;
      }
    }
    return null;
  }
  if (phase === 'run' && /\/index\.html$/.test(location.pathname) && editor && usable(editor) && named) {
    const run = button(['Kør','Run']);
    if (!run) return null;
    // Each attempt may open the parameter dialog once only, even after reinjection.
    const request = JSON.parse(payloadJson);
    if (globalThis.proviScriptRun === request.requestId) return {started:true};
    globalThis.proviScriptRun = request.requestId;
    run.click();
    return {started:true};
  }
  if (['parameter','submit'].includes(phase) && /\/dialog\.html$/.test(location.pathname)) {
    if (!document.body.textContent.includes(scriptName)) return {error:'Excel åbnede parametre til et andet script. Registreringen er stoppet.'};
    const inputs = [...document.querySelectorAll('input[type="text"]')].filter(el=>usable(el)&&/^(Angiv en streng|Enter a string)/.test(el.placeholder));
    const run = button(['Kør','Run']);
    if (inputs.length!==1) return null;
    const request=JSON.parse(payloadJson);
    if (globalThis.proviScriptSubmitted===request.requestId) return {submitted:true};
    if (phase==='submit') {
      if (!run || inputs[0].value!==payloadJson) return null;
      globalThis.proviScriptSubmitted=request.requestId;
      run.click();
      return {submitted:true};
    }
    // Native value setter + input event delivers one complete value to React.
    const setter=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
    setter.call(inputs[0],payloadJson);
    inputs[0].dispatchEvent(new Event('input',{bubbles:true}));
    inputs[0].dispatchEvent(new Event('change',{bubbles:true}));
    if (inputs[0].value!==payloadJson) return {error:'Excel modtog ikke hele salgsregistreringen.'};
    // Let the page apply its state update before clicking Run in the next scan.
    return {filled:true};
  }
  if (phase === 'receipt' && /\/index\.html$/.test(location.pathname) && named) {
    for (const item of document.querySelectorAll('[role="listitem"]')) {
      const value=item.textContent || '';
      const match=value.match(/PROVITRACKER_RESULT:\s*(\{[^\n]*\})/);
      if (match) {
        try { const receipt=JSON.parse(match[1]); if(receipt.requestId===JSON.parse(payloadJson).requestId) return {receipt}; } catch {}
      }
      if (/^(Fejl:|Error:)/.test(value.trim())) return {error:value.trim().slice(0,500)};
    }
  }
  return null;
}

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
  if (['open','run','parameter','submit'].includes(phase) && /\/dialog\.html$/.test(location.pathname)) {
    if (!document.body.textContent.includes(scriptName)) return {error:'Excel åbnede parametre til et andet script. Registreringen er stoppet.'};
    const fields=[...document.querySelectorAll('input,textarea')].filter(el=>usable(el)
      && (el.tagName==='TEXTAREA' || el.type==='text'));
    const parameterName=value=>/^payloadJson\s*\*?$/.test((value || '').trim());
    const labels=el=>[el.getAttribute('aria-label'),el.name,
      ...[...(el.labels || [])].map(label=>label.textContent),
      ...(el.getAttribute('aria-labelledby') || '').split(/\s+/).filter(Boolean).map(id=>document.getElementById(id)?.textContent)];
    const namedFields=fields.filter(el=>labels(el).some(parameterName));
    const inputs=namedFields.length ? namedFields : fields.filter(el=>/^(Angiv en streng|Enter a string)/.test(el.placeholder || ''));
    const run=button(['Kør','Run']);
    if (inputs.length>1) return {error:'Excel viser flere mulige payloadJson-felter. Registreringen er stoppet.'};
    if (inputs.length!==1) return null;
    const input=inputs[0],request=JSON.parse(payloadJson);
    // A previous attempt can leave this dialog open. Resume its parameter step
    // instead of clicking the editor's Run button behind the modal a second time.
    if (['open','run'].includes(phase)) {
      if (globalThis.proviScriptSubmitted && globalThis.proviScriptSubmitted!==request.requestId)
        return {error:'Et tidligere Excel-forsøg har allerede valgt Kør. Kontrollér salget og luk parametervinduet før genforsøg.'};
      return {parameters:true};
    }
    if (globalThis.proviScriptSubmitted===request.requestId) return {submitted:true};
    if (phase==='submit') {
      // A controlled field can revert while the page applies its state update.
      // Return to filling; never submit the default test value or an older sale.
      if (input.value!==payloadJson) return {refill:true};
      if (!run) return null;
      globalThis.proviScriptSubmitted=request.requestId;
      run.click();
      return {submitted:true};
    }
    const prototype=input.tagName==='TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    const setter=Object.getOwnPropertyDescriptor(prototype,'value').set;
    setter.call(input,payloadJson);
    input.dispatchEvent(new Event('input',{bubbles:true}));
    input.dispatchEvent(new Event('change',{bubbles:true}));
    if (input.value!==payloadJson) return {refill:true};
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

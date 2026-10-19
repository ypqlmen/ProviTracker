// Operate visible Office Scripts controls only. No cookies, tokens or private page APIs.
// Serialized by executeScript: all helpers deliberately live inside this function.
export function officeStep(phase, payloadJson, scriptName = 'ProviTrackerSalesRegistrationV4') {
  const text = el => (el.getAttribute('aria-label') || el.textContent || '').replace(/\s+/g,' ').trim();
  const usable=el=>{
    if(el.disabled || el.closest('[aria-hidden="true"],[hidden]'))return false;
    // Excel's floating ribbon menus override visibility:hidden on their
    // portal ancestors. Only the target's computed visibility is decisive;
    // display:none on any ancestor still hides the entire subtree.
    if(['hidden','collapse'].includes(getComputedStyle(el).visibility))return false;
    for(let parent=el;parent;parent=parent.parentElement)if(getComputedStyle(parent).display==='none')return false;
    return true;
  };
  const find = (selector, names) => {
    const matches = [...document.querySelectorAll(selector)].filter(el => usable(el) && names.some(name=>name.toLocaleLowerCase()===text(el).toLocaleLowerCase()));
    return matches.length === 1 ? matches[0] : null;
  };
  const button = names => find('button,[role="button"]',names);
  const requestId=JSON.parse(payloadJson).requestId;
  const state=globalThis.proviOfficeActivation?.requestId===requestId?globalThis.proviOfficeActivation:(globalThis.proviOfficeActivation={requestId});
  const activate=el=>{
    if(!usable(el))return false;
    // React/Fluent may replace a menu node after a click. Keep attempts by
    // semantic control identity, not a detached element reference. Retrying the
    // replacement must still advance to keyboard activation.
    const key=JSON.stringify([phase,el.id || el.getAttribute('data-unique-id') || '',el.getAttribute('role') || el.tagName,text(el).toLocaleLowerCase()]);
    state.activations ??= new Map();
    const now=Date.now(),last=state.activations.get(key);
    if(last && now-last.at<1200)return false;
    const retry=!!last;
    state.activations.set(key,{at:now});
    // Inactive Chrome tabs can update activeElement without emitting focus
    // events. Fluent menus need the same focus lifecycle as a normal click.
    const seen=new Set(),observe=event=>seen.add(event.type);
    for(const type of ['focus','focusin'])el.addEventListener(type,observe);
    el.focus({preventScroll:true});
    for(const type of ['focus','focusin']){
      el.removeEventListener(type,observe);
      if(!seen.has(type))el.dispatchEvent(new FocusEvent(type,{bubbles:type==='focusin',composed:true}));
    }
    if(!el.isConnected)return false;
    if(retry){
      for(const type of ['keydown','keyup'])el.dispatchEvent(new KeyboardEvent(type,{key:'Enter',code:'Enter',keyCode:13,which:13,bubbles:true,cancelable:true,composed:true}));
    }else{
      for(const type of ['pointerdown','mousedown','pointerup','mouseup'])el.dispatchEvent(type.startsWith('pointer')?new PointerEvent(type,{bubbles:true,composed:true,pointerId:1,pointerType:'mouse',isPrimary:true,button:0,buttons:type.endsWith('down')?1:0}):new MouseEvent(type,{bubbles:true,composed:true,button:0,buttons:type.endsWith('down')?1:0,view:window}));
      el.click();
    }
    return true;
  };
  const named = [...document.querySelectorAll('span,div,p,h1,h2,h3')].find(el => el.children.length===0 && text(el)===scriptName);
  const editor = document.querySelector('textarea[aria-label="editor"]');
  if (location.origin === 'https://euc-excel.officeapps.live.com') {
    if (phase !== 'open') return null;
    const menu = document.getElementById('OverflowPivotHeader-Automate');
    if (menu && usable(menu)) { activate(menu); return {progress:true}; }
    const tab = document.getElementById('Automate');
    if (!tab || tab.getAttribute('aria-selected') !== 'true') {
      const control = tab && usable(tab) ? tab : document.getElementById('pivotOverflowButton');
      if (control && usable(control)) activate(control);
      return null;
    }
    const recent = find('[role="menuitem"],[role="menuitemradio"]',['Seneste scripts','Recent scripts']);
    if (recent) { activate(recent); return null; }
    const view = document.getElementById('AutomateGroupViewScripts');
    if (view && usable(view) && view.getAttribute('aria-expanded') !== 'true') activate(view);
    return null;
  }
  if (location.origin !== 'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft') return null;
  if (phase === 'open' && /\/index\.html$/.test(location.pathname)) {
    const recentTab=[...document.querySelectorAll('[role="tab"]')].find(el=>usable(el)&&(/seneste scripts|recent scripts/i.test(text(el)) || /^(Seneste|Recent)\b/.test(el.textContent?.trim() || '')));
    if(!editor && recentTab && recentTab.getAttribute('aria-selected')!=='true'){activate(recentTab);return null;}
    if (editor && usable(editor)) return named ? {ready:true} : {error:'Et andet script er åbent i Excel. Åbn ProviTrackerSalesRegistrationV4 i kodeeditoren, og prøv igen.'};
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
    if (!document.body.textContent.includes(scriptName)) {
      // A discovered dialog frame can precede its asynchronously rendered form.
      // Absence of a name does not identify a different script. Wait without
      // touching any input; reject only an explicitly rendered different title.
      const title=[...document.querySelectorAll('span,div,p,h1,h2,h3')]
        .filter(el=>el.children.length===0 && usable(el))
        .map(text).find(value=>/^(Angiv parametre for|Enter parameters for)\s+/.test(value));
      const controls={scriptMatched:false,titleKnown:!!title};
      if(title) return {error:'Excel åbnede parametre til et andet script. Registreringen er stoppet.',controls};
      return {waiting:'parameter-script',controls};
    }
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
    if (inputs.length!==1) return {waiting:'parameter-field',controls:{scriptMatched:true,fields:fields.length,matches:inputs.length}};
    const input=inputs[0],request=JSON.parse(payloadJson);
    const report=result=>({...result,controls:{scriptMatched:true,fields:fields.length,matches:inputs.length,
      matched:input.value===payloadJson,defaultTest:input.value==='{}' || /^\s*\{\s*"isTest"\s*:\s*true\s*\}\s*$/.test(input.value),
      attached:input.isConnected,readOnly:input.readOnly,disabled:input.disabled,runEnabled:!!run}});
    // A previous attempt can leave this dialog open. Resume its parameter step
    // instead of clicking the editor's Run button behind the modal a second time.
    if (['open','run'].includes(phase)) {
      if (globalThis.proviScriptSubmitted && globalThis.proviScriptSubmitted!==request.requestId)
        return {error:'Et tidligere Excel-forsøg har allerede valgt Kør. Kontrollér salget og luk parametervinduet før genforsøg.'};
      return report({parameters:true});
    }
    if (globalThis.proviScriptSubmitted===request.requestId) return report({submitted:true});
    if (phase==='submit') {
      // A controlled field can revert while the page applies its state update.
      // Return to filling; never submit the default test value or an older sale.
      if (input.value!==payloadJson) return report({refill:true});
      if (!run) return report({waiting:'parameter-run'});
      globalThis.proviScriptSubmitted=request.requestId;
      run.click();
      return report({submitted:true});
    }
    if (input.readOnly) return report({waiting:'parameter-field'});
    // Fluent's parameter form can appear before its controlled input has finished
    // initializing. Use the normal focus/edit/blur sequence; the background job
    // checks the value again on a later scan before choosing Run.
    const focusControl=focused=>{
      const types=focused ? ['focus','focusin'] : ['blur','focusout'];
      const seen=new Set(),observe=event=>seen.add(event.type);
      types.forEach(type=>input.addEventListener(type,observe));
      if(focused) input.focus({preventScroll:true});else input.blur();
      types.forEach(type=>input.removeEventListener(type,observe));
      // In an inactive tab Chrome changes activeElement without emitting these
      // events. Commit the same form lifecycle without activating the browser
      // tab, and avoid double events when the native focus/blur did emit them.
      for(const type of types) if(!seen.has(type)) input.dispatchEvent(new FocusEvent(type,{bubbles:type==='focusin'||type==='focusout',composed:true}));
    };
    focusControl(true);
    const prototype=input.tagName==='TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    const setter=Object.getOwnPropertyDescriptor(prototype,'value').set;
    setter.call(input,payloadJson);
    input.dispatchEvent(new InputEvent('input',{bubbles:true,composed:true,inputType:'insertReplacementText',data:payloadJson}));
    input.dispatchEvent(new Event('change',{bubbles:true}));
    focusControl(false);
    if (input.value!==payloadJson) return report({refill:true});
    return report({filled:true});
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

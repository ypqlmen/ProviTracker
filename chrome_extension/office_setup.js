// Serialized into the verified Office Scripts frame. Only ordinary UI events are used.
// No page internals, credentials, clipboard permissions or Microsoft service APIs.
export function installStep(phase, source, scriptName, requestId) {
  const origin='https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft';
  const label=el=>(el.getAttribute('aria-label') || el.textContent || '').replace(/\s+/g,' ').trim();
  const visible=el=>{
    if(el.closest('[hidden],[aria-hidden="true"]'))return false;
    for(let parent=el;parent;parent=parent.parentElement){const style=getComputedStyle(parent);if(style.display==='none' || style.visibility==='hidden')return false;}
    return true;
  };
  const unique=(selector,names)=>{const all=[...document.querySelectorAll(selector)].filter(el=>visible(el)&&names.some(name=>name.toLocaleLowerCase()===label(el).toLocaleLowerCase()));return all.length===1?all[0]:null;};
  const button=names=>unique('button,[role="button"]',names);
  const enabled=el=>el && !el.disabled && el.getAttribute('aria-disabled')!=='true';
  const state=globalThis.proviInstaller?.requestId===requestId?globalThis.proviInstaller:(globalThis.proviInstaller={requestId});
  const next=phase=>({next:phase});
  const activate=el=>{
    if(!enabled(el))return false;
    const now=Date.now(),last=state.activation;
    if(last?.el===el && now-last.at<1200)return false;
    const retry=last?.el===el;
    state.activation={el,at:now};
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
      for(const type of ['keydown','keyup'])el.dispatchEvent(new KeyboardEvent(type,{key:'Enter',code:'Enter',keyCode:13,which:13,bubbles:true,cancelable:true}));
    }else{
      for(const type of ['pointerdown','mousedown','pointerup','mouseup'])el.dispatchEvent(type.startsWith('pointer')?new PointerEvent(type,{bubbles:true,composed:true,pointerId:1,pointerType:'mouse',isPrimary:true,button:0,buttons:type.endsWith('down')?1:0}):new MouseEvent(type,{bubbles:true,composed:true,button:0,buttons:type.endsWith('down')?1:0,view:window}));
      el.click();
    }
    return true;
  };
  if(location.origin==='https://euc-excel.officeapps.live.com') {
    if(phase!=='install-open')return null;
    const overflow=document.getElementById('OverflowPivotHeader-Automate');
    if(overflow && visible(overflow)){activate(overflow);return null;}
    const tab=document.getElementById('Automate');
    if(!tab || tab.getAttribute('aria-selected')!=='true'){
      const control=tab && visible(tab)?tab:document.getElementById('pivotOverflowButton');if(enabled(control))activate(control);return null;
    }
    const all=unique('[role="menuitem"],[role="menuitemradio"]',['Alle scripts','All scripts','Seneste scripts','Recent scripts']);
    if(all){activate(all);return null;}
    const view=document.getElementById('AutomateGroupViewScripts');
    if(enabled(view)){
      state.viewSince ??= Date.now();
      if(view.getAttribute('aria-expanded')!=='true' || Date.now()-state.viewSince>2500){activate(view);state.viewSince=Date.now();}
    }
    return null;
  }
  if(location.origin===origin && /\/dialog\.html$/.test(location.pathname) && document.body.textContent.trim())return {error:'Excel har allerede et parametervindue åbent. Luk det vindue, før automatisk opsætning startes.',waiting:'office-dialog'};
  if(location.origin!==origin || !/\/index\.html$/.test(location.pathname))return null;
  const editor=document.querySelector('textarea[aria-label="editor"]');
  const named=[...document.querySelectorAll('span,div,p,h1,h2,h3')].filter(el=>el.children.length===0 && visible(el) && label(el)===scriptName);
  const saved=()=>{const control=unique('button',['Scriptet er gemt','Script saved']);return control && !enabled(control);};
  const dirty=()=>enabled(button(['Gem script','Save script']));
  const back=()=>button(['Tilbage','Back']);
  if(phase==='install-open'){
    if(editor && visible(editor)){
      if(dirty())return {error:'Gem eller fjern dine åbne scriptændringer i Excel, før automatisk opsætning startes.'};
      const control=back();if(enabled(control))control.click();return null;
    }
    const filter=unique('input',['Filtrer scripts','Filter scripts']);
    if(filter)return next('install-find');
    return null;
  }
  if(phase==='install-find' || phase==='install-reopen'){
    const recent=[...document.querySelectorAll('[role="tab"]')].find(el=>visible(el)&&(/seneste scripts|recent scripts/i.test(label(el)) || /^(Seneste|Recent)\b/.test(el.textContent?.trim() || '')));
    if(recent && recent.getAttribute('aria-selected')!=='true'){activate(recent);state.libraryReady=false;return null;}
    const filter=unique('input',['Filtrer scripts','Filter scripts']);if(!filter)return null;
    // A library scan must not be restricted by a user's old search.
    if(filter.value!==''){
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(filter,'');filter.dispatchEvent(new Event('input',{bubbles:true}));filter.dispatchEvent(new Event('change',{bubbles:true}));state.libraryReady=false;return null;
    }
    if(!state.libraryReady){state.libraryReady=true;return null;}
    const cards=[...document.querySelectorAll('article')].filter(el=>visible(el)&&label(el)==='Script: '+scriptName);
    if(cards.length>1)return {error:'Der findes flere opsætningsscripts med samme navn. Åbn det korrekte script i Excel, og kontrollér opsætningen manuelt.'};
    if(cards.length===1){
      const edit=[...cards[0].querySelectorAll('button')].filter(el=>visible(el)&&['Rediger','Edit'].includes(label(el)));
      if(edit.length!==1 || !enabled(edit[0]))return null;
      state.reopened=phase==='install-reopen';edit[0].click();return next('install-verify');
    }
    if(phase==='install-reopen')return null; // Saving is not confirmed until the library can reopen it.
    const create=unique('[role="menuitem"]',['Opret i Kodeeditor','Create in Code Editor']);
    if(create && !state.createClicked){state.createClicked=true;create.click();return next('install-name');}
    const control=button(['Nyt script','New Script','New script']);
    if(enabled(control)&&!state.newClicked){state.newClicked=true;control.click();}
    return null;
  }
  if(phase==='install-name'){
    if(!state.createClicked)return {error:'Opsætningen mistede forbindelsen til sin nye kladde. Ingen eksisterende scripts er ændret.'};
    if(!editor || !visible(editor))return null;
    if(named.length===1)return next('install-write');
    const input=unique('input',['Angiv scriptnavn [påkrævet]','Enter script name [required]']);
    if(input){
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(input,scriptName);
      input.dispatchEvent(new Event('input',{bubbles:true}));input.dispatchEvent(new Event('change',{bubbles:true}));
      input.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',code:'Enter',keyCode:13,which:13,bubbles:true,cancelable:true}));input.blur();return null;
    }
    const rename=button(['Omdøb','Rename']);if(enabled(rename))rename.click();return null;
  }
  const normalize=value=>value.replace(/\r\n/g,'\n').replace(/\s+$/,'');
  const focus=()=>{
    editor.focus({preventScroll:true});editor.dispatchEvent(new FocusEvent('focus',{composed:true}));editor.dispatchEvent(new FocusEvent('focusin',{bubbles:true,composed:true}));
  };
  const selectAll=()=>{
    focus();const mac=/Mac/.test(navigator.platform);
    for(const type of ['keydown','keyup'])editor.dispatchEvent(new KeyboardEvent(type,{key:'a',code:'KeyA',keyCode:65,which:65,ctrlKey:!mac,metaKey:mac,bubbles:true,cancelable:true}));
  };
  const readCode=()=>{selectAll();const data=new DataTransfer();editor.dispatchEvent(new ClipboardEvent('copy',{clipboardData:data,bubbles:true,cancelable:true}));return data.getData('text/plain');};
  if(['install-write','install-save','install-verify'].includes(phase)){
    if(!editor || !visible(editor))return null;
    if(named.length!==1)return {error:'Excel viser et andet script end opsætningsscriptet. Forløbet er stoppet uden at overskrive det.'};
    if(phase==='install-write'){
      if(!state.createClicked)return {error:'Kun en kladde oprettet i dette forløb må udfyldes automatisk.'};
      if(!state.pasted){
        const before=readCode();if(!before)return {error:'Excel-editoren kunne ikke læses sikkert. Brug den manuelle opsætning; der er ikke indsat kode.'};
        state.pasted=true;const data=new DataTransfer();data.setData('text/plain',source);editor.dispatchEvent(new ClipboardEvent('paste',{clipboardData:data,bubbles:true,cancelable:true}));return next('install-save');
      }
    }
    const code=readCode(),matched=normalize(code)===normalize(source);
    if(!matched)return {error:'Koden i Excel stemmer ikke med den medfølgende Provi Tracker-version. Scriptet er ikke kørt. Brug manuel opsætning eller kontakt support.',controls:{codeMatched:false}};
    if(phase==='install-save'){
      const save=button(['Gem script','Save script']);if(enabled(save)){save.click();return null;}
      if(saved() && !dirty()){
        const control=back();if(enabled(control)){state.libraryReady=false;control.click();return next('install-reopen');}
      }
      return null;
    }
    if(phase==='install-verify'){
      if(dirty() || !saved())return {error:'Excel har ikke bekræftet, at scriptet er gemt. Forløbet er stoppet før kørsel.'};
      return {installed:true,controls:{codeMatched:true,saved:true,reopened:state.reopened===true}};
    }
  }
  return null;
}

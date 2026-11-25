// Outlook's ordinary controls only. No cookies, tokens, Graph or private page APIs.
export const outlookOrigins=['https://outlook.office.com/*','https://outlook.cloud.microsoft/*','https://outlook.office365.com/*'];
export const isOutlookOrigin=value=>outlookOrigins.some(origin=>origin===value+'/*');
const base='https://outlook.office.com/mail/';
const loginOrigins=['https://login.microsoftonline.com','https://login.microsoft.com','https://login.live.com'];
const isMailPath=path=>/^\/mail(?:\/|$)/.test(path);
const isOutlookEntry=path=>isMailPath(path) || path==='/' || /^\/owa(?:\/|$)/.test(path);
export const validEmail=value=>typeof value==='string' && value.length<=254 && /^[A-Z0-9.!#$%&'*+\/=?^_`{|}~-]+@[A-Z0-9](?:[A-Z0-9.-]*[A-Z0-9])?\.[A-Z]{2,}$/i.test(value);
export async function mailHash(mail,sender) {
  const text=[mail.recipient.toLowerCase(),sender.toLowerCase(),mail.registrationId,mail.html,mail.text].join('\n');
  const bytes=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(text));
  return Array.from(new Uint8Array(bytes)).map(n=>n.toString(16).padStart(2,'0')).join('');
}
async function tabAddress(tab) {
  let value=tab.pendingUrl || tab.url;
  if(!value)value=(await chrome.webNavigation.getAllFrames({tabId:tab.id}).catch(()=>null))?.find(frame=>frame.frameId===0)?.url;
  try{return new URL(value);}catch{return null;}
}

async function selectOutlookTab(job) {
  const candidates=(await chrome.tabs.query({url:outlookOrigins})).flatMap(tab=>{
    if(tab.incognito || !tab.url)return [];
    const url=new URL(tab.url);
    return isOutlookOrigin(url.origin) && isOutlookEntry(url.pathname)?[{tab,mail:isMailPath(url.pathname)}]:[];
  });
  candidates.sort((a,b)=>Number(b.mail)-Number(a.mail) ||
    Number(Boolean(a.tab.discarded || a.tab.frozen))-Number(Boolean(b.tab.discarded || b.tab.frozen)) ||
    Number(b.tab.active)-Number(a.tab.active) || (b.tab.lastAccessed || 0)-(a.tab.lastAccessed || 0));
  job.candidates=candidates.map(entry=>entry.tab);
  const existing=job.candidates.shift();
  if(existing){
    job.existingTab=true;
    await chrome.storage.session.set({outlookTab:existing.id});
    return existing;
  }

  const saved=(await chrome.storage.session.get('outlookTab')).outlookTab;
  if(Number.isInteger(saved))try {
    const tab=await chrome.tabs.get(saved), url=await tabAddress(tab);
    if(!tab.incognito && url && (isOutlookOrigin(url.origin) && isOutlookEntry(url.pathname) || loginOrigins.includes(url.origin)))return tab;
  }catch{}
  const origin=(await chrome.storage.local.get('outlookOrigin')).outlookOrigin;
  const tab=await chrome.tabs.create({url:isOutlookOrigin(origin) ? origin+'/mail/' : base,active:job.operation==='connect'});
  await chrome.storage.session.set({outlookTab:tab.id});return tab;
}
export function outlookStep(request) {
  if(!['https://outlook.office.com','https://outlook.cloud.microsoft','https://outlook.office365.com'].includes(location.origin) || !/^\/mail(?:\/|$)/.test(location.pathname))return null;
  const label=el=>(el.getAttribute('aria-label') || (el.getAttribute('aria-labelledby') || '').split(/\s+/).map(id=>document.getElementById(id)?.textContent || '').join(' ').trim() || el.textContent || '').replace(/[\u200b-\u200f\u202a-\u202e\u2066-\u2069\ue000-\uf8ff\ufeff]/g,'').replace(/\s+/g,' ').trim();
  const visible=el=>!el.closest('[hidden],[aria-hidden="true"]') && getComputedStyle(el).display!=='none' && getComputedStyle(el).visibility!=='hidden' && el.getBoundingClientRect().width>0;
  const editableMail=[...document.querySelectorAll('input,[contenteditable="true"]')].filter(el=>
    visible(el) && ['Emne','Subject','Meddelelsens brødtekst','Message body'].includes(label(el)));
  if(request.operation==='cleanup'){
    const state=globalThis.proviOutlookJob;
    if(state?.requestId===request.requestId){
      state.observer?.disconnect();
      state.recipientObserver?.disconnect();
      delete globalThis.proviOutlookJob;
      if(request.returnUrl && !editableMail.length && state.receiptUrl===location.href){
        const target=new URL(request.returnUrl);
        if(target.origin===location.origin && /^\/mail(?:\/|$)/.test(target.pathname) && target.href!==location.href){
          location.replace(target.href);
        }
      }
    }
    if(globalThis.proviOutlookProfileProbe?.requestId===request.requestId)delete globalThis.proviOutlookProfileProbe;
    return {cleaned:true};
  }
  const email=value=>/^[A-Z0-9.!#$%&'*+\/=?^_`{|}~-]+@[A-Z0-9](?:[A-Z0-9.-]*[A-Z0-9])?\.[A-Z]{2,}$/i.test(value);
  const addresses=elements=>[...new Set([...elements].filter(visible).flatMap(el=>
    [label(el),el.getAttribute('title') || ''].flatMap(text=>
      text.split(/[\s<>()]+/).filter(email).map(value=>value.toLowerCase()))))];
  const profile=[...document.querySelectorAll('#mectrl_main_trigger, #O365_MainLink_Me, #O365_MainLink_MePhoto, [aria-label^="Account manager"], [aria-label^="Kontoadministration"]')].find(visible);
  const accountLabels=[...document.querySelectorAll('#mectrl_currentAccount_secondary, #O365_MainLink_MeEmail')].filter(visible);
  const profileAddresses=addresses([profile,...accountLabels].filter(Boolean));
  const probe=globalThis.proviOutlookProfileProbe;
  if(!profileAddresses.length && probe?.requestId===request.requestId && probe.email && probe.profile===profile && probe.label===label(profile)){
    profileAddresses.push(probe.email);
  }
  const mailbox=document.querySelector('[role="tree"], nav[aria-label="Mail"], nav[aria-label="Post"]') || document;
  const mailboxAddresses=[...new Set([...mailbox.querySelectorAll('[role="treeitem"],[role="row"]')]
    .filter(visible).map(label).filter(email).map(value=>value.toLowerCase()))];
  if(profileAddresses.length>1 || (!profileAddresses.length && mailboxAddresses.length>1))return {waiting:'multiple-accounts'};
  if(profileAddresses.length===1 && mailboxAddresses.length && !mailboxAddresses.includes(profileAddresses[0]))return {waiting:'multiple-accounts'};
  const senderEmail=profileAddresses[0] || mailboxAddresses[0];
  if(!senderEmail){
    if(profile && profile.getAttribute('aria-expanded')!=='true' && !accountLabels.length
      && probe?.requestId!==request.requestId){
      globalThis.proviOutlookProfileProbe={requestId:request.requestId,profile,label:label(profile)};
      profile.click();
    }
    return {waiting:profile?'profile-address':'account'};
  }
  if(probe?.requestId===request.requestId && probe.profile===profile && !probe.email && profileAddresses.length===1){
    probe.email=senderEmail;
    if(profile.getAttribute('aria-expanded')==='true')profile.click();
  }
  if(request.operation==='identity')return {senderEmail,hasDraft:editableMail.length>0};
  if(senderEmail!==request.expectedSender?.toLowerCase())return {error:'Outlook er logget ind med en anden afsender. Forbind den rigtige arbejdsmail i indstillingerne.'};
  const norm=s=>String(s || '').replace(/[\u200b-\u200f\u202a-\u202e\u2066-\u2069\ufeff]/g,'').replace(/\u00a0/g,' ').replace(/\s+/g,' ').trim();
  const one=(selector,names,root=document)=>{const found=[...root.querySelectorAll(selector)].filter(el=>visible(el)&&names.includes(label(el)));return found.length===1?found[0]:null;};
  const subject=one('input',['Emne','Subject']);
  const body=one('[contenteditable="true"]',['Meddelelsens brødtekst','Message body']);
  const state=globalThis.proviOutlookJob?.requestId===request.requestId?globalThis.proviOutlookJob:(globalThis.proviOutlookJob={requestId:request.requestId});
  if(request.operation==='navigate-sent'){
    if(location.href!==request.fromUrl)return {error:'Outlook-fanen skiftede side. Prøv afsendelsen igen.'};
    if(editableMail.length)return {draftOpen:true};
    if(location.pathname!=='/mail/sentitems')location.assign(location.origin+'/mail/sentitems');
    return {navigated:true};
  }
  const focus=el=>{el.focus({preventScroll:true});el.dispatchEvent(new FocusEvent('focus',{composed:true}));el.dispatchEvent(new FocusEvent('focusin',{bubbles:true,composed:true}));};
  const enter=el=>{for(const type of ['keydown','keyup'])el.dispatchEvent(new KeyboardEvent(type,{key:'Enter',code:'Enter',keyCode:13,which:13,bubbles:true,cancelable:true,composed:true}));};
  const fill=(el,value)=>{focus(el);if(el instanceof HTMLInputElement){Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set.call(el,value);}else{const range=document.createRange();range.selectNodeContents(el);const selection=getSelection();selection.removeAllRanges();selection.addRange(range);document.execCommand('insertText',false,value);}el.dispatchEvent(new InputEvent('input',{data:value,inputType:'insertReplacementText',bubbles:true,composed:true}));el.dispatchEvent(new Event('change',{bubbles:true}));};
  if(['sent-baseline','sent-list','sent-message'].includes(request.operation)){
    if(!location.pathname.startsWith('/mail/sentitems'))return {waiting:'sent-folder'};
    const rows=[...document.querySelectorAll('[role="option"][data-convid]')].filter(visible);
    const keys=rows.map(row=>row.getAttribute('data-convid')).filter(key=>typeof key==='string' && key.length>10 && key.length<1000);
    if(!keys.length)return {waiting:'sent-list'};
    if(request.operation==='sent-baseline')return {baseline:[...new Set(keys)]};
    if(request.operation==='sent-list'){
      const candidates=rows.filter(row=>!request.baseline.includes(row.getAttribute('data-convid')) && [...row.querySelectorAll('span')].some(el=>visible(el) && norm(el.textContent)==='salgs reg'));
      if(!candidates.length)return {waiting:'sent-copy'};
      if(candidates.length!==1)return {error:'Flere nye salgs reg-beskeder vises i Sendt post. Kontrollér afsendelsen manuelt.'};
      const row=candidates[0],key=row.getAttribute('data-convid');row.click();return {opened:key};
    }
    const selected=decodeURIComponent(location.pathname.split('/id/')[1] || '');
    if(selected!==request.candidate)return {waiting:'sent-message'};
    const parsed=new DOMParser().parseFromString(request.html,'text/html');
    const expected=parsed.querySelector('table');if(!expected)return {error:'Mailudsnittet mangler.'};
    const cells=table=>[...table.rows].map(row=>[...row.cells].map(cell=>norm(cell.textContent)));
    const expectedCells=JSON.stringify(cells(expected));
    const matches=[...document.querySelectorAll('table')].filter(table=>visible(table) && !table.closest('[contenteditable="true"]') && JSON.stringify(cells(table))===expectedCells);
    if(matches.length!==1)return {waiting:'sent-table'};
    const recipients=[...document.querySelectorAll('[role="presentation"][aria-label]')].filter(el=>visible(el) && /^(?:Til|To):/.test(label(el)));
    if(recipients.length!==1)return {waiting:'sent-recipient'};
    const to=recipients[0],people=[...to.querySelectorAll('[role="button"][data-lpc-hover-target-id]')].filter(visible);
    const extras=[...document.querySelectorAll('[role="presentation"][aria-label]')].filter(el=>visible(el) && /^(?:Cc|Bcc):.+/.test(label(el)));
    if(people.length!==1 || extras.length)return {error:'Den sendte besked viser ikke præcis én modtager. Kontrollér Sendt post.'};
    if(!state.sentCardRequested){state.sentCardRequested=true;people[0].click();return null;}
    const addresses=[...new Set([...document.querySelectorAll('button,[role="button"]')].filter(visible).map(el=>label(el).match(/^(?:Mail|Email|E-mail)(?: \([^)]*\))?:\s*([^ ]+@[^ ]+)$/i)).filter(Boolean).map(match=>match[1].toLowerCase()))];
    if(!addresses.length)return {waiting:'sent-recipient-card'};
    if(addresses.length!==1 || addresses[0]!==request.recipient.toLowerCase())return {error:'Modtageren i Sendt post stemmer ikke med salgsregistreringen. Kontrollér afsendelsen manuelt.'};
    const subject=[...document.querySelectorAll('h1,h2,h3,[role="heading"]')].some(el=>visible(el) && label(el)==='salgs reg');
    if(!subject)return {waiting:'sent-subject'};
    // The copy must be new since the pre-send snapshot, with matching content
    // and an explicitly verified recipient in the same bound sender's mailbox.
    const close=one('button,[role="button"]',['Luk','Close']);if(close)close.click();
    state.receiptUrl=location.href;
    return {sent:true,senderEmail,verifiedRecipient:addresses[0],messageKey:selected};
  }
  if(request.operation==='compose'){
    if(state.created){
      if(!subject || !body || editableMail.length!==2)return null;
      state.subject=subject;
      state.body=body;
      return {composed:true};
    }
    if(editableMail.length)return {draftOpen:true};
    if(location.href!==request.fromUrl)return {error:'Outlook-fanen skiftede side. Prøv afsendelsen igen.'};
    const button=one('button,[role="button"]',['Ny mail','New mail','New message']);
    if(button && !button.disabled && !state.clickedNew){state.clickedNew=true;state.created=true;button.click();}
    return null;
  }
  if(!state.created || !subject || !body)return {waiting:'compose'};
  if(editableMail.length!==2 || state.subject!==subject || state.body!==body){
    return {error:'Outlooks skrivevindue skiftede under afsendelsen. Kontrollér mailen før genforsøg.'};
  }
  let root=subject.parentElement;while(root && root!==document.body && (!root.contains(body) || !one('input,[contenteditable="true"]',['Til','To'],root) || !one('button,[role="button"]',['Send'],root)))root=root.parentElement;
  if(!root || root===document.body)return {error:'Outlooks skrivevindue kunne ikke identificeres sikkert.'};
  const to=one('input,[contenteditable="true"]',['Til','To'],root);
  if(!to)return {waiting:'recipient'};
  const value=el=>{if(el instanceof HTMLInputElement)return el.value;const copy=el.cloneNode(true);copy.querySelectorAll('[contenteditable="false"]').forEach(chip=>chip.remove());return copy.textContent;};
  const chips=[...to.querySelectorAll('[contenteditable="false"][draggable="true"]')];
  const peopleRoot=chips.length?to:root;
  const people=[...new Set([...peopleRoot.querySelectorAll('[email],[data-email],[aria-label],[title]')].flatMap(el=>[el.getAttribute('email'),el.getAttribute('data-email'),el.getAttribute('aria-label'),el.getAttribute('title')]).filter(Boolean).flatMap(s=>s.match(/[A-Z0-9.!#$%&'*+\/=?^_`{|}~-]+@[A-Z0-9.-]+\.[A-Z]{2,}/gi) || []).map(s=>s.toLowerCase()))];
  const extra=[...root.querySelectorAll('input,[role="textbox"][contenteditable="true"]')].filter(el=>visible(el) && ['Cc','Bcc'].includes(label(el)) && (norm(value(el)) || el.querySelector('[contenteditable="false"][draggable="true"]')));
  const recipientReady=()=>!norm(value(to)) && extra.length===0 && (chips.length<=1 && people.length===1 && people[0]===request.recipient.toLowerCase() || chips.length===1 && state.recipientVerified && state.verifiedChip===chips[0] && !state.recipientChanged);
  if(request.operation==='recipient'){
    if(recipientReady())return {recipient:true};
    if(chips.length>1 || people.some(p=>p!==request.recipient.toLowerCase()) || extra.length)return {error:'Outlook viser andre modtagere end den valgte mailadresse. Mailen er ikke sendt.'};
    if(chips.length===1 && !norm(value(to))){
      if(!state.cardRequested){
        state.cardRequested=true;chips[0].dispatchEvent(new MouseEvent('dblclick',{bubbles:true,cancelable:true,view:window}));return null;
      }
      // Outlook may render a resolved recipient as a name only. Verify the
      // ordinary contact card's explicit mail address before trusting that chip.
      const entries=[...document.querySelectorAll('button,[role="button"]')].filter(visible).map(el=>({el,match:label(el).match(/^(?:Mail|Email|E-mail)(?: \([^)]*\))?:\s*([^ ]+@[^ ]+)$/i)})).filter(entry=>entry.match);
      const addresses=[...new Set(entries.map(entry=>entry.match[1].toLowerCase()))];
      if(!addresses.length)return {waiting:'recipient-card'};
      if(addresses.length!==1 || addresses[0]!==request.recipient.toLowerCase())return {error:'Outlooks kontaktkort bekræfter ikke den valgte modtager entydigt. Mailen er ikke sendt.'};
      let card=entries[0].el.parentElement;while(card && card!==document.body && !one('button,[role="button"]',['Luk','Close'],card))card=card.parentElement;
      if(!card || card===document.body || card.contains(to))return {error:'Modtagerens kontaktkort kunne ikke afgrænses sikkert. Mailen er ikke sendt.'};
      state.recipientVerified=true;state.verifiedChip=chips[0];state.recipientChanged=false;
      state.recipientObserver?.disconnect();state.recipientObserver=new MutationObserver(()=>{state.recipientChanged=true;});state.recipientObserver.observe(to,{childList:true,subtree:true,characterData:true});
      one('button,[role="button"]',['Luk','Close'],card).click();return null;
    }
    if(norm(value(to))===request.recipient){enter(to);return null;}
    if(state.recipientFilled)return {waiting:'recipient'};
    fill(to,request.recipient);state.recipientFilled=true;return null;
  }
  if(request.operation==='subject'){
    if(subject.value==='salgs reg')return {subject:true};
    fill(subject,'salgs reg');subject.blur();return null;
  }
  const parsed=new DOMParser().parseFromString(request.html,'text/html');
  const table=parsed.body.firstElementChild;
  const allowed=new Set(['TABLE','TBODY','THEAD','TR','TD','TH','BR']);
  if(!table || table.tagName!=='TABLE' || parsed.body.children.length!==1 || [...parsed.body.querySelectorAll('*')].some(el=>!allowed.has(el.tagName) || [...el.attributes].some(a=>!['style','colspan','rowspan'].includes(a.name)) || /url\s*\(|expression\s*\(|@import|javascript:|behavior\s*:/i.test(el.getAttribute('style') || '')))return {error:'Mailtabellen indeholder ugyldig formatering. Mailen er ikke sendt.'};
  const expectedRows=[...table.rows].map(r=>[...r.cells].map(c=>norm(c.textContent)));
  if(expectedRows.length!==3 || !expectedRows[0].length || !expectedRows.every(r=>r.length===expectedRows[0].length))return {error:'Mailen skal indeholde præcis de to overskriftsrækker og salget.'};
  const matches=()=>{
    const tables=[...body.querySelectorAll('table')];if(tables.length!==1)return false;
    const rows=[...tables[0].rows].map(r=>[...r.cells].map(c=>norm(c.textContent)));
    if(JSON.stringify(rows)!==JSON.stringify(expectedRows))return false;
    const outside=body.cloneNode(true);outside.querySelector('table')?.remove();return !norm(outside.textContent);
  };
  if(request.operation==='body'){
    if(matches())return {body:true};
    if((state.pastes || 0)>=3)return {error:'Outlook bevarede ikke hele mailtabellen. Mailen er ikke sendt.'};
    focus(body);const range=document.createRange();range.selectNodeContents(body);const selection=getSelection();selection.removeAllRanges();selection.addRange(range);
    const data=new DataTransfer();data.setData('text/html',table.outerHTML);data.setData('text/plain',request.text);
    if(!state.pastes)body.dispatchEvent(new ClipboardEvent('paste',{clipboardData:data,bubbles:true,cancelable:true}));
    else document.execCommand('insertHTML',false,table.outerHTML);
    body.dispatchEvent(new InputEvent('input',{inputType:'insertFromPaste',bubbles:true,composed:true}));body.blur();state.pastes=(state.pastes || 0)+1;return null;
  }
  const send=one('button,[role="button"]',['Send'],root);
  const ready=recipientReady() && subject.value==='salgs reg' && matches() && send && !send.disabled;
  if(request.operation==='ready')return ready?{ready:true}: {error:'Modtager, emne eller mailindhold ændrede sig før afsendelsen. Mailen er ikke sendt.'};
  if(request.operation==='send'){
    if(state.sendClicked)return {submitted:true};
    if(!ready)return {error:'Mailen kunne ikke godkendes før afsendelse. Kontrollér Outlook-fanen.'};
    state.sendClicked=true;state.recipientObserver?.disconnect();send.click();return {submitted:true};
  }
  return null;
}
export async function outlookStatus(){
  const sender=(await chrome.storage.local.get('outlookAccount')).outlookAccount;
  const permitted=await chrome.permissions.contains({origins:outlookOrigins});
  const bound=permitted && validEmail(sender);
  return {bound,senderEmail:bound?sender:'',permission:permitted};
}
export async function startOutlook(job,finish,current,{protectTab=async()=>{},progress=()=>{}}={}) {
  const done=result=>finish(job,{senderEmail:job.expectedSender,registrationId:job.mail?.registrationId,recipient:job.mail?.recipient,contentHash:job.contentHash,...result});
  const failed=(error,status='mail-not-sent')=>done({success:false,status:job.armed?'mail-uncertain':status,notSent:!job.armed && status!=='mail-uncertain',error});
  if(job.operation==='send'){
    const m=job.mail;
    if(!m || !validEmail(m.recipient) || !validEmail(job.expectedSender) || !/^[0-9a-f-]{36}$/i.test(m.registrationId || '') || m.subject!=='salgs reg' || typeof m.html!=='string' || m.html.length>120000 || typeof m.text!=='string' || m.text.length>40000){failed('Kontrollér mailadresse og salgsregistrering.');return;}
    job.contentHash=await mailHash(m,job.expectedSender);
    job.ledgerKey='saleMail.'+job.expectedSender.toLowerCase()+'.'+m.registrationId;
    const old=(await chrome.storage.local.get(job.ledgerKey))[job.ledgerKey];
    if(old){
      if(old.hash!==job.contentHash){failed('Denne salgsregistrering er allerede knyttet til et andet mailindhold eller en anden modtager.');return;}
      if(old.state==='sent'){done({success:true,status:'mail-already-sent'});return;}
      if(job.reviewedRetry===true)await chrome.storage.local.remove(job.ledgerKey);
      else{failed('En tidligere afsendelse mangler sikker bekræftelse. Kontrollér Sendt post i Outlook før yderligere handling.','mail-uncertain');return;}
    }
  }
  job.phase='identity';job.startedAt=Date.now();let scanning=false;
  const report=phase=>{if(job.waiting!==phase){job.waiting=phase;progress(phase);}};
  const draftMessage='Der er en mail under redigering i Outlook. Salgs reg afventer, til kladden er gemt eller lukket.';
  const tryAnotherTab=(status,error)=>{
    if(status==='mail-draft-open')job.blockedDraft=true;
    const next=job.phase==='identity' ? job.candidates.shift() : null;
    if(next){job.tabId=next.id;job.waiting=null;job.openedLogin=false;job.waking=false;return;}
    if(job.blockedDraft)failed(draftMessage,'mail-draft-open');
    else failed(error,status);
  };
  const scan=async()=>{
    if(!current() || scanning)return;scanning=true;
    try{
      if(!await chrome.permissions.contains({origins:outlookOrigins})){
        if(job.operation==='connect'){
          report('outlook-permission');
          if(!job.prompted){job.prompted=true;await chrome.tabs.create({url:chrome.runtime.getURL('popup.html#outlook'),active:true});}
          return;
        }
        failed('Forbind Outlook i indstillingerne, og tillad Outlook-adgang i Chrome-udvidelsen.','mail-permission-required');return;
      }
      if(!job.tabId){report('outlook-open');const tab=await selectOutlookTab(job);job.tabId=tab.id;}
      const tab=await chrome.tabs.get(job.tabId).catch(()=>null);
      if(!tab){failed('Outlook-fanen blev lukket. Åbn Outlook i Chrome og prøv igen.');return;}
      const url=await tabAddress(tab);
      if(loginOrigins.includes(url?.origin)){
        if(job.operation==='connect'){
          report('outlook-login');
          if(!job.openedLogin){job.openedLogin=true;await chrome.tabs.update(job.tabId,{active:true});}
          return;
        }
        failed('Log ind på Outlook i Chrome. Mailen afventer; salget er allerede registreret.','mail-login-required');return;
      }
      if(!url || url.href==='about:blank' || tab.status==='loading')return;
      if(isOutlookOrigin(url.origin) && isOutlookEntry(url.pathname) && !isMailPath(url.pathname))return;
      if(!isOutlookOrigin(url.origin) || !isMailPath(url.pathname)){tryAnotherTab('mail-not-sent','Den åbne side er ikke Outlook på nettet til arbejdsbrug. Åbn din indbakke i Chrome, og vælg Forbind Outlook igen.');return;}
      if(tab.discarded || tab.frozen){
        if(job.operation==='connect'){
          report('outlook-open');
          if(!job.waking){job.waking=true;await chrome.tabs.update(tab.id,{active:true});}
          return;
        }
        tryAnotherTab('mail-sleeping','Åbn Outlook-fanen i Chrome én gang for at genoptage mailforbindelsen.');return;
      }
      if(!job.existingTab)await protectTab(tab);
      const navigate=['sent-baseline','sent-list'].includes(job.phase) && job.sentNavigation!==job.phase;
      const args={operation:navigate?'navigate-sent':job.phase,requestId:job.requestId,expectedSender:job.expectedSender,
        recipient:job.mail?.recipient,html:job.mail?.html,text:job.mail?.text,baseline:job.baseline || [],candidate:job.candidate,
        fromUrl:navigate ? job.navigateFrom : job.composeUrl};
      let readings;
      try{
        readings=await chrome.scripting.executeScript({target:{tabId:job.tabId,frameIds:[0]},func:outlookStep,args:[args]});
      }catch(error){
        const latest=await chrome.tabs.get(job.tabId).catch(()=>null);
        if(latest && (latest.status==='loading' || (await tabAddress(latest))?.href!==url.href))return;
        throw error;
      }
      if(!current())return;
      const result=readings[0]?.result;
      if(job.operation==='connect' && result?.waiting){
        report(result.waiting==='multiple-accounts'?'outlook-account-choice':'outlook-account');
      }
      if(!result || result.waiting)return;
      if(result.error){failed(result.error);return;}
      if(result.draftOpen){
        // The editor can remain visible briefly while Outlook submits our message.
        if(job.phase==='sent-list')return;
        tryAnotherTab('mail-draft-open',draftMessage);return;
      }
      if(result.navigated){job.sentNavigation=job.phase;return;}
      if(job.phase==='identity'){
        if(job.operation==='connect'){
          if(!validEmail(result.senderEmail)){failed('Outlook-kontoen kunne ikke identificeres.');return;}
          await chrome.storage.local.set({outlookAccount:result.senderEmail,outlookOrigin:url.origin});done({success:true,status:'outlook-connected',senderEmail:result.senderEmail});return;
        }
        const account=(await chrome.storage.local.get('outlookAccount')).outlookAccount;
        if(account?.toLowerCase()!==job.expectedSender.toLowerCase()){failed('Forbind den valgte afsenderkonto i Outlook igen.');return;}
        if(result.senderEmail!==job.expectedSender.toLowerCase()){
          tryAnotherTab('mail-not-sent','Åbn den tilknyttede arbejdsmail i Outlook, og prøv igen.');return;
        }
        if(result.hasDraft){tryAnotherTab('mail-draft-open',draftMessage);return;}
        job.originalUrl=url.href;
        job.navigateFrom=url.href;
        job.phase='sent-baseline';return;
      }
      if(result.baseline){
        const snapshot=JSON.stringify(result.baseline);if(job.baselineReading!==snapshot){job.baselineReading=snapshot;return;}
        job.baseline=result.baseline;job.composeUrl=url.href;job.phase='compose';return;
      }
      if(result.opened){job.candidate=result.opened;job.phase='sent-message';return;}
      if(result.composed){job.phase='recipient';return;}
      if(result.recipient){job.phase='subject';return;}
      if(result.subject){job.phase='body';return;}
      if(result.body){job.phase='ready';return;}
      if(result.ready){await chrome.storage.local.set({[job.ledgerKey]:{state:'sending',hash:job.contentHash,at:Date.now(),baseline:job.baseline}});job.armed=true;job.phase='send';return;}
      if(result.submitted){job.navigateFrom=url.href;job.phase='sent-list';return;}
      if(result.sent){
        await chrome.storage.local.set({[job.ledgerKey]:{state:'sent',hash:job.contentHash,at:Date.now(),messageKey:result.messageKey}});
        if(job.existingTab)job.returnUrl=job.originalUrl;
        done({success:true,status:'mail-sent'});
      }
    }catch{if(current())failed('Chrome kunne ikke læse Outlook-fanen. Kontrollér udvidelsens adgang på Outlook-siden, og prøv igen.');}
    finally{scanning=false;}
  };
  job.scan=scan;job.poll=setInterval(scan,500);
  job.timeout=setTimeout(()=>failed(job.armed?'Afsendelsen kunne ikke bekræftes. Kontrollér Sendt post i Outlook.':({
    'outlook-permission':'Giv Provi Tracker-udvidelsen adgang til Outlook i Chrome, og prøv igen.',
    'outlook-login':'Log ind i Outlook-fanen med din arbejdsmail, og prøv igen.',
    'outlook-account':'Outlook er åbent, men Chrome kunne ikke læse afsenderen. Åbn kontomenuen i Outlook, og prøv igen.',
    'outlook-account-choice':'Outlook viser flere mulige konti. Åbn din egen arbejdsmail i Chrome, og prøv igen.'
  })[job.waiting] || 'Outlook svarede ikke. Åbn din Outlook-indbakke i Chrome, og prøv igen.'),job.operation==='connect'?600000:90000);
  await scan();
}

export async function cleanupOutlook(job){
  if(!job.tabId)return;
  let timeout;
  try {
    await Promise.race([
      chrome.scripting.executeScript({target:{tabId:job.tabId,frameIds:[0]},func:outlookStep,args:[{
        operation:'cleanup',requestId:job.requestId,returnUrl:job.returnUrl
      }]}),
      new Promise(resolve=>{timeout=setTimeout(resolve,2000);})
    ]);
  } catch {} finally {clearTimeout(timeout);}
}

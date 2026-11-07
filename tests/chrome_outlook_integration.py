"""Actual extension and rich editor events on a synthetic Outlook origin; never sends mail."""
import argparse,json,shutil,tempfile,time,uuid
from pathlib import Path
from playwright.sync_api import sync_playwright
REPO=Path(__file__).resolve().parents[1]
PAGE=r'''<div role="row" aria-label="&#xE488;seller@example.com&#x200E;">seller@example.com</div><button id="new">Ny mail</button><div id="mail-list"></div><div id="read"></div><div id="app"></div><div id="toasts"></div>
<script>
for(const flag of ['chipMode','tamperTo','noToast'])Object.defineProperty(window,flag,{get:()=>sessionStorage.getItem(flag)==='true',set:value=>sessionStorage.setItem(flag,String(value))});
window.sent=JSON.parse(localStorage.getItem('sent') || '[]');
function card(recipient){if(document.querySelector('#card'))return;const panel=document.createElement('aside');panel.id='card';const address=document.createElement('button');address.setAttribute('aria-label','Mail (arbejde): '+recipient);address.textContent=recipient;const close=document.createElement('button');close.textContent='Luk';close.onclick=()=>panel.remove();panel.append(address,close);document.body.append(panel)}
function renderSent(){const list=document.getElementById('mail-list');list.replaceChildren();
 const old=document.createElement('div');old.setAttribute('role','option');old.setAttribute('data-convid','fixture-existing-old');old.textContent='Earlier message';list.append(old);
 for(const item of [...sent].reverse().filter(item=>item.available)){
  const row=document.createElement('div');row.setAttribute('role','option');row.setAttribute('data-convid',item.key);row.innerHTML='<span>Recipient</span> <span>salgs reg</span>';list.prepend(row);
  row.onclick=()=>{history.replaceState(null,'','/mail/sentitems/id/'+encodeURIComponent(item.key));document.getElementById('read').innerHTML='<div role="heading">salgs reg</div><div role="presentation" aria-label="Til: Recipient"><span role="button" tabindex="0" data-lpc-hover-target-id="fixture">Recipient</span></div><div id="copy">'+item.html+'</div>';document.querySelector('[data-lpc-hover-target-id]').onclick=()=>card(item.recipient)};
 }
}
renderSent();
function compose(){
 if(document.querySelector('#compose'))return;
 const root=document.createElement('section');root.id='compose';
 root.innerHTML='<button id="send">Send</button><div id="people"></div>'+(chipMode?'<div role="textbox" contenteditable="true" aria-label="Til">\u200b</div><div role="textbox" contenteditable="true" aria-label="Cc">\u200b</div>':'<input aria-label="Til"><input aria-label="Cc">')+'<div class="lower"><input aria-label="Emne"><div role="textbox" contenteditable="true" aria-label="Meddelelsens brødtekst"><p>Personal signature</p></div></div>';
 document.getElementById('app').append(root);
 const to=root.querySelector('[aria-label=Til]'),subject=root.querySelector('[aria-label=Emne]'),body=root.querySelector('[aria-label="Meddelelsens brødtekst"]');
 let recipient='',subjectModel='',bodyModel='';const value=()=>to instanceof HTMLInputElement?to.value:to.textContent;
 to.addEventListener('input',e=>{if(document.activeElement===to && e instanceof InputEvent && e.inputType==='insertReplacementText')to.typed=value()});
 to.addEventListener('keydown',e=>{if(e.key==='Enter' && to.typed===value()){
  recipient=value();const chip=document.createElement('span');
  if(chipMode){to.replaceChildren(document.createTextNode('\u200b'));chip.contentEditable='false';chip.draggable=true;chip.setAttribute('aria-label','unknownRecipient');chip.textContent='\uF620Recipient\uE6D3';to.append(chip,document.createTextNode('\u200b'));chip.ondblclick=()=>card(recipient);
  }else{to.value='';chip.setAttribute('aria-label',recipient);chip.textContent=recipient;root.querySelector('#people').append(chip)}
 }});
 subject.addEventListener('input',e=>{if(e instanceof InputEvent && document.activeElement===subject)subjectModel=subject.value});
 body.addEventListener('paste',e=>{if(document.activeElement!==body)return;e.preventDefault();body.innerHTML=e.clipboardData.getData('text/html')});
 body.addEventListener('input',()=>{if(document.activeElement===body){bodyModel=body.innerHTML;if(tamperTo)to.querySelector('[draggable]')?.append(' changed')}});
 root.querySelector('#send').onclick=()=>{
  if(!recipient || subjectModel!=='salgs reg' || !bodyModel.includes('<table'))throw Error('Visible field was not committed to editor model');
  sent.push({recipient,subject:subjectModel,html:bodyModel,key:'fixture-conversation-'+sent.length,available:!noToast});localStorage.setItem('sent',JSON.stringify(sent));root.remove();renderSent();
  const unrelated=document.createElement('div');unrelated.role='status';unrelated.textContent='Sent';document.getElementById('toasts').append(unrelated);
 };
}
document.getElementById('new').onclick=compose;
</script>
'''

def run():
 with tempfile.TemporaryDirectory(prefix='provi-outlook-') as folder,sync_playwright() as p:
  folder=Path(folder);ext=folder/'extension';shutil.copytree(REPO/'chrome_extension',ext)
  manifest=ext/'manifest.json';m=json.loads(manifest.read_text());m['host_permissions'].extend(['https://outlook.office.com/*','https://outlook.cloud.microsoft/*','https://outlook.office365.com/*']);manifest.write_text(json.dumps(m))
  bg=ext/'background.js';bg.write_text('''const originalSetTimeout=setTimeout;globalThis.setTimeout=(fn,ms,...args)=>originalSetTimeout(fn,ms===90000?18000:ms,...args);
globalThis.fixtureResults=[];globalThis.fixtureState=()=>pending?{phase:pending.phase,tabId:pending.tabId}:null;
chrome.runtime.connectNative=()=>({postMessage:m=>{if(m.type==='result')fixtureResults.push(m)},onDisconnect:{addListener(){}},onMessage:{addListener:fn=>globalThis.fixtureNative=fn}});
'''+bg.read_text())
  context=p.chromium.launch_persistent_context(str(folder/'profile'),headless=True,channel='chromium',service_workers='block',args=['--disable-extensions-except='+str(ext),'--load-extension='+str(ext)])
  try:
   context.route('https://outlook.office.com/**',lambda r:r.fulfill(content_type='text/html; charset=utf-8',body=PAGE))
   context.route('https://outlook.cloud.microsoft/**',lambda r:r.fulfill(content_type='text/html; charset=utf-8',body=PAGE))
   service=context.service_workers[0] if context.service_workers else context.wait_for_event('serviceworker')
   front=context.new_page();front.bring_to_front();book=None
   ready_deadline=time.monotonic()+15
   while not service.evaluate('typeof fixtureNative === "function"'):
    if time.monotonic()>=ready_deadline:
     raise AssertionError('Outlook fixture native handler did not initialize')
    front.wait_for_timeout(100)
   def request(operation,mail=None,success=True):
    nonlocal book
    request_id=str(uuid.uuid4());service.evaluate('m=>fixtureNative(m)',{'type':'outlook','operation':operation,'requestId':request_id,'expectedSender':'seller@example.com',**({'mail':mail} if mail else {})})
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
     if book is None:
      matches=[page for page in context.pages if any(page.url.startswith(origin+'/mail/') for origin in ('https://outlook.office.com','https://outlook.cloud.microsoft'))]
      if matches:
       assert len(matches)==1;book=matches[0];book.route('https://outlook.office.com/**',lambda r:r.fulfill(content_type='text/html; charset=utf-8',body=PAGE));book.route('https://outlook.cloud.microsoft/**',lambda r:r.fulfill(content_type='text/html; charset=utf-8',body=PAGE));book.goto('https://outlook.cloud.microsoft/mail/') # Attach synthetic routes before the redirected fixture.
     result=service.evaluate('id=>fixtureResults.find(r=>r.requestId===id)',request_id)
     if result:
      assert result['success'] is success,result
      return result
     front.wait_for_timeout(100)
    raise AssertionError(('Outlook timed out',{'url':book.url.split('?')[0] if book else None,'job':service.evaluate('()=>fixtureState()'),'pages':[page.url.split('?')[0] for page in context.pages]}))
   connected=request('connect');assert connected['senderEmail']=='seller@example.com'
   cells=['Dato','Initialer','OSE','CVR','Firma','Telefon']+['Produkt '+str(i) for i in range(28)]
   rows=[['HA']+['']*33,cells,['19.10.2026','TEST','TEST-ONLY','00000000','A & B <SAFE>','00000000']+['2']+['']*27]
   import html
   table='<table style="border-collapse:collapse"><tbody>'+''.join('<tr>'+''.join('<td style="background-color:#92D050">'+html.escape(value)+'</td>' for value in row)+'</tr>' for row in rows)+'</tbody></table>'
   mail={'registrationId':str(uuid.uuid4()),'recipient':'receiver@example.com','subject':'salgs reg','html':table,'text':'\n'.join('\t'.join(row) for row in rows)}
   sent=request('send',mail);assert sent['status']=='mail-sent';assert book.evaluate('sent.length')==1
   assert book.evaluate('sent[0].recipient')==mail['recipient'];assert book.evaluate('sent[0].html').count('<td ')==102
   assert not book.evaluate('sent[0].html').find('Personal signature')>=0
   tab=service.evaluate('async()=> (await chrome.tabs.query({url:"https://outlook.cloud.microsoft/*"}))[0]');assert tab['active'] is False and tab['autoDiscardable'] is False
   repeat=request('send',mail);assert repeat['status']=='mail-already-sent' and book.evaluate('sent.length')==1
   wrong=request('send',{**mail,'recipient':'other@example.com'},False);assert wrong['status']=='mail-not-sent' and book.evaluate('sent.length')==1
   book.evaluate('chipMode=true');chipmail={**mail,'registrationId':str(uuid.uuid4())};chip=request('send',chipmail);assert chip['status']=='mail-sent' and book.evaluate('sent.length')==2
   book.evaluate('tamperTo=true');tampered=request('send',{**mail,'registrationId':str(uuid.uuid4())},False);assert tampered['notSent'] is True and book.evaluate('sent.length')==2
   book.evaluate('document.querySelector("#compose").remove();tamperTo=false;noToast=true;const old=document.createElement("div");old.role="status";old.textContent="Sent";document.getElementById("toasts").append(old)')
   uncertainMail={**mail,'registrationId':str(uuid.uuid4())};uncertain=request('send',uncertainMail,False);assert uncertain['status']=='mail-uncertain' and book.evaluate('sent.length')==3
   repeatUncertain=request('send',uncertainMail,False);assert repeatUncertain['status']=='mail-uncertain' and book.evaluate('sent.length')==3
   book.evaluate('noToast=false')
   book.evaluate('compose()');draft=request('send',{**mail,'registrationId':str(uuid.uuid4())},False);assert 'under redigering' in draft['error'];assert book.evaluate('sent.length')==3
   print('Actual inactive Outlook fixture passed: own-account connection, recipient commit, subject, 3x34 formatted cells, signature replacement, new server copy in Sent Items, verified recipient and full table, duplicate/content guard, personal-draft preservation and background tab.')
  finally:context.close()
if __name__=='__main__':run()

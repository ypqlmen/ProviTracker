"""Real Monaco + extension in inactive, cross-origin Office frames. No Microsoft login or sales."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import time
from playwright.sync_api import sync_playwright

REPO=Path(__file__).resolve().parents[1]
BOOK='https://5rmarketing-my.sharepoint.com/personal/fixture/doc2.aspx?sourcedoc={00000000-0000-4000-8000-000000000020}'
EXCEL='https://euc-excel.officeapps.live.com/setup-fixture'
OFFICE='https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft'
PANE=OFFICE+'/fixture/sdx/fa000000043/1/en-us_web/index.html'
DIALOG=OFFICE+'/fixture/sdx/fa000000043/1/en-us_web/dialog.html'


def run(monaco):
    source=(REPO/'chrome_extension/excel_online_sales_registration_v4.ts').read_bytes().decode('utf-8')
    name='ProviTrackerAutoV4_'+hashlib.sha256(source.encode()).hexdigest()[:10]
    page='''<script src="/vs/loader.js"></script><div id="app"></div><script>
window.scripts={ExistingPersonalScript:'// KEEP PERSONAL SCRIPT'};window.saveCount=0;window.created=0;window.runs=0;window.sales=[];window.failSave=false;window.mismatch=false;
window.require.config({paths:{vs:'/vs'}});window.require(['vs/editor/editor.main'],()=>library());
function button(label,fn){const b=document.createElement('button');b.setAttribute('aria-label',label);b.textContent=label;b.onclick=fn;return b}
function clear(){if(window.code){window.code.dispose();window.code=null}document.getElementById('app').replaceChildren()}
function library(){clear();const app=document.getElementById('app');const filter=document.createElement('input');filter.setAttribute('aria-label','Filtrer scripts');app.append(filter);
app.append(button('Nyt script',()=>{const item=document.createElement('div');item.setAttribute('role','menuitem');item.textContent='Opret i Kodeeditor';item.onclick=()=>{window.created++;edit('Script '+created,'function main(workbook: ExcelScript.Workbook) { console.log("new draft"); }')};app.append(item)}));
for(const [name,text]of Object.entries(scripts)){const article=document.createElement('article');article.setAttribute('aria-label','Script: '+name);const heading=document.createElement('span');heading.textContent=name;article.append(heading);article.append(button('Rediger',()=>edit(name,text)));app.append(article)}}
function edit(name,text){clear();window.currentName=name;const app=document.getElementById('app');app.append(button('Tilbage',library));const title=document.createElement('span');title.id='title';title.textContent=name;app.append(title);
app.append(button('Omdøb',()=>{const input=document.createElement('input');input.setAttribute('aria-label','Angiv scriptnavn [påkrævet]');input.value=currentName;input.onkeydown=e=>{if(e.key==='Enter'){window.currentName=input.value;title.textContent=input.value;input.remove()}};app.append(input)}));
const save=button('Scriptet er gemt',()=>{if(failSave)return;scripts[currentName]=code.getValue();if(mismatch)scripts[currentName]+='// tampered';window.saveCount++;save.setAttribute('aria-label','Scriptet er gemt');save.disabled=true});save.disabled=true;app.append(save);
app.append(button('Kør',()=>{window.runs++;parent.postMessage({type:'parameters',name:currentName},'*')}));const host=document.createElement('div');host.style='height:450px;width:650px';app.append(host);
window.code=monaco.editor.create(host,{value:text,language:'typescript',automaticLayout:true,accessibilitySupport:'on',ariaLabel:'editor',wordWrap:'on'});code.onDidChangeModelContent(()=>{save.disabled=false;save.setAttribute('aria-label','Gem script')});
app.insertAdjacentHTML('beforeend','<ul id="output"></ul>');}
addEventListener('message',e=>{if(e.origin!=='https://euc-excel.officeapps.live.com'||e.data.type!=='payload')return;
const p=e.data.payload;if(!p.isTest && (!p.registrationId || !p.orderNumber))throw Error('Invalid sale');if(!p.isTest && !sales.some(x=>x.registrationId===p.registrationId))sales.push(p);
const receipt={success:true,status:p.isTest?'checked':'registered',scriptVersion:4,row:p.isTest?0:125,worksheetName:p.worksheetName,worksheetId:'fixture-sheet',requestId:p.requestId,orderNumber:p.isTest?'':p.orderNumber,registrationId:p.isTest?'':p.registrationId};
const li=document.createElement('li');li.setAttribute('role','listitem');li.textContent='PROVITRACKER_RESULT:'+JSON.stringify(receipt);document.getElementById('output').append(li)});
</script>'''
    with tempfile.TemporaryDirectory(prefix='provi-setup-') as temp:
        ext=Path(temp)/'extension';shutil.copytree(REPO/'chrome_extension',ext)
        background=ext/'background.js';background.write_bytes(('''globalThis.fixtureIdentity=workbookIdentity;globalThis.fixtureResults=[];globalThis.fixtureProgress=[];
chrome.runtime.connectNative=()=>({postMessage:m=>{if(m.type==='result')fixtureResults.push(m);if(m.type==='progress')fixtureProgress.push(m.phase)},onDisconnect:{addListener(){}},onMessage:{addListener:fn=>globalThis.fixtureNative=fn}});
'''+background.read_bytes().decode('utf-8')).encode('utf-8'))
        with sync_playwright() as p:
            context=p.chromium.launch_persistent_context(str(Path(temp)/'profile'),headless=True,channel='chromium',args=['--disable-extensions-except='+str(ext),'--load-extension='+str(ext)])
            try:
                def route(req):
                    url=req.request.url
                    if url.startswith(OFFICE+'/vs/'):
                        path=monaco/url.split('/vs/',1)[1].split('?',1)[0]
                        if path.is_file():req.fulfill(path=path)
                        else:req.abort()
                    elif url.startswith(PANE):req.fulfill(content_type='text/html; charset=utf-8',body=page)
                    elif url.startswith(DIALOG):req.fulfill(content_type='text/html; charset=utf-8',body='''<div id="title"></div><label for="payload">payloadJson</label><input id="payload" type="text"><button onclick="parent.postMessage({type:'payload',payload:JSON.parse(document.getElementById('payload').value)},'https://euc-excel.officeapps.live.com')">Kør</button><script>addEventListener('message',e=>{if(e.data.name)document.getElementById('title').textContent='Angiv parametre for '+e.data.name})</script>''')
                    elif url.startswith('https://5rmarketing-my.sharepoint.com/_forms/default.aspx'):req.fulfill(content_type='text/html',body='<h1>Returning from login</h1><script>setTimeout(()=>location.assign('+json.dumps(BOOK)+'),700)</script>')
                    elif url.startswith('https://login.microsoftonline.com/'):req.fulfill(content_type='text/html',body='<h1>Fixture login</h1>')
                    elif url.startswith(EXCEL):req.fulfill(content_type='text/html; charset=utf-8',body='''<a role="tab" aria-selected="true" aria-label="Regneark Salg"><span sheet-title="Salg">Salg</span></a><button id="Automate" aria-selected="false" onmousedown="this.setAttribute('aria-selected','true')">Automatiser</button><button id="AutomateGroupViewScripts" onmousedown="if(document.getElementById('Automate').getAttribute('aria-selected')!=='true')return;const m=document.createElement('div');m.setAttribute('role','menuitem');m.textContent='Seneste scripts';m.onclick=()=>{document.getElementById('pane').src='''+json.dumps(PANE).replace(chr(34),'&quot;')+''';m.remove()};document.body.append(m);this.setAttribute('aria-expanded','true')">Få vist scripts</button><iframe id="pane"></iframe><script>addEventListener('message',e=>{if(e.origin!=='''+json.dumps(OFFICE)+''')return;if(e.data.type==='parameters'){const f=document.createElement('iframe');f.id='dialog';f.src='''+json.dumps(DIALOG)+''';f.onload=()=>f.contentWindow.postMessage({name:e.data.name},'''+json.dumps(OFFICE)+''');document.body.append(f)}if(e.data.type==='payload'){document.getElementById('pane').contentWindow.postMessage(e.data,'''+json.dumps(OFFICE)+''');document.getElementById('dialog').remove()}})</script>''')
                    elif url.startswith(BOOK):req.fulfill(content_type='text/html; charset=utf-8',body='<iframe src="'+EXCEL+'"></iframe>')
                    else:req.abort()
                context.route('https://**/*',route)
                context.set_default_timeout(10000)
                service=context.service_workers[0] if context.service_workers else context.wait_for_event('serviceworker')
                book=context.new_page();book.goto(BOOK);front=context.new_page();front.bring_to_front()
                service.evaluate('async url=>{const tabs=await chrome.tabs.query({});const t=tabs.find(t=>t.url===url);const identity=fixtureIdentity(url);await chrome.storage.session.set({["masterLaunch."+identity]:{tabId:t.id}});}',BOOK)
                book.goto('https://login.microsoftonline.com/fixture')
                def request(identity,success=True,registration=None):
                    print('Setup request:',identity,flush=True)
                    service.evaluate('m=>fixtureNative(m)',{'type':'register' if registration else 'install','requestId':identity,'workbookUrl':BOOK,**({'registration':registration} if registration else {})})
                    deadline=time.monotonic()+45
                    last=None;login_resumed=False
                    while time.monotonic()<deadline:
                        progress=service.evaluate('fixtureProgress.at(-1)')
                        if progress!=last:print('Setup phase:',progress,flush=True);last=progress
                        if identity=='setup-first' and progress=='login' and not login_resumed:
                            assert not service.evaluate('id=>fixtureResults.some(r=>r.requestId===id)',identity),'Login must not finish setup'
                            book.goto('https://5rmarketing-my.sharepoint.com/_forms/default.aspx');front.bring_to_front();login_resumed=True
                        result=service.evaluate('id=>fixtureResults.find(r=>r.requestId===id)',identity)
                        if result:
                            assert result.get('success') is success,result
                            if success: assert result.get('background') is True,result
                            return result
                        front.wait_for_timeout(100)
                    raise AssertionError(('Timed out',service.evaluate('fixtureProgress'),[(f.url,f.locator('body').inner_text()[:500]) for f in book.frames]))
                first=request('setup-first');assert first['status']=='checked' and first['row']==0
                assert 'login' in service.evaluate('fixtureProgress'),'Same setup must resume after Microsoft login'
                assert book.evaluate('async()=>{const q=await navigator.locks.query();return q.held.some(l=>l.name.startsWith("ProviTracker.background."));}'),'Master workbook must hold the background lease'
                assert service.evaluate('async()=>{const t=await chrome.tabs.query({url:"https://5rmarketing-my.sharepoint.com/*"});return t[0].autoDiscardable===false;}')
                frame=next(f for f in book.frames if f.url==PANE)
                assert frame.evaluate('scripts["'+name+'"]').replace('\r\n','\n')==source.replace('\r\n','\n')
                assert frame.evaluate('scripts.ExistingPersonalScript')=='// KEEP PERSONAL SCRIPT'
                assert frame.evaluate('created')==1 and frame.evaluate('saveCount')==1 and frame.evaluate('runs')==1
                assert {'install-write','install-save','install-reopen','install-verify','receipt'}<=set(service.evaluate('fixtureProgress'))
                assert 'PROVITRACKER_RESULT' not in json.dumps(first['diagnostics']),'Diagnostics must exclude source'
                second=request('setup-repeat');assert second['status']=='checked'
                assert frame.evaluate('created')==1 and frame.evaluate('saveCount')==1,'Repeat must reuse the saved script'
                # Identity representation is intentionally encapsulated; verify the actual stored binding.
                stored=service.evaluate('chrome.storage.local.get(null)');assert any(isinstance(v,dict) and v.get('name')==name for v in stored.values()),stored
                updated_source=source+'\n// New packaged script revision for the isolated update test.\n'
                updated_name='ProviTrackerAutoV4_'+hashlib.sha256(updated_source.encode()).hexdigest()[:10]
                service.evaluate('text=>{globalThis.fixtureFetch=fetch;globalThis.fetch=(...args)=>Promise.resolve(new Response(text,{status:200}));}',updated_source)
                sale={'requestId':'auto-sale','registrationId':'fixture-sale-uuid','orderNumber':'FIXTURE-ONLY','worksheetName':'Salg'}
                wrong=request('wrong-sheet-update',False,registration={**sale,'requestId':'wrong-sheet-update','worksheetName':'Another'});assert 'arkfanen Another' in wrong['error'];assert frame.evaluate('created')==1
                updated=request('auto-sale',registration=sale);assert updated['status']=='registered' and updated['requestId']==sale['requestId'];assert updated['registrationId']==sale['registrationId']
                assert frame.evaluate('created')==2 and frame.evaluate('saveCount')==2 and frame.evaluate('runs')==4
                assert frame.evaluate('scripts["'+name+'"]') is not None,'Older script must remain'
                assert frame.evaluate('scripts["'+updated_name+'"]').replace('\r\n','\n')==updated_source.replace('\r\n','\n')
                assert frame.evaluate('sales.length')==1,'Updating and checking must not duplicate the sale'
                assert updated['diagnostics']['scriptUpdated'] is True
                assert {'probe','install-open','install-write','install-verify','receipt'} <= {stage['phase'] for stage in updated['diagnostics']['stages']}
                assert len([e for e in updated['diagnostics']['events'] if e['event']=='script-updated'])==1
                frame.evaluate('library()')
                again=request('auto-sale-repeat',registration={**sale,'requestId':'auto-sale-repeat'})
                assert again['status']=='registered' and frame.evaluate('created')==2 and frame.evaluate('runs')==5 and frame.evaluate('sales.length')==1,'Unchanged script must not reinstall; sale identity remains stable'
                name=updated_name
                frame.evaluate('code.setValue("// unsaved personal edit")')
                unsaved=request('setup-unsaved',False);assert 'scriptændringer' in unsaved['error'];assert frame.evaluate('code.getValue()')=='// unsaved personal edit'
                frame.evaluate('library()')
                frame.evaluate('name=>{scripts[name]+="// externally modified";library()}',name)
                failure=request('setup-modified',False);assert 'Koden i Excel' in failure['error']
                assert frame.evaluate('created')==2 and frame.evaluate('saveCount')==2 and frame.evaluate('runs')==5,'Modified code must never be overwritten or run'
                print('Actual background extension + Monaco passed: create, paste, full-code readback, save, reopen, write-free receipt, progress, repeat, personal-script preservation unsaved-edit protection modified-code rejection, automatic script revision update, old-version preservation and one sale after update.')
            finally:context.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--monaco',type=Path,required=True);args=parser.parse_args();run(args.monaco)

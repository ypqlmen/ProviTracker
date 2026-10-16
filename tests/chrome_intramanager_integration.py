"""Real Chromium extension with synthetic history pages and a fake native port.
The permission grant is confined to a disposable extension/profile. No real
Intramanager requests, credentials, payroll or employee browser are accessed.
"""
from pathlib import Path
import json
import shutil
import tempfile
import time
from playwright.sync_api import sync_playwright

BASE='https://5r.intramanager.com/reports/history/'
PROFILE='Sofie Eksempel'

def check():
    repo=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='provi-intra-fixture-') as temp:
        ext=Path(temp)/'extension';shutil.copytree(repo/'chrome_extension',ext)
        manifest=json.loads((ext/'manifest.json').read_text(encoding='utf-8'))
        # Automatic grant only in the synthetic test, never in the release.
        manifest['host_permissions']+=manifest.pop('optional_host_permissions')
        (ext/'manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
        bg=ext/'background.js'
        bg.write_text('''globalThis.fixtureResults=[];
chrome.runtime.connectNative=()=>({postMessage:m=>{if(m.type==='result')fixtureResults.push(m)},onDisconnect:{addListener(){}},onMessage:{addListener:fn=>globalThis.fixtureNative=fn}});
'''+bg.read_text(encoding='utf-8'),encoding='utf-8')
        with sync_playwright() as p:
            context=p.chromium.launch_persistent_context(str(Path(temp)/'profile'),channel='chromium',headless=True,
                args=['--disable-extensions-except='+str(ext),'--load-extension='+str(ext)])
            try:
                account=[PROFILE];requests=[]
                def route(req):
                    url=req.request.url;requests.append(url)
                    from urllib.parse import urlsplit,parse_qs
                    query=parse_qs(urlsplit(url).query)
                    body='<div id="mobile-navigation"><img alt="'+account[0]+'"></div><input id="startdate" value="'+query.get('b',[''])[0]+'"><input id="stopdate" value="'+query.get('c',[''])[0]+'">'
                    body+='<main id="main"><table><thead><tr>'+''.join('<th>'+s+'</th>' for s in ['Projekt','StartStop','LøntimerRingetimer','Salg','Points','Grundløn','Bonus','Provision'])+'</tr></thead><tbody>'
                    for row in [['Telenor Erhverv','dato','7 t. 30 min.<br>6 t. 00 min.','1','1','771,83','50,00','0,00'],['Sygdom Fuldtid - 50%','dato','4 t. 00 min.','0','0','411,64','0,00','388,80'],['Annullering - Beløb','dato','0 t. 00 min.','0','0','0,00','0,00','-380,40']]:
                        body+='<tr>'+''.join('<td>'+s+'</td>' for s in row)+'</tr>'
                    req.fulfill(status=200,content_type='text/html; charset=utf-8',body=body+'</tbody></table></main>')
                context.route('https://5r.intramanager.com/**',route)
                foreground=context.pages[0];foreground.bring_to_front()
                service=context.service_workers[0] if context.service_workers else context.wait_for_event('serviceworker')
                deadline=time.monotonic()+10
                while not service.evaluate('typeof fixtureNative === "function"'):
                    assert time.monotonic()<deadline,"Extension service worker did not initialize"
                    foreground.wait_for_timeout(100)
                book=context.new_page();book.goto(BASE);foreground.bring_to_front()
                service.evaluate('async()=>{const t=await chrome.tabs.query({url:"https://5r.intramanager.com/*"});await chrome.storage.session.set({intramanagerTab:t[0].id});}')
                def run(message):
                    service.evaluate('m=>fixtureNative(m)',message)
                    deadline=time.monotonic()+50
                    while time.monotonic()<deadline:
                        results=service.evaluate('fixtureResults')
                        answer=next((r for r in results if r.get('requestId')==message['requestId']),None)
                        if answer:return answer
                        foreground.wait_for_timeout(100)
                    raise AssertionError('Intramanager fixture timed out')
                result=run({'type':'intramanager','operation':'profile','requestId':'profile'})
                assert result.get('success') and result.get('profileName')==PROFILE,result
                result=run({'type':'intramanager','operation':'hours','requestId':'payroll','expectedProfile':PROFILE,'fromDate':'21-09-2026','toDate':'20-10-2026'})
                assert result.get('success'),result
                for key,value in {'hours':7.5,'phoneHours':6,'sickHours':4,'sickPay':800.44,'cancellations':-380.40,'payrollSchema':2}.items():assert result[key]==value,(key,result)
                book=next(page for page in context.pages if page.url.startswith(BASE))
                account[0]='Anden Konto';book.reload();before=len(requests)
                result=run({'type':'intramanager','operation':'hours','requestId':'wrong','expectedProfile':PROFILE,'fromDate':'21-08-2026','toDate':'20-09-2026'})
                assert not result.get('success') and 'anden Intramanager-konto' in result.get('error',''),result
                assert len(requests)==before,'Wrong account must stop before another query'
                assert len([page for page in context.pages if page.url.startswith(BASE)])==1
                assert service.evaluate('(async()=>{const t=await chrome.tabs.query({active:true});return t.every(x=>!x.url?.startsWith("https://5r.intramanager.com"));})()')
                print('Actual background Chromium: profile binding, normal history navigation, full sick pay, negative cancellation and wrong-account stop passed.')
            finally:context.close()

if __name__=='__main__':check()

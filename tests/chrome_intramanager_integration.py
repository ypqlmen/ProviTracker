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
                args=['--disable-extensions-except='+str(ext),'--load-extension='+str(ext),'--host-resolver-rules=MAP 5r.intramanager.com 127.0.0.1'])
            try:
                # Reproduce the installation guide in this disposable profile.
                # Command-line loading alone disables unpacked extensions on reload.
                setup=context.new_page();setup.goto('chrome://extensions/')
                setup.evaluate('async()=>await new Promise(r=>chrome.developerPrivate.updateProfileConfiguration({inDeveloperMode:true},r))')
                setup.close()
                account=[PROFILE];requests=[]
                def route(req):
                    url=req.request.url;requests.append(url)
                    table='<table><thead><tr>'+''.join('<th>'+s+'</th>' for s in ['Projekt','StartStop','LøntimerRingetimer','Salg','Points','Grundløn','Bonus','Provision'])+'</tr></thead><tbody>'
                    for row in [['Telenor Erhverv','dato','7 t. 30 min.<br>6 t. 00 min.','1','1','771,83','50,00','0,00'],['Sygdom Fuldtid - 50%','dato','4 t. 00 min.','0','0','411,64','0,00','388,80'],['Annullering - Beløb','dato','0 t. 00 min.','0','0','0,00','0,00','-380,40']]:
                        table+='<tr>'+''.join('<td>'+v+'</td>' for v in row)+'</tr>'
                    table+='</tbody></table>'
                    # Like the live page: URL date arguments are ignored. Søg must submit
                    # the normal form; stale previous results stay visible during loading.
                    body='<div id="mobile-navigation"><img alt="'+account[0]+'"></div><form id="form-search"><input id="startdate" value="01-10-2026"><input id="stopdate" value="18-10-2026"><input type="checkbox" name="g[]" checked><button type="submit">Søg</button></form><main id="main">'+table.replace('7 t. 30 min.','99 t. 00 min.')+'</main>'
                    body+='<script>window.submits=0;document.getElementById("form-search").onsubmit=e=>{e.preventDefault();window.submits++;setTimeout(()=>{document.getElementById("main").innerHTML='+json.dumps(table)+'},1200)};</script>'
                    req.fulfill(status=200,content_type='text/html; charset=utf-8',body=body)
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
                assert book.locator('#startdate').input_value()=='21-09-2026'
                assert book.locator('#stopdate').input_value()=='20-10-2026'
                assert book.evaluate('submits')==1
                assert not book.locator('input[type=checkbox]').is_checked(),'Query must include all projects'
                assert service.evaluate('async()=>{const t=await chrome.tabs.query({url:"https://5r.intramanager.com/*"});return t[0].autoDiscardable===false;}')
                held=book.evaluate('async()=>{const q=await navigator.locks.query();return q.held.some(l=>l.name.startsWith("ProviTracker.background."));}')
                assert held,'Inactive payroll tab must hold the background lease'
                popup=context.new_page();popup.goto('chrome-extension://'+service.url.split('/')[2]+'/popup.html')
                popup.get_by_role('button',name='Intramanager forbundet',exact=True).wait_for()
                assert PROFILE in popup.locator('#intramanager-status').inner_text()
                popup.close();foreground.bring_to_front()
                account[0]='Anden Konto';book.reload();before=len(requests)
                result=run({'type':'intramanager','operation':'hours','requestId':'wrong','expectedProfile':PROFILE,'fromDate':'21-08-2026','toDate':'20-09-2026'})
                assert not result.get('success') and 'anden Intramanager-konto' in result.get('error',''),result
                assert len(requests)==before,'Wrong account must stop before another query'
                popup=context.new_page();popup.goto('chrome-extension://'+service.url.split('/')[2]+'/popup.html')
                popup.locator('#intramanager-status').filter(has_text='anden Intramanager-konto').wait_for()
                assert popup.get_by_role('button',name='Åbn Intramanager',exact=True).is_visible()
                popup.close();foreground.bring_to_front()
                assert len([page for page in context.pages if page.url.startswith(BASE)])==1
                assert service.evaluate('(async()=>{const t=await chrome.tabs.query({active:true});return t.every(x=>!x.url?.startsWith("https://5r.intramanager.com"));})()')
                print('Intramanager: payroll and wrong-account checks passed',flush=True)
                # Lost session tab ID must rediscover the existing tab, even with
                # a stale ID that now belongs to an unrelated foreground page.
                account[0]=PROFILE;book.reload()
                service.evaluate('async()=>{await chrome.storage.session.clear();const t=await chrome.tabs.query({active:true});await chrome.storage.session.set({intramanagerTab:t[0].id});}')
                popup=context.new_page();popup.goto('chrome-extension://'+service.url.split('/')[2]+'/popup.html')
                popup.get_by_role('button',name='Intramanager forbundet',exact=True).wait_for()
                assert PROFILE in popup.locator('#intramanager-status').inner_text()
                assert len([page for page in context.pages if page.url.startswith(BASE)])==1
                popup.close();foreground.bring_to_front()
                print('Intramanager: stale session ID recovered',flush=True)
                # Closing the payroll tab is not disconnecting the confirmed account.
                book.close()
                popup=context.new_page();popup.goto('chrome-extension://'+service.url.split('/')[2]+'/popup.html')
                deadline=time.monotonic()+10
                while not any(page.url.startswith(BASE) or page.url.startswith('chrome-error:') for page in context.pages):
                    assert time.monotonic()<deadline
                    popup.wait_for_timeout(100)
                book=next(page for page in context.pages if page.url.startswith(BASE) or page.url.startswith('chrome-error:'))
                # Chromium can start an extension-created tab before Playwright
                # attaches its fixture route. Network is blocked by the resolver;
                # navigate the same tab through the isolated route before reading.
                book.goto(BASE)
                popup.get_by_role('button',name='Intramanager forbundet',exact=True).wait_for()
                assert PROFILE in popup.locator('#intramanager-status').inner_text()
                popup.close();foreground.bring_to_front()
                print('Intramanager: closed tab recovered',flush=True)
                # An extension reload keeps local account/permissions, and must
                # re-find/protect the tab without repeating the app's bind flow.
                extension_id=service.url.split('/')[2]
                with context.expect_event('serviceworker') as resumed:
                    service.evaluate('()=>chrome.runtime.reload()')
                    popup=context.new_page();popup.goto('chrome-extension://'+extension_id+'/popup.html')
                service=resumed.value
                deadline=time.monotonic()+10
                while not service.evaluate('typeof fixtureNative === "function"'):
                    assert time.monotonic()<deadline
                    foreground.wait_for_timeout(100)
                popup.get_by_role('button',name='Intramanager forbundet',exact=True).wait_for()
                assert service.evaluate('async()=>{const s=await chrome.storage.local.get("intramanagerAccount");return s.intramanagerAccount;}')==PROFILE
                popup.close()
                print('Intramanager: extension reload recovered',flush=True)
                # An expired website login must keep the binding but cannot be
                # advertised as a verified, currently connected account.
                book.goto('https://5r.intramanager.com/login/index/')
                popup=context.new_page();popup.goto('chrome-extension://'+service.url.split('/')[2]+'/popup.html')
                popup.locator('#intramanager-status').filter(has_text='Kontotilknytningen er gemt. Log ind').wait_for()
                assert popup.get_by_role('button',name='Åbn Intramanager',exact=True).is_visible()
                assert not popup.get_by_role('button',name='Intramanager forbundet',exact=True).count()
                book.goto(BASE)
                popup.get_by_role('button',name='Intramanager forbundet',exact=True).wait_for()
                popup.close()
                print('Intramanager: login return recovered',flush=True)
                # A real browser shutdown clears storage.session. Reopen the
                # same disposable profile; retain local binding and host access.
                extension_id=service.url.split('/')[2]
                context.close()
                context=p.chromium.launch_persistent_context(str(Path(temp)/'profile'),channel='chromium',headless=True,
                    args=['--disable-extensions-except='+str(ext),'--load-extension='+str(ext),'--host-resolver-rules=MAP 5r.intramanager.com 127.0.0.1'])
                context.route('https://5r.intramanager.com/**',route)
                service=context.service_workers[0] if context.service_workers else context.wait_for_event('serviceworker')
                assert service.evaluate('async()=>{const s=await chrome.storage.local.get("intramanagerAccount");return s.intramanagerAccount;}')==PROFILE
                assert service.evaluate('chrome.permissions.contains({origins:["https://5r.intramanager.com/*"]})')
                # The restored connection must create/re-find its tab without
                # opening the popup or re-running the app's account-binding flow.
                deadline=time.monotonic()+10
                while not any(page.url.startswith(BASE) or page.url.startswith('chrome-error:') for page in context.pages):
                    assert time.monotonic()<deadline,'Startup did not restore the payroll tab'
                    context.pages[0].wait_for_timeout(100)
                book=next(page for page in context.pages if page.url.startswith(BASE) or page.url.startswith('chrome-error:'))
                book.goto(BASE) # Attach the isolated route as in closed-tab recovery above.
                popup=context.new_page();popup.goto('chrome-extension://'+extension_id+'/popup.html')
                popup.get_by_role('button',name='Intramanager forbundet',exact=True).wait_for(timeout=15000)
                assert PROFILE in popup.locator('#intramanager-status').inner_text()
                assert service.evaluate('async()=>{const t=await chrome.tabs.query({url:"https://5r.intramanager.com/*"});return t.length===1 && t[0].autoDiscardable===false;}')
                print('Actual Chromium: fresh payroll, full sick pay, signed cancellations, wrong-account stop, binding after cleared session/stale tab ID, closed-tab recovery, extension reload, real browser restart, expired login and login return passed.')
            finally:context.close()

if __name__=='__main__':check()

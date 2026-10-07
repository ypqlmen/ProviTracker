"""Synthetic workbook fixture: exact permitted hosts, no login or company data."""
import json
import re
import subprocess
import time

WORKBOOK_URL = 'https://5rmarketing-my.sharepoint.com/personal/fixture/_layouts/15/doc2.aspx?sourcedoc={00000000-0000-4000-8000-000000000001}'
OFFICE_ORIGIN = 'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft'
OFFICE_INDEX = OFFICE_ORIGIN + '/fixture/sdx/fa000000043/1/en-us_web/index.html'
OFFICE_DIALOG = OFFICE_ORIGIN + '/fixture/sdx/fa000000043/1/en-us_web/dialog.html'

EXCEL_URL = 'https://euc-excel.officeapps.live.com/x/_layouts/xlviewerinternal.aspx?provi-fixture=1'


def open_workbook(context):
    def document(route):
        if route.request.url.startswith(EXCEL_URL):
            # Matches the live Excel sheet control, including its authoritative title.
            body = '<a role="tab" aria-label="Regneark Ark1" aria-selected="true"><span sheet-title="Ark1">Ark1</span></a>'
            body += '<button id="Automate" aria-selected="false" onclick="this.setAttribute(\'aria-selected\',\'true\')">Automatiser</button>'
            body += '<button id="AutomateGroupViewScripts" aria-expanded="false" onclick="this.setAttribute(\'aria-expanded\',\'true\');document.getElementById(\'recent\').hidden=false">Få vist scripts</button>'
            body += '<button id="recent" hidden role="menuitem" onclick="this.hidden=true;document.getElementById(\'pane\').src='+repr(OFFICE_INDEX)+'">Seneste scripts</button><iframe id="pane"></iframe>'
            body += '<script>addEventListener("message",e=>{if(e.origin!=='+json.dumps(OFFICE_ORIGIN)+')return;if(e.data.type==="parameters"){const f=document.createElement("iframe");f.id="dialog";f.src='+json.dumps(OFFICE_DIALOG)+';document.body.append(f)}if(e.data.type==="payload"){document.getElementById("pane").contentWindow.postMessage(e.data,'+json.dumps(OFFICE_ORIGIN)+');document.getElementById("dialog").remove()}})</script>'
        elif route.request.url.startswith(OFFICE_INDEX):
            # Transport fixture only: V3 workbook logic is tested independently in Node.
            body = '''<div><span>ProviTrackerSalesRegistrationV3</span><button id="edit" onclick="document.getElementById('editor').hidden=false;document.getElementById('run').hidden=false;this.hidden=true">Rediger</button></div>
<textarea id="editor" aria-label="editor" hidden></textarea><button id="run" hidden onclick="parent.postMessage({type:'parameters'},'https://euc-excel.officeapps.live.com')">Kør</button><ul id="output"></ul>
<script>window.sales=[];window.preview=null;window.months=[];addEventListener('message',e=>{if(e.origin!=='https://euc-excel.officeapps.live.com'||e.data.type!=='payload')return;const p=e.data.payload;let row=0,status='checked';
if(!p.isTest){const old=sales.find(x=>x.registrationId===p.registrationId);status=old?'already_registered':'registered';if(old)row=old.row;else{const month=p.date.slice(3);if(months.at(-1)!==month)months.push(month);row=122+sales.length+months.length;preview=p;sales.push({...p,row})}}
const r={success:true,status,scriptVersion:3,row,orderNumber:p.isTest?'':p.orderNumber,registrationId:p.isTest?'':p.registrationId,requestId:p.requestId};const li=document.createElement('li');li.setAttribute('role','listitem');li.textContent='Oplysninger: PROVITRACKER_RESULT:'+JSON.stringify(r);document.getElementById('output').append(li)})</script>'''
        elif route.request.url.startswith(OFFICE_DIALOG):
            body = '''<div>Angiv parametre for ProviTrackerSalesRegistrationV3</div><input type="text" placeholder="Angiv en streng (f.eks. abcd)" oninput="window.payload=this.value;Promise.resolve().then(()=>document.getElementById('submit').disabled=false)"><button id="submit" disabled onclick="parent.postMessage({type:'payload',payload:JSON.parse(window.payload)},'https://euc-excel.officeapps.live.com')">Kør</button>'''
        else:
            # SharePoint starts with an empty source and loads Excel asynchronously.
            body = '<iframe id="WacFrame_Excel_0" src=""></iframe><script>setTimeout(()=>document.querySelector("iframe").src='+json.dumps(EXCEL_URL)+',500)</script>'
        route.fulfill(status=200, content_type='text/html; charset=utf-8', body=body)
    context.route(re.compile(re.escape(WORKBOOK_URL)), document)
    context.route(re.compile(re.escape(EXCEL_URL)), document)
    context.route(OFFICE_ORIGIN+'/**', document)
    workbook = context.new_page()
    workbook.goto(WORKBOOK_URL)
    workbook.frame_locator('#WacFrame_Excel_0').get_by_role('tab', name='Regneark Ark1', exact=True).wait_for()
    assert workbook.evaluate('document.querySelector("iframe").contentDocument === null'), 'Fixture must be cross-origin'
    workbook.evaluate('window.proviUntouched = "keep-workbook"')
    return workbook


def check_native_workbook(context, worker, extension_id):
    workbook = open_workbook(context)
    foreground = context.new_page()
    foreground.bring_to_front()
    try:
        # Reload while Excel is already open: content listeners from the prior
        # extension instance must not be required for the next successful check.
        service = next(w for w in context.service_workers if w.url.startswith('chrome-extension://'+extension_id+'/'))
        try:
            service.evaluate('chrome.runtime.reload()')
        except Exception:
            pass  # The evaluation context can disappear during the requested reload.
        foreground.wait_for_timeout(2000)
        process = subprocess.Popen([str(worker), '--stdin-json'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            process.stdin.write(json.dumps({'action':'chrome-probe', 'workbookUrl':WORKBOOK_URL}))
            process.stdin.close()
            deadline = time.monotonic()+45
            while process.poll() is None and time.monotonic() < deadline:
                foreground.wait_for_timeout(100)
            if process.poll() is None:
                raise AssertionError('Native workbook probe exceeded its bounded deadline')
            output, errors = process.stdout.read(), process.stderr.read()
            assert process.returncode == 0, errors
            result = json.loads(output)
            assert result.get('success') and result.get('status') == 'chrome-connected', result
            assert result.get('background') is True, result
            assert workbook.evaluate('window.proviUntouched') == 'keep-workbook', 'Probe must preserve the workbook'
            check_native_sales(context, worker, workbook, foreground)
            print('Actual Chromium/native-host workbook probe passed: cross-origin Ark1, background tab, extension reload')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdout.close()
            process.stderr.close()
    finally:
        foreground.close()
        workbook.close()
        context.unroute(re.compile(re.escape(WORKBOOK_URL)))
        context.unroute(re.compile(re.escape(EXCEL_URL)))
        context.unroute(OFFICE_ORIGIN+'/**')


def check_native_sales(context, worker, workbook, foreground):
    def run(action, registration, expected_success=True):
        process=subprocess.Popen([str(worker),'--stdin-json'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        try:
            process.stdin.write(json.dumps({'action':action,'workbookUrl':WORKBOOK_URL,'registration':registration},ensure_ascii=False))
            process.stdin.close()
            deadline=time.monotonic()+190
            while process.poll() is None and time.monotonic()<deadline:foreground.wait_for_timeout(100)
            assert process.poll() is not None,'Native sale timed out'
            output,errors=process.stdout.read(),process.stderr.read()
            assert process.returncode==0,errors
            result=json.loads(output)
            assert result.get('success') is expected_success,result
            if expected_success: assert result.get('background') is True,result
            assert result.get('requestId')==registration['requestId'],result
            return result
        finally:
            if process.poll() is None:process.kill();process.wait()
            process.stdout.close();process.stderr.close()
    setup=run('chrome-setup',{'requestId':'00000000-0000-4000-8000-000000000101'})
    assert setup['status']=='checked' and setup['row']==0
    pane=next(f for f in workbook.frames if f.url.startswith(OFFICE_INDEX))
    assert pane.evaluate('sales.length')==0,'Setup must not add a sale'
    sale={'registrationId':'00000000-0000-4000-8000-000000000102','requestId':'00000000-0000-4000-8000-000000000103',
        'date':'07.10.2026','orderNumber':'TEST-OSE','companyName':'Synthetic ÆØÅ','note':'Synthetic only','sellerInitials':'TEST','cvrNumber':'00000000','phoneNumber':'00000000','items':[{'key':'mobil_25gb_36','quantity':2}]}
    first=run('chrome-register',sale)
    assert first['status']=='registered' and first['registrationId']==sale['registrationId']
    assert pane.evaluate('sales.length')==1
    assert pane.evaluate('preview.companyName')==sale['companyName'],'Complete input events must reach the Office pane'
    retry=run('chrome-register',{**sale,'requestId':'00000000-0000-4000-8000-000000000104'})
    assert retry['status']=='already_registered' and retry['row']==first['row']
    assert pane.evaluate('sales.length')==1
    assert workbook.evaluate('window.proviUntouched')=='keep-workbook'
    service=next(w for w in context.service_workers if w.url.startswith('chrome-extension://'))
    tab_id=service.evaluate('async()=> (await chrome.tabs.query({url:"https://5rmarketing-my.sharepoint.com/*"}))[0].id')
    assert service.evaluate('id=>chrome.tabs.get(id).then(t=>t.autoDiscardable)',tab_id) is False
    # Explicit discarding overrides Memory Saver protection. Detection must not wake it.
    await_discard=service.evaluate('id=>chrome.tabs.discard(id)',tab_id)
    assert await_discard['discarded'] is True
    waiting=run('chrome-setup',{'requestId':'00000000-0000-4000-8000-000000000107'},False)
    assert waiting['status']=='chrome-sleeping',waiting
    assert service.evaluate('id=>chrome.tabs.get(id).then(t=>t.active)',tab_id) is False
    print('Actual browser lifecycle passed: protected background tab, forced discard, typed wait without activation')
    print('Actual Chromium/native host + Office controls transport passed: gallery, setup, parameters, fresh receipt, retry, background tab')

"""Synthetic workbook fixture: exact permitted hosts, no login or company data."""
import json
import re
import subprocess
import time

WORKBOOK_URL = 'https://5rmarketing-my.sharepoint.com/personal/fixture/_layouts/15/doc2.aspx?sourcedoc={00000000-0000-4000-8000-000000000001}'
EXCEL_URL = 'https://euc-excel.officeapps.live.com/x/_layouts/xlviewerinternal.aspx?provi-fixture=1'


def open_workbook(context):
    def document(route):
        if route.request.url.startswith(EXCEL_URL):
            # Matches the live Excel sheet control, including its authoritative title.
            body = '<a role="tab" aria-label="Regneark Ark1" aria-selected="true"><span sheet-title="Ark1">Ark1</span></a>'
        else:
            # SharePoint starts with an empty source and loads Excel asynchronously.
            body = '<iframe id="WacFrame_Excel_0" src=""></iframe><script>setTimeout(()=>document.querySelector("iframe").src='+json.dumps(EXCEL_URL)+',500)</script>'
        route.fulfill(status=200, content_type='text/html', body=body)
    context.route(re.compile(re.escape(WORKBOOK_URL)), document)
    context.route(re.compile(re.escape(EXCEL_URL)), document)
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

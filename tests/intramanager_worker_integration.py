"""Exercise the app's Intramanager worker on synthetic pages in its own Chromium."""
import html
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from playwright.sync_api import sync_playwright

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'intramanager_worker'))
import intramanager_sync as worker


def run():
    rows = [
        ['Telenor Erhverv', '19-10-2026 08:00\n19-10-2026 15:30', '7 t. 30 min.\n6 t. 00 min.', '1', '1', '771,83', '50,00', '0,00'],
        ['Sygdom Fuldtid - 50%', '20.10.2026', '4 t. 00 min.', '0', '0', '411,64', '0,00', '388,80'],
        ['Annullering - Beløb', '20.10.2026', '0 t. 00 min.', '0', '0', '0,00', '0,00', '-380,40'],
    ]
    table = '<table><tbody>' + ''.join(
        '<tr>' + ''.join('<td>' + html.escape(cell).replace('\n', '<br>') + '</td>' for cell in row) + '</tr>'
        for row in rows
    ) + '</tbody></table>'
    history = '''<meta charset="utf-8"><form action="/reports/history/result">
        <input id="startdate" name="start"><input id="stopdate" name="end">
        <button type="submit">Søg</button></form><main id="main">Testkonto</main>'''
    punched_in = False
    clicks = 0
    navigations = []

    def handle(route):
        nonlocal punched_in, clicks
        url = route.request.url
        navigations.append(url)
        if url == worker.HISTORY_URL:
            route.fulfill(content_type='text/html; charset=utf-8', body=history)
        elif url.startswith(worker.HISTORY_URL + 'result?'):
            route.fulfill(content_type='text/html; charset=utf-8', body='<main id="main">' + table + '</main>')
        elif url == worker.PUNCH_URL:
            button_label = 'Stempel ud' if punched_in else 'Stempel ind'
            route.fulfill(content_type='text/html; charset=utf-8', body=
                          f'<main id="main"><form action="/reports/punch-in/change"><button>{button_label}</button></form></main>')
        elif url.split('?', 1)[0] == worker.PUNCH_URL + 'change':
            punched_in = not punched_in
            clicks += 1
            route.fulfill(content_type='text/html; charset=utf-8', body='<main id="main">Stempling opdateret</main>')
        else:
            route.abort()

    with tempfile.TemporaryDirectory(prefix='provi-intramanager-worker-') as folder, sync_playwright() as p:
        state = Path(folder) / 'session.json'

        def launch(**options):
            browser = p.chromium.launch(**options)

            def new_context(**args):
                context = browser.new_context(**args)
                context.route('**/*', handle)
                return context

            return SimpleNamespace(new_context=new_context, close=browser.close)

        class PlaywrightContext:
            def __enter__(self):
                return SimpleNamespace(chromium=SimpleNamespace(launch=launch))

            def __exit__(self, *exc):
                return False

        def app_call(action, **details):
            payload = {'action': action, 'username': 'fixture', 'password': 'synthetic',
                       'fromDate': '01-10-2026', 'toDate': '20-10-2026', 'onDate': '20-10-2026',
                       'sessionState': str(state), **details}
            stdout = io.BytesIO()
            with patch.object(worker, 'sync_playwright', PlaywrightContext), \
                 patch.object(sys, 'argv', ['intramanager_sync.exe', '--stdin-json']), \
                 patch.object(sys, 'stdin', io.StringIO(json.dumps(payload))), \
                 patch.object(sys, 'stdout', SimpleNamespace(buffer=stdout, flush=stdout.flush)):
                worker.main()
            result = json.loads(stdout.getvalue())
            assert result['success'], result
            return result

        overview = app_call('overview')
        assert overview['hoursSuccess'] and overview['punchSuccess'] and overview['statusKnown']
        assert overview['hours'] == 7.5 and overview['phoneHours'] == 6
        assert overview['sickPay'] == 800.44 and overview['cancellations'] == -380.40
        assert overview['dailyHours'] == {
            '2026-10-19': {'hours': 7.5, 'absenceHours': 0.0},
            '2026-10-20': {'hours': 0.0, 'absenceHours': 4.0},
        }
        assert not overview['clockedIn'] and clicks == 0 and state.is_file()
        assert navigations.count(worker.HISTORY_URL + 'result?start=01-10-2026&end=20-10-2026') == 1
        assert app_call('punch-toggle', targetAction='in')['clockedIn'] and clicks == 1
        assert app_call('punch-toggle', targetAction='in')['clockedIn'] and clicks == 1

    print('Intramanager worker: requested pay period, paid and phone hours, full sick pay, signed cancellations, punch status, punch action and idempotent retry passed.')


if __name__ == '__main__':
    run()

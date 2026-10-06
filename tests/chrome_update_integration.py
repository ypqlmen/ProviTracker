"""Windows CI: exercise actual unpacked-extension reload through the installed native host.

Uses an isolated Chromium profile and blank tab. Never opens a company workbook or signs in.
Only the disposable installed test copy of the worker bundle is changed, not release artifacts.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import winreg
from playwright.sync_api import sync_playwright

def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return {}

def wait_for(context, predicate, description):
    deadline = time.monotonic()+90
    while time.monotonic() < deadline:
        if predicate():
            return
        context.pages[0].wait_for_timeout(250)
    workers = [w.url for w in context.service_workers]
    raise AssertionError(f'Timed out: {description}; service workers={workers}')

def main(worker, installer):
    root = Path(os.environ['LOCALAPPDATA']) / 'ProviTrackerChromeBridge'
    extension = root / 'extension'
    bundle_manifest = worker.parent / '_internal' / 'chrome_extension' / 'manifest.json'
    original = bundle_manifest.read_bytes()
    manifest = json.loads(original)
    initial = manifest['version']
    parts = [int(p) for p in initial.split('.')]
    parts[-1] += 1
    updated = '.'.join(str(p) for p in parts)
    digest = hashlib.sha256(base64.b64decode(manifest['key'])).hexdigest()[:32]
    extension_id = ''.join(chr(ord('a')+int(c,16)) for c in digest)
    result = subprocess.run([str(worker), '--stdin-json'], input='{"action":"chrome-install"}',
        text=True, capture_output=True, timeout=30, check=True)
    assert json.loads(result.stdout)['success'], result.stdout
    # Playwright's isolated Chromium build uses Chromium's registry namespace.
    registry_path = 'Software\\Chromium\\NativeMessagingHosts\\dk.provitracker.masterark'
    previous = None
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry_path) as key:
            previous = winreg.QueryValueEx(key, '')[0]
    except FileNotFoundError:
        pass
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, registry_path) as key:
        winreg.SetValueEx(key, '', 0, winreg.REG_SZ, str(root / 'host.json'))
    (root / 'loaded.json').unlink(missing_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix='provi-chrome-update-') as profile, sync_playwright() as p:
            context = p.chromium.launch_persistent_context(profile, channel='chromium', headless=True,
                args=[f'--disable-extensions-except={extension}', f'--load-extension={extension}'])
            try:
                page = context.pages[0]
                page.evaluate('window.proviUntouched = "keep-me"')
                wait_for(context, lambda: read_json(root / 'loaded.json').get('version') == initial,
                    'initial native-host connection')
                assert any(w.url.startswith('chrome-extension://'+extension_id+'/') for w in context.service_workers)
                # Exercise app replacement while Chrome holds the packaged native host open.
                log = root / 'integration-installer.log'
                result = subprocess.run([str(installer), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                    '/CLOSEAPPLICATIONS', '/FORCECLOSEAPPLICATIONS', f'/LOG={log}'], timeout=180)
                assert result.returncode == 0, log.read_text(encoding='utf-8', errors='replace')[-5000:]
                (root / 'loaded.json').unlink(missing_ok=True)
                wait_for(context, lambda: read_json(root / 'loaded.json').get('version') == initial,
                    'native-host reconnection after silent app installation')
                assert page.evaluate('window.proviUntouched') == 'keep-me', 'Installation must keep Chrome tabs open'
                # An in-flight check must postpone the copy and reload until it finishes.
                (root / 'request.json').write_text(json.dumps({'expiresAt':time.time()+120}), encoding='utf-8')
                manifest['version'] = updated
                bundle_manifest.write_text(json.dumps(manifest), encoding='utf-8')
                result = subprocess.run([str(worker), '--stdin-json'], input='{"action":"chrome-update"}',
                    text=True, capture_output=True, timeout=30, check=True)
                assert json.loads(result.stdout)['status'] == 'chrome-update-deferred'
                page.wait_for_timeout(4000)
                assert read_json(extension / 'manifest.json')['version'] == initial
                assert read_json(root / 'loaded.json')['version'] == initial
                (root / 'request.json').unlink()
                # The still-connected host sees the newly bundled version on its next poll.
                wait_for(context, lambda: read_json(root / 'loaded.json').get('version') == updated,
                    'automatic extension reload and native-host reconnection')
                workers = [w for w in context.service_workers if w.url.startswith('chrome-extension://'+extension_id+'/')]
                assert workers and workers[-1].evaluate('chrome.runtime.getManifest().version') == updated
                assert page.evaluate('window.proviUntouched') == 'keep-me', 'A workbook tab must not be reloaded'
                assert context.pages == [page], 'Extension update must not open or activate extra tabs'
                print(f'Actual Chromium/native-host automatic update passed: {initial} -> {updated}; open tab preserved')
            finally:
                context.close()
    finally:
        bundle_manifest.write_bytes(original)
        (root / 'request.json').unlink(missing_ok=True)
        if previous is None:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, registry_path)
        else:
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, registry_path) as key:
                winreg.SetValueEx(key, '', 0, winreg.REG_SZ, previous)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker', type=Path, required=True)
    parser.add_argument('--installer', type=Path, required=True)
    args = parser.parse_args()
    main(args.worker, args.installer)

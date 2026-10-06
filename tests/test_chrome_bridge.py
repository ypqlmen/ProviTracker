import io
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
import tempfile
import time
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'intramanager_worker'))
import chrome_bridge as bridge

class ChromeBridgeTests(unittest.TestCase):
    def test_framing_unicode(self):
        stream = io.BytesIO()
        bridge.write_frame(stream, {'message': 'Åbent masterark'})
        stream.seek(0)
        self.assertEqual(bridge.read_frame(stream), {'message':'Åbent masterark'})
        self.assertIsNone(bridge.read_frame(stream))

    def test_rejects_malformed_frames(self):
        for raw in [b'x', struct.pack('<I', bridge.LIMIT+1), struct.pack('<I', 10)+b'{}', struct.pack('<I',2)+b'[]']:
            with self.assertRaises(ValueError): bridge.read_frame(io.BytesIO(raw))

    def test_exact_destination(self):
        valid = 'https://5rmarketing-my.sharepoint.com/path?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}'
        self.assertTrue(bridge.valid_workbook(valid))
        for url in [valid.replace('https:', 'http:'), valid.replace('.com/', '.com.evil.test/'), valid.replace('https://', 'https://user@'), valid.split('?')[0]]:
            self.assertFalse(bridge.valid_workbook(url))

    def test_extension_identity_and_permissions(self):
        self.assertRegex(bridge.extension_id(), '^[a-p]{32}$')
        manifest = json.loads((bridge.extension_dir() / 'manifest.json').read_text())
        self.assertEqual(manifest['host_permissions'], ['https://5rmarketing-my.sharepoint.com/*', 'https://euc-excel.officeapps.live.com/*', 'https://fa000000043.mro1cdnstorage.public.onecdn.static.microsoft/*'])
        self.assertNotIn('cookies', manifest['permissions'])

    def test_native_roundtrip_rejects_stale_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            request = {'type':'probe', 'requestId':'fresh', 'expiresAt':time.time()+30,
                'workbookUrl':'https://5rmarketing-my.sharepoint.com/a?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}'}
            bridge.atomic_json(root / 'request.json', request)
            stdin, stdout = io.BytesIO(), io.BytesIO()
            for message in [{'type':'poll'}, {'type':'result', 'requestId':'stale', 'success':True, 'status':'chrome-connected'},
                            {'type':'result', 'requestId':'fresh', 'success':True, 'status':'chrome-connected', 'background':True}]:
                bridge.write_frame(stdin, message)
            stdin.seek(0)
            with patch.object(bridge, 'root', return_value=root), patch.object(bridge, 'prepare_native_stdio'), \
                 patch.object(bridge.sys, 'stdin', SimpleNamespace(buffer=stdin)), patch.object(bridge.sys, 'stdout', SimpleNamespace(buffer=stdout)):
                self.assertEqual(bridge.native_main('chrome-extension://' + bridge.extension_id() + '/'), 0)
            stdout.seek(0)
            self.assertEqual(bridge.read_frame(stdout), request)
            result = bridge.read_json(root / 'response.json')
            self.assertEqual(result['requestId'], 'fresh')
            self.assertTrue(result['success'])
            self.assertTrue(result['background'])

    def test_registration_receipt_is_bound_to_request_and_sale(self):
        registration = {'requestId':'request', 'registrationId':'sale', 'orderNumber':'BISS'}
        receipt = {'success':True, 'scriptVersion':3, 'requestId':'request', 'registrationId':'sale', 'orderNumber':'BISS', 'status':'registered', 'row':123}
        self.assertTrue(bridge.valid_receipt(receipt, registration))
        for key, value in [('requestId','stale'),('registrationId','other-sale'),('orderNumber','other-order'),('row',True),('row',3),('scriptVersion',2),('status','checked')]:
            self.assertFalse(bridge.valid_receipt({**receipt, key:value}, registration), (key,value))
        setup = {'requestId':'request', 'isTest':True}
        self.assertTrue(bridge.valid_receipt({**receipt,'status':'checked','row':0,'orderNumber':'','registrationId':''}, setup))
        self.assertFalse(bridge.valid_receipt(receipt, setup))

    def test_register_native_roundtrip_validates_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            registration = {'requestId':'fresh','registrationId':'sale','orderNumber':'BISS'}
            request = {'type':'register','requestId':'fresh','registration':registration,'expiresAt':time.time()+30,
                'workbookUrl':'https://5rmarketing-my.sharepoint.com/a?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}'}
            bridge.atomic_json(root / 'request.json',request)
            stdin,stdout=io.BytesIO(),io.BytesIO()
            receipt={'type':'result','success':True,'scriptVersion':3,**registration,'row':123,'status':'registered'}
            for message in [{'type':'poll'}, {**receipt,'registrationId':'wrong'}, receipt]: bridge.write_frame(stdin,message)
            stdin.seek(0)
            with patch.object(bridge,'root',return_value=root),patch.object(bridge,'prepare_native_stdio'), \
                patch.object(bridge.sys,'stdin',SimpleNamespace(buffer=stdin)),patch.object(bridge.sys,'stdout',SimpleNamespace(buffer=stdout)):
                bridge.native_main('chrome-extension://'+bridge.extension_id()+'/')
            stdout.seek(0)
            self.assertEqual(bridge.read_frame(stdout),request)
            result=bridge.read_json(root / 'response.json')
            self.assertTrue(result['success']);self.assertEqual(result['registrationId'],'sale')

    def test_setup_keeps_app_request_id_and_unicode_file_size(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge,'root',return_value=Path(folder)),patch.object(bridge,'register_host'):
            request_id='00000000-0000-4000-8000-000000000001'
            def respond(_):
                envelope=bridge.read_json(Path(folder)/'request.json')
                self.assertEqual(envelope['requestId'],request_id)
                bridge.atomic_json(Path(folder)/'response.json',{'success':True,'status':'checked','row':0,'orderNumber':'','registrationId':'','scriptVersion':3,'requestId':request_id})
            with patch.object(bridge.time,'sleep',side_effect=respond):
                result=bridge.run_registration({'action':'chrome-setup','workbookUrl':'https://5rmarketing-my.sharepoint.com/a?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}', 'registration':{'requestId':request_id}})
            self.assertTrue(result['success'])
            self.assertFalse((Path(folder)/'request.json').exists())
            data={'note':'ÆØÅ'*1500}
            bridge.atomic_json(Path(folder)/'unicode.json',data)
            self.assertEqual(bridge.read_json(Path(folder)/'unicode.json'),data)

    def test_rejects_unknown_origin_before_reading(self):
        self.assertEqual(bridge.native_main('chrome-extension://' + 'a'*32 + '/evil'), 1)

    def test_prepare_can_be_repeated_without_changing_install_location(self):
        with tempfile.TemporaryDirectory(prefix='provi-æøå-') as folder:
            with patch.object(bridge, 'root', return_value=Path(folder)), patch.object(bridge, 'register_host') as register:
                first = bridge.install_extension()
                path = Path(first['extensionPath'])
                (path / 'popup.js').write_text('old version')
                second = bridge.install_extension()
                self.assertEqual(first["extensionPath"], second["extensionPath"])
                self.assertTrue(first["updated"])
                self.assertTrue(second["updated"])
                self.assertEqual(register.call_count, 2)
                self.assertEqual((path / 'popup.js').read_bytes(), (bridge.extension_dir() / 'popup.js').read_bytes())
                self.assertEqual(len(list(path.glob('*'))), len(bridge.EXTENSION_FILES))
                self.assertFalse(bridge.install_extension()['updated'])

    def test_new_assets_are_copied_and_verified_before_reload(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge, 'root', return_value=Path(folder)), \
             patch.object(bridge, 'register_host'):
            root = Path(folder)
            bridge.install_extension()
            bundle = root / 'bundle'
            bundle.mkdir()
            for name in bridge.EXTENSION_FILES:
                (bundle / name).write_bytes((bridge.extension_dir() / name).read_bytes())
            (bundle / 'assets').mkdir()
            (bundle / 'assets' / 'new.js').write_text('// new feature')
            with patch.object(bridge, 'extension_dir', return_value=bundle):
                self.assertTrue(bridge.sync_installed_extension()['updated'])
            ready = bridge.ready_package()
            self.assertIn('assets/new.js', ready['files'])
            (root / 'extension' / 'assets' / 'new.js').unlink()
            self.assertEqual(bridge.ready_package(), {})
            for unsafe in ['../outside.json', '/outside.json', 'C:\\outside.json']:
                self.assertFalse(bridge.valid_package_files(list(bridge.EXTENSION_FILES)+[unsafe]))

    def test_update_does_not_install_without_user_setup(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge, 'root', return_value=Path(folder)), \
             patch.object(bridge, 'register_host') as register:
            self.assertEqual(bridge.sync_installed_extension()['status'], 'chrome-not-installed')
            register.assert_not_called()

    def test_upgrade_defers_during_probe_and_never_downgrades(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge, 'root', return_value=Path(folder)), \
             patch.object(bridge, 'register_host'):
            root = Path(folder)
            bridge.install_extension()
            bundle = root / 'bundle'
            bundle.mkdir()
            for name in bridge.EXTENSION_FILES:
                (bundle / name).write_bytes((bridge.extension_dir() / name).read_bytes())
            manifest = bridge.read_json(bundle / 'manifest.json')
            old_version = manifest['version']
            parts = [int(p) for p in manifest['version'].split('.')]
            parts[-1] += 1
            next_version = '.'.join(map(str, parts))
            manifest['version'] = next_version
            bridge.atomic_json(bundle / 'manifest.json', manifest)
            bridge.atomic_json(root / 'request.json', {'expiresAt': time.time()+30})
            with patch.object(bridge, 'extension_dir', return_value=bundle):
                self.assertEqual(bridge.sync_installed_extension()['status'], 'chrome-update-deferred')
                self.assertEqual(bridge.ready_package()['version'], old_version)
                (root / 'request.json').unlink()
                self.assertTrue(bridge.sync_installed_extension()['updated'])
                self.assertEqual(bridge.ready_package()['version'], next_version)
            self.assertFalse(bridge.sync_installed_extension()['updated'])
            self.assertEqual(bridge.ready_package()['version'], next_version)

    def test_interrupted_copy_cannot_trigger_reload_and_can_be_repaired(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge, 'root', return_value=Path(folder)), \
             patch.object(bridge, 'register_host'):
            root = Path(folder)
            bridge.install_extension()
            bundle = root / 'bundle'
            bundle.mkdir()
            for name in bridge.EXTENSION_FILES:
                (bundle / name).write_bytes((bridge.extension_dir() / name).read_bytes())
            (bundle / 'background.js').write_text('// new background')
            manifest = bridge.read_json(bundle / 'manifest.json')
            parts = [int(p) for p in manifest['version'].split('.')]
            parts[-1] += 1
            next_version = '.'.join(map(str, parts))
            manifest['version'] = next_version
            bridge.atomic_json(bundle / 'manifest.json', manifest)
            original = Path.replace
            def interrupted(path, target):
                if target.name == 'identity.js':
                    raise OSError('Simulated interruption')
                return original(path, target)
            with patch.object(bridge, 'extension_dir', return_value=bundle):
                with patch.object(Path, 'replace', interrupted), self.assertRaises(OSError):
                    bridge.sync_installed_extension()
                self.assertEqual(bridge.ready_package(), {})
                self.assertTrue(bridge.sync_installed_extension()['updated'])
                self.assertTrue(bridge.ready_package())
                self.assertEqual(list((root / 'extension').glob('*.tmp')), [])

    def test_update_lock_excludes_concurrent_updates(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge, 'root', return_value=Path(folder)):
            with bridge.update_lock():
                with self.assertRaises(RuntimeError):
                    with bridge.update_lock():
                        self.fail('Concurrent update acquired the lock')
            with bridge.update_lock():
                pass

    def test_native_requests_reload_then_confirms_loaded_version(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge, 'root', return_value=Path(folder)), \
             patch.object(bridge, 'register_host'):
            bridge.install_extension()
            version = bridge.ready_package()['version']
            stdin, stdout = io.BytesIO(), io.BytesIO()
            for loaded in ['0.1.1', version, '0.1.9', 'bad', None]:
                bridge.write_frame(stdin, {'type':'poll', 'extensionVersion':loaded})
            stdin.seek(0)
            with patch.object(bridge, 'prepare_native_stdio'), \
                 patch.object(bridge.sys, 'stdin', SimpleNamespace(buffer=stdin)), \
                 patch.object(bridge.sys, 'stdout', SimpleNamespace(buffer=stdout)):
                bridge.native_main('chrome-extension://' + bridge.extension_id() + '/')
            stdout.seek(0)
            self.assertEqual(bridge.read_frame(stdout), {'type':'reload-extension', 'version':version})
            self.assertEqual(bridge.read_frame(stdout)['type'], 'extension-update-error')
            self.assertIsNone(bridge.read_frame(stdout))
            self.assertEqual(bridge.read_json(Path(folder) / 'loaded.json')['version'], version)

    def test_probe_started_during_poll_prevents_reload(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bridge, 'root', return_value=Path(folder)):
            root = Path(folder)
            request = {'type':'probe', 'requestId':'started', 'expiresAt':time.time()+30,
                'workbookUrl':'https://5rmarketing-my.sharepoint.com/a?sourcedoc={535121b9-ed93-447b-9f89-7e8d575d03e4}'}
            def start_probe():
                bridge.atomic_json(root / 'request.json', request)
            stdin, stdout = io.BytesIO(), io.BytesIO()
            bridge.write_frame(stdin, {'type':'poll', 'extensionVersion':'0.1.1'})
            stdin.seek(0)
            with patch.object(bridge, 'sync_installed_extension', side_effect=start_probe), \
                 patch.object(bridge, 'ready_package', return_value={'version':'0.1.2'}), \
                 patch.object(bridge, 'prepare_native_stdio'), \
                 patch.object(bridge.sys, 'stdin', SimpleNamespace(buffer=stdin)), \
                 patch.object(bridge.sys, 'stdout', SimpleNamespace(buffer=stdout)):
                bridge.native_main('chrome-extension://' + bridge.extension_id() + '/')
            stdout.seek(0)
            self.assertEqual(bridge.read_frame(stdout), request)
            self.assertIsNone(bridge.read_frame(stdout))

if __name__ == '__main__': unittest.main()

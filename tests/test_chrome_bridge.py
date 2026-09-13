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
        self.assertEqual(manifest['host_permissions'], ['https://5rmarketing-my.sharepoint.com/*'])
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
            with patch.object(bridge, 'root', return_value=root), patch.object(bridge.sys, 'platform', 'test'), \
                 patch.object(bridge.sys, 'stdin', SimpleNamespace(buffer=stdin)), patch.object(bridge.sys, 'stdout', SimpleNamespace(buffer=stdout)):
                self.assertEqual(bridge.native_main('chrome-extension://' + bridge.extension_id() + '/'), 0)
            stdout.seek(0)
            self.assertEqual(bridge.read_frame(stdout), request)
            result = bridge.read_json(root / 'response.json')
            self.assertEqual(result['requestId'], 'fresh')
            self.assertTrue(result['success'])
            self.assertTrue(result['background'])

    def test_rejects_unknown_origin_before_reading(self):
        self.assertEqual(bridge.native_main('chrome-extension://' + 'a'*32 + '/evil'), 1)

    def test_prepare_can_be_repeated_without_changing_install_location(self):
        with tempfile.TemporaryDirectory(prefix='provi-æøå-') as folder:
            with patch.object(bridge, 'root', return_value=Path(folder)), patch.object(bridge, 'register_host') as register:
                first = bridge.install_extension()
                path = Path(first['extensionPath'])
                (path / 'popup.js').write_text('old version')
                second = bridge.install_extension()
                self.assertEqual(first, second)
                self.assertEqual(register.call_count, 2)
                self.assertEqual((path / 'popup.js').read_bytes(), (bridge.extension_dir() / 'popup.js').read_bytes())
                self.assertEqual(len(list(path.glob('*'))), 6)

if __name__ == '__main__': unittest.main()

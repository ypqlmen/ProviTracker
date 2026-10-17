import sys
import json
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'intramanager_worker'))
from master_registration import parse_receipt, valid_workbook_url
class ReceiptTests(unittest.TestCase):
    def test_reject_unconfirmed_or_stale_results(self):
        valid = dict(success=True, scriptVersion=2, requestId='fresh', status='registered', orderNumber='000123', row=116)
        line = lambda r: 'PROVITRACKER_RESULT:' + json.dumps(r)
        self.assertIsNotNone(parse_receipt(line(valid),'fresh','000123'))
        for patch in [dict(requestId='old'),dict(success=False),dict(orderNumber='other'),dict(status='checked'),dict(row=0),dict(scriptVersion=1)]:
            self.assertIsNone(parse_receipt(line(valid|patch),'fresh','000123'))
        for bad in ['[]','null','42','{"success":true}', 'not json']:
            self.assertIsNone(parse_receipt('PROVITRACKER_RESULT:'+bad,'fresh','000123'))
    def test_url(self):
        self.assertTrue(valid_workbook_url('https://tenant.sharepoint.com/Documents/master.xlsx'))
        for bad in ['https://sharepoint.com.evil.test/a','http://tenant.sharepoint.com/a','https://user:password@tenant.sharepoint.com/a',None]:
            self.assertFalse(valid_workbook_url(bad))
if __name__ == '__main__': unittest.main()

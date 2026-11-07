import importlib.util,io,json,struct,sys,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('mail_bridge',Path(__file__).parents[1]/'intramanager_worker/chrome_bridge.py');bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
class SalesMailTests(unittest.TestCase):
 def setUp(self):
  self.html='<table style="border-collapse:collapse"><tbody>'+''.join('<tr>'+''.join('<td style="background-color:#92D050">cell</td>' for _ in range(6))+'</tr>' for _ in range(3))+'</tbody></table>'
  self.mail={'subject':'salgs reg','recipient':'recipient@example.com','registrationId':'00000000-0000-4000-8000-000000000001','html':self.html,'text':'fixture'}
  self.query={'type':'outlook','operation':'send','requestId':'00000000-0000-4000-8000-000000000002','expectedSender':'seller@example.com','mail':self.mail}
 def test_mail_is_one_explicit_recipient(self):
  self.assertTrue(bridge.valid_outlook_request(self.query))
  for address in ['a@example.com\r\nBcc:b@example.com','a@example.com,b@example.com','a@example.com;b@example.com','mailto:a@example.com']:
   self.assertFalse(bridge.valid_outlook_request({**self.query,'mail':{**self.mail,'recipient':address}}))
 def test_unsafe_or_incomplete_html_is_rejected(self):
  for html in ['<img src="https://example.com/pixel">',self.html.replace('<td ','<td onclick="x()" ',1),self.html.replace('#92D050','url(https://example.com)',1),self.html.replace('background-color','background-image',1),self.html.replace('<tr>','',1),self.html+'<script>x()</script>']:
   self.assertFalse(bridge.valid_mail_table(html),html)
 def test_receipt_matches_attempt_sender_recipient_and_content(self):
  result={'success':True,'status':'mail-sent','requestId':self.query['requestId'],'senderEmail':'seller@example.com','recipient':self.mail['recipient'],'registrationId':self.mail['registrationId'],'contentHash':bridge.mail_content_hash(self.mail,'seller@example.com')}
  self.assertTrue(bridge.valid_outlook_result(result,self.query))
  for key,value in [('requestId','old'),('senderEmail','other@example.com'),('recipient','other@example.com'),('registrationId','other'),('contentHash','0'*64)]:self.assertFalse(bridge.valid_outlook_result({**result,key:value},self.query))
 def test_large_mail_frames_keep_original_nonmail_limit(self):
  value={'type':'outlook','padding':'x'*20000};stream=io.BytesIO();bridge.write_frame(stream,value);stream.seek(0);self.assertEqual(bridge.read_frame(stream),value)
  with self.assertRaises(ValueError):bridge.write_frame(io.BytesIO(),{'type':'poll','padding':'x'*20000})
  with self.assertRaises(ValueError):bridge.read_frame(io.BytesIO(struct.pack('<I',bridge.MAIL_LIMIT+1)))
 def test_snapshot_must_belong_to_verified_archive(self):
  receipt={'row':125,'worksheetId':'sheet'};request={'registrationId':self.mail['registrationId']};snapshot={'row':125,'worksheetId':'sheet','registrationId':self.mail['registrationId'],'html':self.html,'text':'fixture'}
  self.assertTrue(bridge.valid_mail_snapshot(snapshot,receipt,request));self.assertFalse(bridge.valid_mail_snapshot({**snapshot,'row':126},receipt,request))
if __name__=='__main__':unittest.main()

"""Render the installed app with fictional, isolated data, without cloud login."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import struct
import tempfile

def capture(app, output):
    output=output.resolve();output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='provi-visual-demo-') as data:
        root=Path(data)
        samples={'salespeople.json':[{'id':'demo','name':'Demo · fiktive tal'}],
            'orders.json':[{'id':'DEMO-%03d'%(i+1),'salespersonId':'demo','sellerInitials':'DEMO','companyName':company,'createdAt':'2026-10-%02dT14:30:00'%(16-i),'items':[{'productKey':'mobil_25gb_36','quantity':2+i},{'productKey':'til_datakort','quantity':1}],'note':note,'masterRegistrationState':state} for i,(company,note,state) in enumerate([('Nordlys Studio · fiktiv virksomhed','Klar til overførsel','pending'),('Eksempel Design · fiktiv virksomhed','Forlængelse af eksisterende abonnement','registered'),('Demo Arkitektur · fiktiv virksomhed','En længere note, som skal kunne læses uden at overlappe de øvrige kolonner.','error'),('Fiktivt Konsulenthus','Aftale godkendt','registered')])],'products.json':[],
            'settings.json':{'activeSalespersonId':'demo','hourlyRate':102.91,'taxDeduction':4500,'taxRatePercent':38,
                'intramanagerEnabled':False,
                'intramanagerHoursCache':{'21-09-2026_20-10-2026':{'hours':110.5,'phoneHours':95,'sickHours':8.75,'sickPay':874.34,'sickPayFull':1748.68,'cancellations':-350,'payrollSchema':2,'syncedAt':'2026-10-17T12:00:00'}}}}
        for name,value in samples.items():(root/name).write_text(json.dumps(value),encoding='utf-8')
        env={**os.environ,'PROVI_DATA_DIR':data,'PROVI_VISUAL_REVIEW_DIR':str(output),'QT_QPA_PLATFORM':'offscreen'}
        if os.name=='nt':
            # Qt's Windows offscreen plugin uses FreeType and does not discover
            # the normal Windows font registry. Point it at installed fonts.
            fontdir=Path(os.environ['WINDIR'])/'Fonts'
            assert (fontdir/'segoeui.ttf').is_file(),'Windows UI font is missing'
            env['QT_QPA_FONTDIR']=str(fontdir)
        subprocess.run([str(app.resolve())],env=env,check=True,timeout=30)
        images=list(output.glob('*.png'))
        expected={'01-dashboard.png','02-orders.png','03-reports.png','04-contacts-preview.png','05-settings.png',
            '06-new-order-dialog.png','08-settings-payroll.png','09-settings-intramanager.png','10-settings-masterark.png','11-settings-account.png','12-dashboard-compact.png','13-orders-empty.png','14-dashboard-small.png','15-new-order-compact.png','16-orders-filtered.png','17-login.png','18-report-details.png','19-report-compact.png','20-report-empty.png','21-product-picker.png','22-product-search.png','23-product-empty.png','24-product-compact.png','25-edit-order.png'}
        assert {p.name for p in images}==expected,[p.name for p in images]
        for image in images:
            data=image.read_bytes()
            assert len(data)>512 and data[:8]==b'\x89PNG\r\n\x1a\n',image.name
            width,height=struct.unpack('>II',data[16:24])
            assert width>=160 and height>=120,(image.name,width,height)
        print('Twenty-four premium interface views captured, including all settings pages, compact dashboard and empty orders. New-order save and edit preserve selected quantities and metadata. Contact preview cannot search.')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--app',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();capture(args.app,args.output)

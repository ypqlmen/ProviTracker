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
            'orders.json':[],'products.json':[],
            'settings.json':{'activeSalespersonId':'demo','hourlyRate':102.91,'taxDeduction':4500,'taxRatePercent':38,
                'automaticTaxEstimate':True,'intramanagerEnabled':False,
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
        expected={'01-dashboard.png','02-orders.png','03-reports.png','05-settings.png',
            '06-new-order-dialog.png','07-seller-dialog.png','08-settings-middle.png','09-settings-registration.png'}
        assert {p.name for p in images}==expected,[p.name for p in images]
        for image in images:
            data=image.read_bytes()
            assert len(data)>512 and data[:8]==b'\x89PNG\r\n\x1a\n',image.name
            width,height=struct.unpack('>II',data[16:24])
            assert width>=160 and height>=120,(image.name,width,height)
        print('Eight Midnight Grey native views captured; contacts are absent for the unauthenticated demo.')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--app',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();capture(args.app,args.output)

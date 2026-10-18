"""Bounded native Chrome transport. Only local app requests carry sale data; Chrome returns receipts."""
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import time
import uuid
import math
from datetime import datetime
from contextlib import contextmanager
from urllib.parse import urlparse, parse_qs

HOST = 'dk.provitracker.masterark'
LIMIT = 16384
EXTENSION_FILES = ('manifest.json', 'background.js', 'diagnostics.js', 'intramanager.js', 'identity.js', 'probe.js', 'popup.html', 'popup.js', 'office_ui.js', 'office_setup.js', 'receipt.js', 'excel_online_sales_registration_v3.ts', 'excel_online_sales_registration_v4.ts')

def version_tuple(value):
    parts = str(value).split('.')
    if not 1 <= len(parts) <= 4 or any(not p.isdigit() or int(p) > 65535 for p in parts):
        raise ValueError('Ugyldigt versionsnummer i Chrome-udvidelsen.')
    return tuple(int(p) for p in parts) + (0,) * (4 - len(parts))

def package_hash(contents):
    digest = hashlib.sha256()
    for name in sorted(contents):
        digest.update(name.encode('utf-8') + b'\0' + contents[name] + b'\0')
    return digest.hexdigest()

@contextmanager
def update_lock():
    folder = root()
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / 'update.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            if sys.platform == 'win32':
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise RuntimeError('Chrome-udvidelsen opdateres allerede. Prøv igen om lidt.') from exc
        try:
            yield
        finally:
            lock.seek(0)
            if sys.platform == 'win32':
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)

def ready_package():
    folder = root()
    ready = read_json(folder / 'installed.json')
    try:
        names = ready.get('files', [])
        if not valid_package_files(names):
            return {}
        contents = {name: (folder / 'extension' / name).read_bytes() for name in names}
        manifest = json.loads(contents['manifest.json'])
        if ready.get('version') == manifest['version'] and ready.get('hash') == package_hash(contents):
            return ready
    except (OSError, ValueError, KeyError):
        pass
    return {}

def valid_package_files(names):
    return isinstance(names, list) and all(
        isinstance(name, str) and name and '\\' not in name and ':' not in name
        and not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
        for name in names) and set(EXTENSION_FILES).issubset(names)

def extension_dir():
    return Path(sys._MEIPASS) / 'chrome_extension' if getattr(sys, 'frozen', False) else Path(__file__).resolve().parent.parent / 'chrome_extension'

def extension_id():
    manifest = json.loads((extension_dir() / 'manifest.json').read_text(encoding='utf-8'))
    digest = hashlib.sha256(base64.b64decode(manifest['key'])).hexdigest()[:32]
    return ''.join(chr(ord('a') + int(c, 16)) for c in digest)

def root():
    return Path(os.environ['LOCALAPPDATA']) / 'ProviTrackerChromeBridge'

def read_frame(stream):
    header = stream.read(4)
    if not header: return None
    if len(header) != 4: raise ValueError('Truncated header')
    length, = struct.unpack('<I', header)
    if not 0 < length <= LIMIT: raise ValueError('Invalid frame size')
    raw = stream.read(length)
    if len(raw) != length: raise ValueError('Truncated message')
    value = json.loads(raw)
    if not isinstance(value, dict): raise ValueError('Expected object')
    return value

def write_frame(stream, value):
    raw = json.dumps(value, ensure_ascii=False).encode('utf-8')
    if len(raw) > LIMIT: raise ValueError('Message too large')
    stream.write(struct.pack('<I', len(raw)) + raw)
    stream.flush()

def atomic_json(path, value):
    temp = path.with_suffix('.' + uuid.uuid4().hex + '.tmp')
    try:
        temp.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
        temp.replace(path)
    finally:
        temp.unlink(missing_ok=True)

def read_json(path):
    try:
        if path.stat().st_size > LIMIT: return {}
        value = json.loads(path.read_text(encoding='utf-8'))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError): return {}

def valid_workbook(url):
    try:
        parsed = urlparse(url)
        params = {k.lower(): v for k, v in parse_qs(parsed.query).items()}
        uuid.UUID(params['sourcedoc'][0].strip('{}'))
        return parsed.scheme == 'https' and parsed.hostname == '5rmarketing-my.sharepoint.com' and not parsed.username and not parsed.password
    except (ValueError, KeyError, TypeError): return False

def register_host():
    if sys.platform != 'win32' or not getattr(sys, 'frozen', False):
        raise RuntimeError('Chrome-forbindelsen skal afprøves fra den installerede Windows-version.')
    import winreg
    folder = root()
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / 'host.json'
    atomic_json(path, {'name': HOST, 'description': 'Provi Tracker masterark', 'path': sys.executable,
        'type': 'stdio', 'allowed_origins': ['chrome-extension://' + extension_id() + '/']})
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, 'Software\\Google\\Chrome\\NativeMessagingHosts\\' + HOST) as key:
        winreg.SetValueEx(key, '', 0, winreg.REG_SZ, str(path))

def install_extension(only_existing=False):
    # A stable user-owned directory keeps Chrome's unpacked installation valid
    # after application upgrades. Never load from PyInstaller's internal directory.
    destination = root() / 'extension'
    if only_existing and not (destination / 'manifest.json').exists():
        return {'success': True, 'status': 'chrome-not-installed', 'updated': False}
    with update_lock():
        request = read_json(root() / 'request.json')
        if request.get('expiresAt', 0) > time.time():
            return {'success': True, 'status': 'chrome-update-deferred', 'updated': False}
        source = extension_dir()
        contents = {path.relative_to(source).as_posix(): path.read_bytes()
                    for path in source.rglob('*') if path.is_file()}
        if not valid_package_files(list(contents)):
            raise ValueError('Chrome-udvidelsens pakke er ufuldstændig.')
        manifest = json.loads(contents['manifest.json'])
        version_tuple(manifest['version'])
        current = read_json(destination / 'manifest.json')
        # An older running helper must never roll back a newer installed extension.
        if current.get('version') and version_tuple(current['version']) > version_tuple(manifest['version']):
            return {'success': True, 'status': 'chrome-prepared', 'updated': False,
                'extensionPath': str(destination), 'extensionVersion': current['version']}
        register_host()
        destination.mkdir(parents=True, exist_ok=True)
        changed = any(not (destination / name).exists() or (destination / name).read_bytes() != data
                      for name, data in contents.items())
        if changed:
            # Publish the manifest and readiness marker last. A partial copy cannot trigger reload.
            for name in sorted(n for n in contents if n != 'manifest.json') + ['manifest.json']:
                target = destination / name
                target.parent.mkdir(parents=True, exist_ok=True)
                temp = target.with_name(target.name + '.' + uuid.uuid4().hex + '.tmp')
                try:
                    temp.write_bytes(contents[name])
                    temp.replace(target)
                finally:
                    temp.unlink(missing_ok=True)
        atomic_json(root() / 'installed.json', {'version': manifest['version'], 'hash': package_hash(contents),
            'files': sorted(contents)})
        return {'success': True, 'status': 'chrome-prepared', 'updated': changed,
            'extensionPath': str(destination), 'extensionVersion': manifest['version']}

def sync_installed_extension():
    return install_extension(only_existing=True)

def valid_receipt(value, registration):
    if not isinstance(value, dict) or value.get('success') is not True or value.get('scriptVersion') != 4 or value.get('requestId') != registration.get('requestId'):
        return False
    if not isinstance(value.get('worksheetName'), str) or not value['worksheetName'].strip() or not isinstance(value.get('worksheetId'), str) or not value['worksheetId']:
        return False
    if registration.get('worksheetName') and value['worksheetName'] != registration['worksheetName']:
        return False
    if registration.get('isTest') is True:
        return value.get('status') == 'checked' and value.get('orderNumber') == '' and value.get('registrationId') == '' and type(value.get('row')) is int and value.get('row') == 0
    return (value.get('status') in {'registered', 'already_registered'} and type(value.get('row')) is int and value['row'] >= 16
        and value.get('orderNumber') == registration.get('orderNumber') and value.get('registrationId') == registration.get('registrationId'))

def probe(payload):
    return request_chrome(payload, 'probe')

def run_registration(payload):
    return request_chrome(payload, 'install' if payload.get('action') == 'chrome-autosetup' else 'setup' if payload.get('action') == 'chrome-setup' else 'register')

def valid_intramanager_request(value):
    if value.get('type')!='intramanager' or value.get('workbookUrl')!='https://5r.intramanager.com/reports/history/': return False
    if value.get('operation')=='profile': return True
    if value.get('operation')!='hours' or not isinstance(value.get('expectedProfile'),str) or not 3<=len(value['expectedProfile'])<=140: return False
    try:
        start=datetime.strptime(value['fromDate'],'%d-%m-%Y')
        end=datetime.strptime(value['toDate'],'%d-%m-%Y')
        return 0<=(end-start).days<=370
    except (ValueError,TypeError,KeyError): return False

def valid_intramanager_result(value, query):
    if value.get('success') is not True or not isinstance(value.get('profileName'),str) or not 3<=len(value['profileName'])<=140: return False
    if query.get('operation')=='profile': return True
    normalize=lambda text:' '.join(text.casefold().split())
    if normalize(value['profileName'])!=normalize(query.get('expectedProfile','')): return False
    if value.get('periodFrom')!=query.get('fromDate') or value.get('periodTo')!=query.get('toDate') or value.get('payrollSchema')!=2: return False
    for key in ['hours','phoneHours','sickHours','sickPay','cancellations']:
        n=value.get(key)
        if type(n) not in (int,float) or not math.isfinite(n) or abs(n)>10000000: return False
        if key in ['hours','phoneHours','sickHours'] and n<0: return False
    return True

def request_intramanager(payload):
    return request_chrome({**payload,'workbookUrl':'https://5r.intramanager.com/reports/history/'},'intramanager')

def request_chrome(payload, kind):
    url = payload.get('workbookUrl', '')
    if kind!='intramanager' and not valid_workbook(url): raise ValueError('Gem et direkte link til masterarket med sourcedoc i adressen.')
    register_host()
    folder = root()
    request_id = str(uuid.uuid4())
    if kind in {'setup','install'} and payload.get('registration', {}).get('requestId'):
        request_id = str(uuid.UUID(payload['registration']['requestId']))
    registration = {'isTest': True, 'requestId': request_id} if kind in {'setup','install'} else dict(payload.get('registration', {}))
    if kind == 'register':
        try:
            uuid.UUID(registration['registrationId'])
            uuid.UUID(registration['requestId'])
        except (ValueError, KeyError, TypeError) as exc:
            raise ValueError('Registreringen mangler et gyldigt kontrolnummer. Ældre salg skal kontrolleres manuelt.') from exc
        request_id = registration['requestId']
        if registration.get('isTest') is True or not isinstance(registration.get('orderNumber'), str) or not registration['orderNumber'].strip():
            raise ValueError('Ugyldig salgsregistrering.')
    duration = 300 if kind=='install' else 60 if kind=='intramanager' else 35 if kind == 'probe' else 180
    envelope = {'type': kind, 'requestId': request_id, 'workbookUrl': url, 'expiresAt': time.time()+duration}
    if kind not in {'probe','intramanager'}: envelope['registration'] = registration
    if kind=='intramanager':
        envelope.update({key:payload.get(key) for key in ['operation','fromDate','toDate','expectedProfile']})
        if not valid_intramanager_request(envelope): raise ValueError('Kontrollér kontonavn og periode for Intramanager i Chrome.')
    if len(json.dumps(envelope, ensure_ascii=False).encode('utf-8')) > LIMIT:
        raise ValueError('Salgsregistreringen er for lang til Chrome. Forkort bemærkningerne og prøv igen.')
    request = folder / 'request.json'
    response = folder / 'response.json'
    with update_lock():
        existing = read_json(request)
        if existing.get('expiresAt', 0) > time.time(): raise RuntimeError('En anden Chrome-kontrol er i gang. Prøv igen om lidt.')
        atomic_json(request, envelope)
    try:
        last_phase = None
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if kind=='install':
                progress = read_json(folder / 'progress.json')
                phase = progress.get('phase')
                if progress.get('requestId')==request_id and phase in SETUP_PHASES and phase!=last_phase:
                    last_phase=phase
                    print('PROVITRACKER_PROGRESS:'+json.dumps({'phase':phase}),file=sys.stderr,flush=True)
            result = read_json(response)
            if result.get('requestId') == request_id:
                if kind=='probe' or result.get('success') is not True or (valid_intramanager_result(result,envelope) if kind=='intramanager' else valid_receipt(result, registration)): return result
                return {'success':False,'error':'Excel returnerede en forkert kvittering. Kontrollér salget før genforsøg.'}
            time.sleep(.2)
        return {'success':False, 'error':'Chrome svarede ikke. Åbn Chrome og masterarket, og klik på Provi Tracker-udvidelsen. Prøv derefter igen.'}
    finally:
        if read_json(request).get('requestId') == request_id: request.unlink(missing_ok=True)
        if read_json(response).get('requestId') == request_id: response.unlink(missing_ok=True)

def prepare_native_stdio():
    if sys.platform == 'win32':
        import msvcrt
        msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
        msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)

def native_main(origin):
    if origin != 'chrome-extension://' + extension_id() + '/': return 1
    prepare_native_stdio()
    last_id = None
    while True:
        message = read_frame(sys.stdin.buffer)
        if message is None: return 0
        request = read_json(root() / 'request.json')
        live = request.get('expiresAt', 0) > time.time()
        if message.get('type') == 'poll':
            if not live:
                try:
                    sync_installed_extension()
                    with update_lock():
                        # A probe or another updater may have started since the initial read.
                        request = read_json(root() / 'request.json')
                        live = request.get('expiresAt', 0) > time.time()
                        ready = ready_package()
                        loaded = message.get('extensionVersion')
                        if not live and ready and loaded:
                            if version_tuple(loaded) < version_tuple(ready['version']):
                                write_frame(sys.stdout.buffer, {'type': 'reload-extension', 'version': ready['version']})
                            elif loaded == ready['version']:
                                atomic_json(root() / 'loaded.json', {'version': loaded, 'checkedAt': time.time()})
                except (OSError, ValueError, RuntimeError) as exc:
                    write_frame(sys.stdout.buffer, {'type': 'extension-update-error', 'error': str(exc)[:500]})
            if live and request.get('requestId') != last_id and (valid_intramanager_request(request) if request.get('type')=='intramanager' else valid_workbook(request.get('workbookUrl', ''))):
                last_id = request['requestId']
                write_frame(sys.stdout.buffer, request)
        elif message.get('type') == 'progress' and live and request.get('type')=='install' and message.get('requestId')==last_id==request.get('requestId'):
            if message.get('phase') in SETUP_PHASES:
                atomic_json(root() / 'progress.json', {'requestId':last_id,'phase':message['phase']})
        elif message.get('type') == 'result' and live and message.get('requestId') == last_id == request.get('requestId'):
            if request.get('type')=='intramanager':
                success=valid_intramanager_result(message,request)
                result={key:message.get(key) for key in ['profileName','periodFrom','periodTo','payrollSchema','hours','phoneHours','sickHours','sickPay','cancellations']}
                result.update({'requestId':last_id,'success':success})
            elif request.get('type') == 'probe':
                success = message.get('success') is True and message.get('status') == 'chrome-connected'
                waiting_status = message.get('status') if message.get('status') in {'chrome-sleeping', 'chrome-login-required'} else 'error'
                result = {'requestId':last_id,'success':success,'status':'chrome-connected' if success else waiting_status}
            else:
                success = valid_receipt(message, request.get('registration', {}))
                result = {key:message.get(key) for key in ['requestId','scriptVersion','registrationId','orderNumber','row','status','worksheetName','worksheetId']}
                result['success'] = success
            if request.get('type')=='install': result['diagnostics']=safe_setup_report(message.get('diagnostics'))
            result['background'] = message.get('background') is True
            if not success: result['error'] = str(message.get('error', 'Excel bekræftede ikke salget. Kontrollér masterarket før genforsøg.'))[:500]
            atomic_json(root() / 'response.json', result)


SETUP_PHASES = {'probe','install-open','install-find','install-name','install-write','install-save','install-reopen','install-verify','open','run','parameter','submit','receipt'}
def safe_setup_report(value):
    if not isinstance(value,dict): return {}
    report={'format':1,'kind':'install','extensionVersion':value.get('extensionVersion','') if isinstance(value.get('extensionVersion'),str) and value['extensionVersion'].replace('.','').isdigit() and len(value['extensionVersion'])<=24 else 'unknown'}
    if value.get('scriptVersion')==4: report['scriptVersion']=4
    revision=value.get('scriptRevision')
    if isinstance(revision,str) and len(revision)==10 and all(c in '0123456789abcdef' for c in revision): report['scriptRevision']=revision
    if type(value.get('elapsedMs')) is int: report['elapsedMs']=max(0,min(value['elapsedMs'],3600000))
    report['phase']=value.get('phase') if isinstance(value.get('phase'),str) and value['phase'] in SETUP_PHASES else 'probe'
    report['events']=[]
    for item in value.get('events',[])[:120] if isinstance(value.get('events'),list) else []:
        if not isinstance(item,dict): continue
        event={}
        for key in ['event','phase','outcome','reason']:
            if isinstance(item.get(key),str) and item[key] in SETUP_REPORT_CODES: event[key]=item[key]
        for key in ['active','discarded','frozen','matched','codeMatched','saved','reopened','scriptMatched','defaultTest','attached','disabled','readOnly','runEnabled']:
            if type(item.get(key)) is bool: event[key]=item[key]
        for key in ['ms','lastMs','repeats','frames','office','dialogs','targets','fields','matches','frame','inflight']:
            if type(item.get(key)) is int: event[key]=max(0,min(item[key],3600000))
        report['events'].append(event)
    return report
SETUP_REPORT_CODES = SETUP_PHASES | {'start','tab','navigation','injection','injected','rejected','finished','workbook-frames','office-frames','scan-error','result','error','none','other','permission','frame-gone','context-gone','waiting','parameter-script','parameter-field','parameter-run','parameters','ready','started','filled','refill','submitted','installed','next'}

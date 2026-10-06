"""Read-only Chrome connection prototype. Never accepts sale data or file paths from Chrome."""
import base64
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
import uuid
from contextlib import contextmanager
from urllib.parse import urlparse, parse_qs

HOST = 'dk.provitracker.masterark'
LIMIT = 16384
EXTENSION_FILES = ('manifest.json', 'background.js', 'identity.js', 'probe.js', 'popup.html', 'popup.js')

def version_tuple(value):
    parts = str(value).split('.')
    if not 1 <= len(parts) <= 4 or any(not p.isdigit() or int(p) > 65535 for p in parts):
        raise ValueError('Ugyldigt versionsnummer i Chrome-udvidelsen.')
    return tuple(int(p) for p in parts) + (0,) * (4 - len(parts))

def package_hash(contents):
    digest = hashlib.sha256()
    for name in EXTENSION_FILES:
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
        contents = {name: (folder / 'extension' / name).read_bytes() for name in EXTENSION_FILES}
        manifest = json.loads(contents['manifest.json'])
        if ready.get('version') == manifest['version'] and ready.get('hash') == package_hash(contents):
            return ready
    except (OSError, ValueError, KeyError):
        pass
    return {}

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
        temp.write_text(json.dumps(value), encoding='utf-8')
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
        contents = {name: (extension_dir() / name).read_bytes() for name in EXTENSION_FILES}
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
            for name in [n for n in EXTENSION_FILES if n != 'manifest.json'] + ['manifest.json']:
                target = destination / name
                temp = destination / (name + '.' + uuid.uuid4().hex + '.tmp')
                try:
                    temp.write_bytes(contents[name])
                    temp.replace(target)
                finally:
                    temp.unlink(missing_ok=True)
        atomic_json(root() / 'installed.json', {'version': manifest['version'], 'hash': package_hash(contents)})
        return {'success': True, 'status': 'chrome-prepared', 'updated': changed,
            'extensionPath': str(destination), 'extensionVersion': manifest['version']}

def sync_installed_extension():
    return install_extension(only_existing=True)

def probe(payload):
    url = payload.get('workbookUrl', '')
    if not valid_workbook(url): raise ValueError('Gem et direkte link til masterarket med sourcedoc i adressen.')
    register_host()
    folder = root()
    request_id = str(uuid.uuid4())
    request = folder / 'request.json'
    response = folder / 'response.json'
    with update_lock():
        existing = read_json(request)
        if existing.get('expiresAt', 0) > time.time(): raise RuntimeError('En anden Chrome-kontrol er i gang. Prøv igen om lidt.')
        atomic_json(request, {'type':'probe', 'requestId':request_id, 'workbookUrl':url, 'expiresAt':time.time()+35})
    try:
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            result = read_json(response)
            if result.get('requestId') == request_id: return result
            time.sleep(.2)
        return {'success':False, 'error':'Chrome svarede ikke. Åbn Chrome og masterarket, og klik på Provi Tracker-udvidelsen. Prøv derefter igen.'}
    finally:
        if read_json(request).get('requestId') == request_id: request.unlink(missing_ok=True)
        if read_json(response).get('requestId') == request_id: response.unlink(missing_ok=True)

def native_main(origin):
    if origin != 'chrome-extension://' + extension_id() + '/': return 1
    if sys.platform == 'win32':
        import msvcrt
        msvcrt.setmode(sys.stdin.fileno(), os.O_BINARY)
        msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)
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
            if live and request.get('requestId') != last_id and valid_workbook(request.get('workbookUrl', '')):
                last_id = request['requestId']
                write_frame(sys.stdout.buffer, request)
        elif message.get('type') == 'result' and live and message.get('requestId') == last_id == request.get('requestId'):
            success = message.get('success') is True and message.get('status') == 'chrome-connected'
            atomic_json(root() / 'response.json', {'requestId':last_id, 'success':success,
                'status':'chrome-connected' if success else 'error', 'background':message.get('background') is True,
                'error':str(message.get('error', 'Chrome-kontrollen mislykkedes.'))[:500]})

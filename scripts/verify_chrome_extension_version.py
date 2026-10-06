"""Every extension change needs a higher version so installed workers reload it."""
import json
from pathlib import Path
import re
import subprocess
import sys

def version(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+){0,3}', value):
        raise ValueError('Invalid Chrome extension version')
    parts = tuple(int(p) for p in value.split('.'))
    if any(p > 65535 for p in parts):
        raise ValueError('Invalid Chrome extension version')
    return parts + (0,) * (4-len(parts))

def check(base):
    current = json.loads(Path('chrome_extension/manifest.json').read_text(encoding='utf-8'))['version']
    version(current)
    if not base or set(base) == {'0'}:
        return
    subprocess.run(['git', 'cat-file', '-e', base+'^{commit}'], check=True)
    changed = subprocess.check_output(['git', 'diff', '--name-only', base, 'HEAD', '--', 'chrome_extension/'], text=True)
    if not changed.strip():
        return
    previous = subprocess.run(['git', 'show', base+':chrome_extension/manifest.json'], capture_output=True, text=True)
    if previous.returncode:
        # The extension did not exist before this feature was added.
        return
    before = json.loads(previous.stdout)['version']
    if version(current) <= version(before):
        raise ValueError(f'Chrome extension files changed: bump manifest.version above {before} (currently {current}).')
    print(f'Chrome extension version increased: {before} -> {current}')

if __name__ == '__main__':
    check(sys.argv[1] if len(sys.argv) > 1 else '')

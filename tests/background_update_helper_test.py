"""Run the production PowerShell handoff against a harmless local installer."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def ps_quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def wait_for(predicate, process, description, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        if process.poll() is not None:
            break
        time.sleep(0.05)
    raise AssertionError(description + f'; process exit={process.poll()}')


def run_case(fixture, folder, body, mode):
    folder.mkdir()
    work = folder / 'work'
    current = folder / 'old'
    work.mkdir()
    current.mkdir()
    marker = folder / 'attempt.txt'
    log = folder / 'helper.log'
    dest = folder / 'installed'
    previous = current / 'ProvisionTrackerV2.exe'
    shutil.copyfile(fixture, previous)
    installer = work / 'installer.exe'
    shutil.copyfile(fixture, installer)
    ready, commit, cancel = (work / name for name in ('ready', 'commit', 'cancel'))
    script = work / 'handoff.ps1'

    parent = subprocess.Popen([sys.executable, '-c', 'import sys; sys.stdin.read()'], stdin=subprocess.PIPE)
    helper = other = None
    try:
        script.write_text('\n'.join(
            '$' + k + ' = ' + ps_quote(v) for k, v in {
                'installer': installer, 'targetDir': dest, 'targetApp': dest / previous.name,
                'currentApp': previous, 'markerPath': marker, 'logPath': log,
                'workDir': work, 'readyPath': ready, 'commitPath': commit,
                'cancelPath': cancel, 'expectedVersion': '1.6.0',
            }.items()
        ) + f"""
$parentPid = {parent.pid}
$arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/NOCLOSEAPPLICATIONS')
$arguments += ('/DIR="' + $targetDir + '"')
""" + body, encoding='utf-8-sig')
        env = dict(os.environ)
        if mode == 'failed':
            env['PROVI_PROBE_INSTALL_FAIL'] = '1'
        helper = subprocess.Popen(['powershell.exe', '-NoProfile', '-NonInteractive',
                                   '-ExecutionPolicy', 'Bypass', '-File', str(script)],
                                  env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_for(ready.is_file, helper, f'{mode}: helper did not signal readiness')
        assert previous.is_file() and not (dest / previous.name).exists()
        if mode == 'another-instance':
            other = subprocess.Popen([str(previous), '--hold'])
        if mode in {'approved', 'failed', 'another-instance'}:
            staged = work / 'approval.tmp'
            staged.write_text('ready')
            staged.replace(commit)
            time.sleep(0.5)
            assert parent.poll() is None and not (dest / previous.name).exists(), 'The app must exit before installation'
        elif mode == 'cancelled':
            cancel.write_text('cancel')
            helper.wait(timeout=20)
            assert parent.poll() is None, 'Cancelling must leave the app open'
        parent.stdin.close()
        parent.wait(timeout=10)
        helper.wait(timeout=35)
        if mode == 'approved':
            assert (dest / 'launched.txt').is_file(), log.read_bytes()
            assert 'success' in marker.read_text(encoding='utf-8-sig')
            assert not work.exists()
        elif mode == 'failed':
            assert (current / 'launched.txt').is_file(), log.read_bytes()
            assert 'failed' in marker.read_text(encoding='utf-8-sig') and installer.is_file()
            assert not (dest / 'launched.txt').exists()
        elif mode == 'another-instance':
            assert other.poll() is None, 'An update must not stop another active instance'
            assert 'Skipped' in log.read_text(encoding='utf-8-sig')
            assert not (dest / previous.name).exists()
        else:
            assert not (dest / previous.name).exists() and not marker.exists(), log.read_bytes()
    finally:
        for process in (helper, parent, other):
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()


def main(fixture):
    header = (Path(__file__).resolve().parents[1] / 'updater_script.h').read_text(encoding='utf-8')
    body = header.split('R"PS(', 1)[1].split(')PS"', 1)[0]
    with tempfile.TemporaryDirectory(prefix='provi-background-update-') as root:
        for mode in ('approved', 'cancelled', 'unapproved', 'another-instance', 'failed'):
            run_case(fixture, Path(root) / ("Søren O'Brien " + mode), body, mode)
    print('Windows update helper: waited for approval and exit, left cancelled app open, refused unapproved shutdown, preserved another open instance, and restarted the prior app after installation failure.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--fixture', required=True, type=Path)
    main(parser.parse_args().fixture.resolve())

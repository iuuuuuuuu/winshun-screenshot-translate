"""Optional real Chromium selection regression, entirely on a private desktop.

Pass an installed Electron executable with --electron. No download, clipboard
access, input simulation, foreground switching or user browser profile access.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import uuid

from test_native import NativeProcess, ROOT, api, w


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--electron', type=Path, required=True)
    parser.add_argument('--build', type=Path, default=Path(tempfile.gettempdir())/'winshun-st-native-044')
    parser.add_argument('--output', type=Path, default=ROOT/'.test-output/browser-selection')
    args = parser.parse_args()
    assert args.electron.is_file(), args.electron
    args.output.mkdir(parents=True, exist_ok=True)
    # A fresh directory prevents an old browser.json/ HWND from satisfying readiness.
    out = Path(tempfile.mkdtemp(prefix='chromium-', dir=args.output.resolve()))
    desktop_name = 'WinShun_ST_Probe_' + uuid.uuid4().hex
    desktop = api('user32', 'CreateDesktopW', w.HANDLE, w.LPCWSTR, ctypes.c_void_p,
                  ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p)(desktop_name, None, None, 0, 0x1ff, None)
    if not desktop:
        raise OSError('Private desktop unavailable; no test on user desktop')
    env = dict(os.environ)
    env.pop('ELECTRON_RUN_AS_NODE', None)
    env['WS_APP_DIR'] = str(Path(os.environ['LOCALAPPDATA'])/'Programs/WinShun')
    process = None
    try:
        with (out/'browser.log').open('wb') as log:
            process = NativeProcess([str(args.electron.resolve()), str(Path(__file__).parent/'fixtures/selection-browser.cjs'),
                                     str(out)], env, desktop_name, log)
        start = time.monotonic()
        while not (out/'browser.json').exists() and time.monotonic()-start < 12:
            time.sleep(.1)
        fixture = json.loads((out/'browser.json').read_text())
        with (out/'reader.log').open('wb') as log:
            reader = NativeProcess([str(args.build.resolve()/'ScreenshotTranslate.exe'), '--selection-worker'], env, desktop_name, log)
        start = time.monotonic()
        try:
            reader.stdin.write(json.dumps(dict(target=fixture['target'], x=150, y=180)).encode()+b'\n')
            reader.stdin.close()
            try:
                code = reader.wait(8)
            except subprocess.TimeoutExpired:
                reader.kill(); reader.wait()
                raise
            result = json.loads(reader.stdout.read())
        finally:
            reader.close()
        result.update(passed=code == 0 and result.get('text') == fixture['expected'],
                      ms=round((time.monotonic()-start)*1000), expected=fixture['expected'],
                      testBoundary='Private Electron fixture; not the user Codex selection')
        report = out/'report.json'
        report.write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps(dict(report=str(report), **result)))
        assert result['passed'], result
    finally:
        if process:
            (out/'stop').touch()
            try:
                process.wait(5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
            finally:
                process.close()
        api('user32', 'CloseDesktop', w.BOOL, w.HANDLE)(desktop)


if __name__ == '__main__':
    main()

"""Selection regression using a fresh Firefox profile on a private desktop.

Only a synthetic data: document is loaded. No user browser, clipboard, focus,
profile or accessibility settings are changed. Requires Mozilla geckodriver.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import threading
import urllib.parse
import uuid

import requests
from test_native import NativeProcess, api, w


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firefox', type=Path, default=Path('C:/Program Files/Firefox Developer Edition/firefox.exe'))
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--without-primer', action='store_true', help='Reproduce the cold Firefox accessibility failure')
    args = parser.parse_args()
    api('user32', 'SetProcessDpiAwarenessContext', w.BOOL, w.HANDLE)(ctypes.c_void_p(-4))
    args.output.mkdir(parents=True, exist_ok=True)
    desktop_name = 'WinShun_ST_Probe_' + uuid.uuid4().hex
    desktop = api('user32', 'CreateDesktopW', w.HANDLE, w.LPCWSTR, ctypes.c_void_p,
                  ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p)(desktop_name, None, None, 0, 0x1ff, None)
    if not desktop:
        raise OSError('Private desktop unavailable; no test on user desktop')
    env = dict(os.environ)
    runtime = Path(os.environ['LOCALAPPDATA'])/'Programs/WinShun'
    env.update(WS_APP_DIR=str(runtime), WS_QT_DIR=str(runtime), MOZ_NO_REMOTE='1', ST_SELECTION_TRACE='1')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    http = requests.Session()
    http.trust_env = False
    base = f'http://127.0.0.1:{port}'
    process = None
    primer = None
    session = None
    report = {'testBoundary': 'Private Firefox, fresh profile, synthetic local text only', 'cases': []}
    def request(method, path, body=None):
        response = http.request(method, base+path, json=body, timeout=40)
        response.raise_for_status()
        return response.json().get('value')
    def command(method, path, body=None):
        return request(method, '/session/'+session+path, body)
    try:
        with (args.output/'driver.log').open('wb') as log:
            process = NativeProcess([str(args.driver.resolve()), '--host', '127.0.0.1', '--port', str(port)], env, desktop_name, log)
        deadline = time.monotonic()+10
        while True:
            try:
                request('GET', '/status')
                break
            except requests.RequestException:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.1)
        value = request('POST', '/session', {'capabilities': {'alwaysMatch': {
            'browserName': 'firefox', 'moz:firefoxOptions': {'binary': str(args.firefox), 'args': ['-no-remote']}}}})
        session = value['sessionId']
        report['browserVersion'] = value['capabilities']['browserVersion']
        html = '<title>WinShun isolated selection fixture</title><h1>Selection fixture</h1>' + \
               '<div><section><article><p id="source">Please translate this Firefox selection.</p></article></section></div>' + \
               '<input value="Unrelated focus"><p>QQ</p>'
        command('POST', '/url', {'url': 'data:text/html;charset=utf-8,'+urllib.parse.quote(html)})
        windows = []
        callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        def visit(hwnd, _):
            title = ctypes.create_unicode_buffer(512)
            api('user32', 'GetWindowTextW', ctypes.c_int, w.HWND, w.LPWSTR, ctypes.c_int)(hwnd, title, 512)
            if 'WinShun isolated selection fixture' in title.value:
                windows.append(hwnd)
            return True
        callback = callback_type(visit)
        api('user32', 'EnumDesktopWindows', w.BOOL, w.HANDLE, callback_type, w.LPARAM)(desktop, callback, 0)
        assert len(windows) == 1, windows
        target = windows[0]
        if not args.without_primer:
            with (args.output/'primer.log').open('wb') as log:
                primer = NativeProcess([str(args.build.resolve()/'ScreenshotTranslate.exe'), '--selection-worker'], env, desktop_name, log)
            primer.stdin.write(json.dumps({'prime': True, 'target': str(target)}).encode()+b'\n')
            ready = threading.Event()
            def drain_primer():
                for line in primer.stdout:
                    if json.loads(line).get('ready'):
                        ready.set()
            threading.Thread(target=drain_primer, daemon=True).start()
            assert ready.wait(5), 'Firefox accessibility primer did not become ready'
        report['primerEnabled'] = primer is not None
        for name, x, y in [('primary-cold', 100, 100), ('primary-retry', 100, 100), ('primary-reselect', 100, 100),
                            ('secondary', 2700, 100), ('secondary-repeat', 2700, 100),
                            ('long-scrolled', 100, 100), ('long-focus-restored', 100, 100),
                            ('long-over-100k', 100, 100)]:
            command('POST', '/window/rect', {'x': x, 'y': y, 'width': 800, 'height': 500})
            expected = 'Please translate this Firefox selection.'
            if name != 'primary-retry':
                expected = command('POST', '/execute/sync', {'script': '''
                document.querySelector('input').focus();
                const range = document.createRange();
                range.selectNodeContents(document.getElementById('source'));
                const selection = window.getSelection();
                selection.removeAllRanges(); selection.addRange(range);
                return selection.toString();''', 'args': []})
            if name.startswith('long-'):
                expected = command('POST', '/execute/sync', {'script': r'''
                const source = document.getElementById('source');
                document.activeElement.blur();
                source.style.whiteSpace = 'pre-wrap';
                source.textContent = Array.from({length: arguments[0]}, (_, i) =>
                    'Line ' + i + ': Cross-page selected text must remain readable after scrolling and changing focus.').join('\n');
                const range = document.createRange(); range.selectNodeContents(source);
                const selection = window.getSelection(); selection.removeAllRanges(); selection.addRange(range);
                window.scrollTo(0, document.body.scrollHeight);
                return selection.toString();''', 'args': [1600 if name == 'long-over-100k' else 150]})
                if name == 'long-focus-restored':
                    original = command('GET', '/window')
                    other = command('POST', '/window/new', {'type': 'window'})['handle']
                    command('POST', '/window', {'handle': other})
                    command('POST', '/url', {'url': 'data:text/html,<title>Temporary focus fixture</title>Other fixture'})
                    command('DELETE', '/window')
                    command('POST', '/window', {'handle': original})
            expected = command('POST', '/execute/sync', {'script': 'return window.getSelection().toString()', 'args': []})
            assert expected, 'Browser itself cleared the fixture selection'
            expected = expected.replace('\r\n', '\n')
            rect = w.RECT()
            api('user32', 'GetWindowRect', w.BOOL, w.HWND, ctypes.POINTER(w.RECT))(target, ctypes.byref(rect))
            with (args.output/(name+'.log')).open('wb') as log:
                reader = NativeProcess([str(args.build.resolve()/'ScreenshotTranslate.exe'), '--selection-worker'], env, desktop_name, log)
            started = time.monotonic()
            output = []
            drain = threading.Thread(target=lambda: output.append(reader.stdout.read()), daemon=True)
            drain.start()
            try:
                reader.stdin.write(json.dumps({'target': str(target), 'x': rect.left+150, 'y': rect.top+180}).encode()+b'\n')
                reader.stdin.close()
                try:
                    code = reader.wait(8)
                except subprocess.TimeoutExpired:
                    reader.kill(); reader.wait()
                    raise
                drain.join(3)
                assert not drain.is_alive(), 'Selection output pipe did not close'
                result = json.loads(b''.join(output))
            finally:
                reader.close()
            result.update(name=name, passed=code == 0 and result.get('text', '').strip() == expected.strip(),
                          expectedLength=len(expected),
                          ms=round((time.monotonic()-started)*1000),
                          window=[rect.left, rect.top, rect.right, rect.bottom])
            if not result['passed']:
                actual = result.get('text', '')
                mismatch = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), min(len(actual), len(expected)))
                result['difference'] = {'offset': mismatch, 'actual': actual[max(0, mismatch-30):mismatch+50],
                                        'expected': expected[max(0, mismatch-30):mismatch+50]}
            report['cases'].append(result)
            print(json.dumps({k: v for k, v in result.items() if k != 'text'} | {'textLength': len(result.get('text', ''))}), flush=True)
        report['passed'] = all(case['passed'] for case in report['cases'])
        if primer:
            primer.stdin.close()
            report['primerExitedOnParentClose'] = primer.wait(3) == 0
            assert report['primerExitedOnParentClose']
        assert report['passed'], report
    finally:
        (args.output/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        if primer:
            primer.stdin.close()
            try:
                primer.wait(3)
            except subprocess.TimeoutExpired:
                primer.kill(); primer.wait()
            primer.close()
        if session:
            try:
                command('DELETE', '')
            except requests.RequestException:
                pass
        if process:
            process.kill(); process.wait(); process.close()
        http.close()
        api('user32', 'CloseDesktop', w.BOOL, w.HANDLE)(desktop)


if __name__ == '__main__':
    main()

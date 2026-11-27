"""Selection regression using a fresh Firefox profile on a private desktop.

Only a synthetic data: document is loaded. No user browser, focus,
profile or accessibility settings are changed. Requires Mozilla geckodriver.
With --native-copy or --icon-click, explicit native copy briefly uses the
clipboard and restores it; it never seeds or clears the user's existing data.
"""
import argparse
import ctypes
from http.server import ThreadingHTTPServer
import json
import hashlib
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
from test_native import NativeProcess, Server, api, w


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firefox', type=Path, default=Path('C:/Program Files/Firefox Developer Edition/firefox.exe'))
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/WinShun')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--without-primer', action='store_true', help='Reproduce the cold Firefox accessibility failure')
    parser.add_argument('--icon-click', action='store_true', help='Verify the actual QML icon refreshes a late Firefox selection')
    parser.add_argument('--uia-mode', type=int, choices=(0,1,2), help='Firefox UIA mode in the temporary test profile only')
    parser.add_argument('--case', action='append', help='Run only named selection cases')
    parser.add_argument('--native-copy',action='store_true',help='Explicit Firefox native copy; preserves and restores the existing clipboard')
    args = parser.parse_args()
    api('user32', 'SetProcessDpiAwarenessContext', w.BOOL, w.HANDLE)(ctypes.c_void_p(-4))
    args.output.mkdir(parents=True, exist_ok=True)
    desktop_name = 'WinShun_ST_Probe_' + uuid.uuid4().hex
    desktop = api('user32', 'CreateDesktopW', w.HANDLE, w.LPCWSTR, ctypes.c_void_p,
                  ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p)(desktop_name, None, None, 0, 0x1ff, None)
    if not desktop:
        raise OSError('Private desktop unavailable; no test on user desktop')
    env = dict(os.environ)
    runtime = args.runtime.resolve()
    env.update(WS_APP_DIR=str(runtime), WS_QT_DIR=str(runtime), MOZ_NO_REMOTE='1', ST_SELECTION_TRACE='1', MOZ_REMOTE_ALLOW_SYSTEM_ACCESS='1')
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
    def clipboard_hashes():
        # Read-only signatures. Do not print or save the original clipboard text.
        if not api('user32','OpenClipboard',w.BOOL,w.HWND)(None):return None
        values={}
        try:
            format=0
            while True:
                format=api('user32','EnumClipboardFormats',w.UINT,w.UINT)(format)
                if not format:break
                # These clipboard handles are GDI objects, not byte buffers.
                if format in (2,3,9,14,0x82,0x83,0x8e):continue
                data=api('user32','GetClipboardData',w.HANDLE,w.UINT)(format)
                size=api('kernel32','GlobalSize',ctypes.c_size_t,w.HANDLE)(data)
                pointer=api('kernel32','GlobalLock',ctypes.c_void_p,w.HANDLE)(data)
                if pointer:
                    values[str(format)]=hashlib.sha256(ctypes.string_at(pointer,size)).hexdigest()
                    api('kernel32','GlobalUnlock',w.BOOL,w.HANDLE)(data)
            return values
        finally:api('user32','CloseClipboard',w.BOOL)()
    clipboard_before=clipboard_hashes() if args.native_copy or args.icon_click else None
    def request(method, path, body=None):
        response = http.request(method, base+path, json=body, timeout=40)
        if not response.ok: print(response.text,flush=True)
        response.raise_for_status()
        return response.json().get('value')
    def command(method, path, body=None):
        return request(method, '/session/'+session+path, body)
    def drag_shadow(name):
        endpoints=command('POST','/execute/sync',{'script': '''
        document.activeElement.blur();
        const old=document.getElementById('source'), source=document.createElement('div');source.id='source';old.replaceWith(source);
        source.innerHTML=' <a href="#author">iuuuuuuuu</a> released this <relative-time></relative-time> UNSELECTED AFTER';
        source.querySelector('relative-time').attachShadow({mode:'open'}).innerHTML='<span>1 hour ago</span>';
        window.getSelection().removeAllRanges();window.scrollTo(0,0);
        const a=source.querySelector('a').getBoundingClientRect(), b=source.querySelector('relative-time').getBoundingClientRect();
        const points=[[a.left-1,a.top+a.height/2],[b.right-1,b.top+b.height/2]];
        return arguments[0].endsWith('reverse')?points.reverse():points;
        ''','args':[name]})
        command('POST','/moz/context',{'context':'chrome'})
        try:
            snapshot=command('POST','/execute/async',{'script':r'''
            const done=arguments[arguments.length-1], points=arguments[0], mm=window.gBrowser.selectedBrowser.messageManager;
            const listener=m=>{mm.removeMessageListener('ST:selection',listener);done(m.data);};
            mm.addMessageListener('ST:selection',listener);
            const script=`
              try {
                const [a,b]=${JSON.stringify(points)};
                content.document.addEventListener('dragstart',e=>e.preventDefault());
                content.synthesizeMouseEvent('mousedown',a[0],a[1],{button:0,clickCount:1,inputSource:1});
                for(let i=1;i<=20;i++)content.synthesizeMouseEvent('mousemove',a[0]+(b[0]-a[0])*i/20,a[1]+(b[1]-a[1])*i/20,{button:0,buttons:1,clickCount:0,inputSource:1},{isDOMEventSynthesized:false});
                content.synthesizeMouseEvent('mouseup',b[0],b[1],{button:0,clickCount:1,inputSource:1});
                const s=content.getSelection(),time=content.document.querySelector('relative-time');
                sendAsyncMessage('ST:selection',{legacyText:s.toString(),ranges:s.getComposedRanges({shadowRoots:[time.shadowRoot]}).map(r=>({start:r.startContainer.nodeValue,startOffset:r.startOffset,end:r.endContainer.nodeValue,endOffset:r.endOffset}))});
              }catch(e){sendAsyncMessage('ST:selection',{error:String(e)});}
            `;
            mm.loadFrameScript('data:application/javascript,'+encodeURIComponent(script),false);
            ''','args':[endpoints]})
            assert 'error' not in snapshot,snapshot
            (args.output/(name+'-dom.json')).write_text(json.dumps(snapshot,indent=2),encoding='utf-8')
        finally:command('POST','/moz/context',{'context':'content'})
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
        options={'binary':str(args.firefox),'args':['-no-remote']}
        if args.uia_mode is not None:options['prefs']={'accessibility.uia.enable':args.uia_mode}
        value = request('POST', '/session', {'capabilities': {'alwaysMatch': {
            'browserName': 'firefox', 'moz:firefoxOptions': options}}})
        session = value['sessionId']
        report['browserVersion'] = value['capabilities']['browserVersion']
        test_profile=Path(value['capabilities']['moz:profile'])
        report['uiaPreference']={name:[line for line in (test_profile/name).read_text(encoding='utf-8').splitlines() if 'accessibility.uia.' in line]
            for name in ('user.js','prefs.js') if (test_profile/name).exists()}
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
                            ('inline-release', 100, 100), ('inline-reverse', 100, 100),
                            ('inline-partial', 100, 100), ('inline-indented', 100, 100),
                            ('inline-shadow', 100, 100), ('inline-shadow-reverse', 100, 100),
                            ('inline-avatar', 100, 100), ('inline-avatar-partial', 100, 100),
                            ('inline-github', 100, 100), ('inline-github-no-avatar', 100, 100),
                            ('inline-links-partial', 100, 100), ('inline-links-reverse', 100, 100),
                            ('inline-literal-object', 100, 100),
                            ('blocks-partial',100,100),('blocks-full',100,100),
                            ('mouse-shadow-forward',100,100),('mouse-shadow-reverse',100,100),
                            ('long-scrolled', 100, 100), ('long-focus-restored', 100, 100),
                            ('long-over-100k', 100, 100)]:
            if args.case and name not in args.case:continue
            if name.startswith('mouse-shadow') and not args.native_copy and not args.case:continue
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
            if name.startswith('inline-'):
                expected = command('POST', '/execute/sync', {'script': r'''
                const source = document.getElementById('source');
                source.innerHTML = '<a href="#author">iuuuuuuuu</a> released this <relative-time>17 minutes ago</relative-time>';
                if(arguments[0].startsWith('inline-links'))
                    source.innerHTML = '<a href="#author">iuuuuuuuu</a> released this <relative-time><a href="#time">1 hour ago</a></relative-time> UNSELECTED';
                if(arguments[0] === 'inline-literal-object')
                    source.innerHTML = '<a href="#author">Literal</a> \uFFFC <relative-time>text 😀 中文</relative-time>';
                if(arguments[0].startsWith('inline-avatar')) {
                    source.insertAdjacentHTML('afterbegin',"<img alt='@iuuuuuuuu' width='20' height='20'> ");
                    source.firstChild.src=URL.createObjectURL(new Blob(['<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><circle cx="10" cy="10" r="10" fill="blue"/></svg>'],{type:'image/svg+xml'}));
                }
                if(arguments[0] === 'inline-indented' || arguments[0].startsWith('inline-shadow'))
                    source.innerHTML = '\n      <a href="#author">iuuuuuuuu</a>\n\n    released this\n\n      <relative-time>\n        27 Nov 11:09\n      </relative-time>\n  ';
                if(arguments[0].startsWith('inline-shadow')) {
                    const time = source.querySelector('relative-time');
                    time.textContent = '17 minutes ago';
                    time.attachShadow({mode:'open'}).innerHTML = '<span><slot></slot></span>';
                }
                const selection = window.getSelection(); selection.removeAllRanges();
                const first = source.querySelector('a').firstChild;
                const time = source.querySelector('relative-time');
                const last = time.querySelector('a')?.firstChild || time.firstChild;
                if(arguments[0].endsWith('reverse')) selection.setBaseAndExtent(last, last.length, first, 0);
                else if(arguments[0].endsWith('partial')) selection.setBaseAndExtent(first, 3, last, 7);
                else if(arguments[0] === 'inline-avatar') {const range=document.createRange();range.selectNodeContents(source);selection.addRange(range);}
                else selection.setBaseAndExtent(first, 0, last, last.length);
                return selection.toString();''', 'args': [name]})
            if name.startswith('inline-github'):
                command('POST','/execute/sync',{'script':r'''
                const old=document.getElementById('source');const source=document.createElement('div');source.id='source';old.replaceWith(source);
                source.innerHTML=`
      <img alt="@iuuuuuuuu" width="20" height="20">
      <a href="#author">iuuuuuuuu</a>

    released this

      <relative-time class="no-wrap" prefix="" datetime="2026-11-27T11:09:42Z">
        27 Nov 11:09
      </relative-time>
  `;
                source.querySelector('img').src=URL.createObjectURL(new Blob(['<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20"><circle cx="10" cy="10" r="10" fill="blue"/></svg>'],{type:'image/svg+xml'}));
                const time=source.querySelector('relative-time');time.style.whiteSpace='nowrap';time.attachShadow({mode:'open'}).innerHTML='<span part="root">1 hour ago</span>';
                const selection=window.getSelection();selection.removeAllRanges();const range=document.createRange();
                if(arguments[0].endsWith('no-avatar'))range.setStart(source.querySelector('a').firstChild,0);
                else range.setStart(source,0);
                range.setEndAfter(time);selection.addRange(range);
                ''','args':[name]})
                snapshot=command('POST','/execute/sync',{'script': '''
                const s=window.getSelection();return {text:s.toString(),anchor:s.anchorNode?.nodeValue,anchorOffset:s.anchorOffset,focus:s.focusNode?.nodeValue,focusOffset:s.focusOffset};''','args':[]})
                (args.output/(name+'-dom.json')).write_text(json.dumps(snapshot,indent=2),encoding='utf-8')
            if name.startswith('blocks-'):
                command('POST','/execute/sync',{'script': '''
                const old=document.getElementById('source'), source=document.createElement('div');source.id='source';old.replaceWith(source);
                source.innerHTML='<p>UNSELECTED BEFORE</p><p id="first">First 😀 <a href="#one">linked words</a>.</p><p id="second">Second <a href="#two">paragraph</a> tail.</p><p>UNSELECTED AFTER</p>';
                const s=window.getSelection(), first=source.querySelector('#first').firstChild;
                if(arguments[0]==='blocks-partial')s.setBaseAndExtent(first,6,source.querySelector('#second a').firstChild,4);
                else s.setBaseAndExtent(first,0,source.querySelector('#second').lastChild,6);
                ''','args':[name]})
            if name.startswith('mouse-shadow'):drag_shadow(name)
            expected = command('POST', '/execute/sync', {'script': 'return window.getSelection().toString()', 'args': []})
            if name.startswith('mouse-shadow'):expected='iuuuuuuuu released this 1 hour ago'
            if name.startswith('inline-github'):expected='iuuuuuuuu released this 1 hour ago'
            if name=='inline-avatar':expected='iuuuuuuuu released this 17 minutes ago'
            assert expected, 'Browser itself cleared the fixture selection'
            expected = expected.replace('\r\n', '\n')
            if args.native_copy:command('POST','/execute/sync',{'script':'document.activeElement.blur();','args':[]})
            rect = w.RECT()
            api('user32', 'GetWindowRect', w.BOOL, w.HWND, ctypes.POINTER(w.RECT))(target, ctypes.byref(rect))
            with (args.output/(name+'.log')).open('wb') as log:
                reader = NativeProcess([str(args.build.resolve()/'ScreenshotTranslate.exe'), '--selection-worker'], env, desktop_name, log)
            started = time.monotonic()
            output = []
            drain = threading.Thread(target=lambda: output.append(reader.stdout.read()), daemon=True)
            drain.start()
            try:
                reader.stdin.write(json.dumps({'target': str(target), 'x': rect.left+150, 'y': rect.top+180,'copyFirefox':args.native_copy}).encode()+b'\n')
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
        if args.icon_click:
            for reverse in (False, True):
                icon_out = args.output.resolve()/('icon-reverse' if reverse else 'icon-forward')
                icon_out.mkdir(exist_ok=True)
                # Model the accessibility tree publishing the drag's final
                # endpoint after the first non-empty selection was captured.
                initial = command('POST', '/execute/sync', {'script': '''
                const source=document.getElementById('source'); source.style.whiteSpace='normal';
                source.innerHTML='<a href="#author">iuuuuuuuu</a> released this <relative-time>17 minutes ago</relative-time>';
                window.scrollTo(0,0); const selection=window.getSelection();
                selection.setBaseAndExtent(source.firstChild.firstChild,0,source.childNodes[1],10);
                return selection.toString();''', 'args': []})
                server=ThreadingHTTPServer(('127.0.0.1',0),Server)
                server.daemon_threads=True
                threading.Thread(target=server.serve_forever,daemon=True).start()
                try:
                    with tempfile.TemporaryDirectory(prefix='FirefoxIconQA-') as temporary:
                        data=Path(temporary)
                        (data/'services.json').write_text(json.dumps({'version':2,'profiles':{
                            'google':{'endpoint':f'http://127.0.0.1:{server.server_port}/google'}}}),encoding='utf-8')
                        icon_env=dict(env,QT_QUICK_BACKEND='software',WS_DATA_DIR=str(data))
                        for key in ('HTTPS_PROXY','HTTP_PROXY','ALL_PROXY','OPENAPI_TRANSLATE_PROXY'):
                            icon_env.pop(key,None)
                        with (icon_out/'stderr.log').open('wb') as log:
                            probe=NativeProcess([str(args.build.resolve()/'ScreenshotTranslate.exe'),'--probe'],icon_env,desktop_name,log)
                        chunks=[]
                        drain=threading.Thread(target=lambda:chunks.append(probe.stdout.read()),daemon=True);drain.start()
                        try:
                            params={'dataDir':str(data),'probeOutput':str(icon_out),'probeRuntime':str(runtime),
                                'locale':'zh','appearance':{'theme':'light','renderer':'software'},
                                'settings':{'provider':'google','fallbackEnabled':False},'capabilities':{'hostHotkeys':True},
                                'probeFirefoxTarget':str(target),'probeInitialSelection':initial,
                                'probeFinalSelection':'iuuuuuuuu released this 1 hour ago' if args.native_copy else 'iuuuuuuuu released this 17 minutes ago'}
                            probe.stdin.write(json.dumps({'jsonrpc':'2.0','id':1,'method':'initialize','params':params}).encode()+b'\n')
                            probe.stdin.flush()
                            deadline=time.monotonic()+25
                            while not (icon_out/'icon-ready.json').exists():
                                assert time.monotonic()<deadline, 'Native Firefox icon did not appear'
                                time.sleep(.05)
                            if args.native_copy:
                                drag_shadow('mouse-shadow-reverse' if reverse else 'mouse-shadow-forward')
                                final=params['probeFinalSelection']
                            else:
                                final=command('POST','/execute/sync',{'script': '''
                                const source=document.getElementById('source'), first=source.firstChild.firstChild,
                                    last=source.querySelector('relative-time').firstChild, selection=window.getSelection();
                                if(arguments[0]) selection.setBaseAndExtent(last,last.length,first,0);
                                else selection.setBaseAndExtent(first,0,last,last.length);
                                return selection.toString();''','args':[reverse]})
                                assert final==params['probeFinalSelection']
                            (icon_out/'click-ready').touch()
                            code=probe.wait(15);drain.join(3)
                            result=json.loads((icon_out/'report.json').read_text(encoding='utf-8'))
                            assert code==0 and result['passed'],result.get('error')
                            report.setdefault('iconCases',[]).append({'reverse':reverse,'passed':True,
                                'initialLength':len(initial),'finalLength':len(final)})
                        finally:
                            probe.stdin.close()
                            try: probe.wait(2)
                            except subprocess.TimeoutExpired: probe.kill();probe.wait()
                            drain.join(3);probe.close()
                            (icon_out/'stdout.jsonl').write_bytes(b''.join(chunks))
                finally:
                    server.shutdown();server.server_close()
        report['passed'] = all(case['passed'] for case in report['cases'])
        if clipboard_before is not None:
            after=clipboard_hashes()
            report['clipboardPreserved']=after is not None and all(after.get(k)==v for k,v in clipboard_before.items())
            assert report['clipboardPreserved'],'Clipboard changed during copy test; inspect whether a concurrent user copy occurred'
        if primer:
            primer.stdin.close()
            report['primerExitedOnParentClose'] = primer.wait(3) == 0
            assert report['primerExitedOnParentClose']
        assert report['passed'], [{k: c[k] for k in ('name', 'difference') if k in c} for c in report['cases'] if not c['passed']]
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

"""Real Chromium passive-selection regression; private desktop and synthetic data only."""
import argparse
import ctypes
import json
import os
import subprocess
from pathlib import Path
import tempfile
import threading
import time
import uuid
from test_native import NativeProcess, ROOT, api, w


def main():
    api('user32','SetProcessDpiAwarenessContext',w.BOOL,w.HANDLE)(ctypes.c_void_p(-4))
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--electron',type=Path,required=True)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--mode',action='append',choices=('forward','reverse','none','hidden','long'))
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='passive-',dir=args.output.resolve()))
    desktop_name='WinShun_ST_Probe_'+uuid.uuid4().hex
    desktop=api('user32','CreateDesktopW',w.HANDLE,w.LPCWSTR,ctypes.c_void_p,ctypes.c_void_p,w.DWORD,w.DWORD,ctypes.c_void_p)(desktop_name,None,None,0,0x1ff,None)
    assert desktop,'Private test desktop unavailable'
    env=dict(os.environ,WS_APP_DIR=str(args.runtime.resolve()),WS_QT_DIR=str(args.runtime.resolve()),ST_SELECTION_TRACE='1')
    env.pop('ELECTRON_RUN_AS_NODE',None)
    process=None;report={'passed':False,'cases':[],'clipboardAccess':False}
    try:
        with (out/'browser.log').open('wb') as log:
            process=NativeProcess([str(args.electron.resolve()),str(ROOT/'native/fixtures/selection-passive.cjs'),str(out)],env,desktop_name,log)
        deadline=time.monotonic()+20
        while not (out/'ready').exists():
            assert time.monotonic()<deadline,'Chromium fixture readiness timeout'
            time.sleep(.05)
        for index,mode in enumerate(args.mode or ('forward','reverse','none','hidden','long')):
            (out/'request.json').write_text(json.dumps({'id':index,'mode':mode}),encoding='utf-8')
            deadline=time.monotonic()+10
            while True:
                try:fixture=json.loads((out/'response.json').read_text())
                except (OSError,json.JSONDecodeError):fixture={}
                if fixture.get('id')==index:break
                assert time.monotonic()<deadline,'Fixture response timeout'
                time.sleep(.05)
            time.sleep(.15)
            cases=[(name,True) for name in fixture['points']]
            if mode=='forward':cases.append(('terminal',False))
            for name,passive in cases:
                p=fixture['points'][name];expected=fixture['expected'] if name=='text' or not passive else ''
                with (out/f'{mode}-{name}-{passive}.log').open('wb') as log:
                    reader=NativeProcess([str(args.build.resolve()/'ScreenshotTranslate.exe'),'--selection-worker'],env,desktop_name,log)
                chunks=[];drain=threading.Thread(target=lambda:chunks.append(reader.stdout.read()),daemon=True);drain.start()
                try:
                    reader.stdin.write(json.dumps({'target':fixture['target'],'x':p[0],
                        'y':p[1],'passive':passive}).encode()+b'\n');reader.stdin.close()
                    try:
                        code=reader.wait(8)
                    except subprocess.TimeoutExpired:
                        reader.kill();reader.wait()
                        raise
                    drain.join(3)
                    assert not drain.is_alive(),'Selection output pipe did not close'
                    result=json.loads(b''.join(chunks))
                finally:
                    reader.close()
                row={'mode':mode,'point':name,'passive':passive,'expectedChars':len(expected),'actualChars':len(result.get('text','')),
                    'passed':code==0 and result.get('text')==expected}
                report['cases'].append(row);print(json.dumps(row),flush=True)
        report['passed']=all(row['passed'] for row in report['cases'])
        assert report['passed'],'Passive selection regression failed'
    finally:
        (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        if process:
            (out/'stop').touch()
            try:process.wait(5)
            except Exception:process.kill();process.wait()
            process.close()
        api('user32','CloseDesktop',w.BOOL,w.HANDLE)(desktop)


if __name__=='__main__':main()

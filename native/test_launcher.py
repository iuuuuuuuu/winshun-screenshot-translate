"""Launcher rejection checks: invalid runtime must fail before opening any UI."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--incompatible',type=Path,required=True)
a=p.parse_args()
exe=(a.build/'ScreenshotTranslate.exe').resolve()
with tempfile.TemporaryDirectory(prefix='st-launcher-') as empty:
    cases=[('missing',None,b'WS_QT_DIR'),('relative','runtime',b'WS_QT_DIR'),
           ('path-list',empty+';C:\\Windows',b'WS_QT_DIR'),
           ('incomplete',empty,b'Qt 6.12.x'),
           ('incompatible',str(a.incompatible.resolve()),b'Qt 6.12.x')]
    results={}
    for name,runtime,message in cases:
        env=dict(os.environ)
        env.pop('WS_QT_DIR',None)
        if runtime is not None:env['WS_QT_DIR']=runtime
        done=subprocess.run([str(exe)],env=env,input=b'',capture_output=True,timeout=5,
                            creationflags=subprocess.CREATE_NO_WINDOW)
        assert done.returncode==2 and message in done.stderr,(name,done.returncode,done.stderr)
        results[name]=True
print(json.dumps(dict(passed=True,cases=results)))

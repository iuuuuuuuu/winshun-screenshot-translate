"""Release gate using a disposable GitHub Windows VM and synthetic text only."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--qt-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get('GITHUB_ACTIONS') == 'true' and os.environ.get('RUNNER_ENVIRONMENT') == 'github-hosted'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    runtime = out/'runtime'
    runtime.mkdir()
    for dll in (args.qt_root/'bin').glob('*.dll'):
        shutil.copy2(dll, runtime/dll.name)
    shutil.copytree(args.qt_root/'plugins/platforms', runtime/'platforms')
    shutil.copytree(args.qt_root/'qml', runtime/'qml')
    env = dict(os.environ, WS_APP_DIR=str(runtime), WS_QT_DIR=str(runtime),
               QT_PLUGIN_PATH=str(runtime), QML_IMPORT_PATH=str(runtime/'qml'), QT_QUICK_BACKEND='software')
    env['PATH'] = str(runtime)+os.pathsep+env['PATH']
    # A real input-desktop copy test is permitted only on this disposable VM.
    result = subprocess.run([str(args.build.resolve()/'native_tests.exe'), '--selection-release'],
                            env=env, capture_output=True, timeout=45)
    (out/'clipboard.stdout.json').write_bytes(result.stdout)
    (out/'clipboard.stderr.log').write_bytes(result.stderr)
    assert result.returncode == 0, result.stderr.decode('utf-8', errors='replace')
    report = json.loads(result.stdout)
    assert all(report.values()), report
    # Also run real native EDIT/long-text and restoration on a private desktop.
    command = [sys.executable, '-X', 'utf8', '-B', 'native/test_native.py', '--build', str(args.build.resolve()),
               '--runtime', str(runtime), '--selection-copy', '--output', str(out/'native')]
    subprocess.run(command, check=True, env=env)
    native = json.loads((out/'native/stdout.jsonl').read_text(encoding='utf-8'))
    assert native['passed'] and native['nativeClipboardTest'], native
    (out/'report.json').write_text(json.dumps({'passed': True, 'clipboard': report, 'native': native}, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'runtime': str(runtime)}))


if __name__ == '__main__':
    main()

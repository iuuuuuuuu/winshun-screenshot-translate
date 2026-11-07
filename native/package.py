"""生成包含独立 Qt 运行库的原生安装包；Python 仅用于开发和发布。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
import zipfile


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path(tempfile.gettempdir())/'winshun-st-native-040')
    parser.add_argument('--qt-root', type=Path, default=root/'.host/qt/6.10.0/msvc2022_64')
    parser.add_argument('--vs-root', type=Path, default=Path('D:/VisualStudio/Product'))
    parser.add_argument('--out', type=Path)
    parser.add_argument('--allow-dirty', action='store_true', help='仅本地验证允许未提交源码')
    args = parser.parse_args()
    subprocess.run([sys.executable, '-B', str(root/'native/build_ecdict.py')], check=True)
    manifest = json.loads((root/'plugin.json').read_text(encoding='utf-8'))
    version, plugin_id = manifest['version'], manifest['id']
    assert manifest['runtime']['program'] == 'ScreenshotTranslate.exe'
    assert manifest['runtime'].get('qt') != 'host', '管理员插件必须自带运行库'
    assert 'elevated' in manifest['permissions']
    status = subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()
    dirty = bool(status)
    if dirty and not args.allow_dirty:
        parser.error('发布前请提交源码；本地验证可加 --allow-dirty\n' + status)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    out = (args.out or root/f'dist/release-{version}').resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Inherit the output directory's ACL so restricted-medium QA can also read
    # the staged package when the developer shell is elevated.
    stage = out/('stage-' + uuid.uuid4().hex)
    stage.mkdir()
    runtime = stage/'runtime'
    runtime.mkdir()
    for name in ('ScreenshotTranslate.exe', 'ScreenshotTranslateApp.dll'):
        shutil.copy2(args.build/name, stage/name)
    for name in ('plugin.json', 'README.md', 'CHANGELOG.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copy2(root/name, stage/name)
    shutil.copytree(root/'native/licenses', stage/'licenses')
    shutil.copytree(root/'data', stage/'data')
    (stage/'native').mkdir()
    shutil.copy2(root/'native/README.md', stage/'native/README.md')
    (stage/'.screenshots').mkdir()
    for name in ('word-0.4.3.png', 'settings-0.4.3.png', 'toolbar-0.4.3.png'):
        shutil.copy2(root/'.screenshots'/name, stage/'.screenshots'/name)
    (runtime/'fonts').mkdir()
    shutil.copy2(root/'native/assets/AlibabaPuHuiTi-3-55-Regular.ttf', runtime/'fonts/AlibabaPuHuiTi-3-55-Regular.ttf')
    shutil.copy2(root/'native/assets/Alibaba-LICENSE.txt', stage/'licenses/Alibaba-LICENSE.txt')
    env = dict(os.environ)
    env['PATH'] = str(args.qt_root.resolve()/'bin') + os.pathsep + env.get('PATH', '')
    subprocess.run([str(args.qt_root/'bin/windeployqt.exe'), '--release', '--dir', str(runtime),
                    '--qmldir', str(root/'native/qml'), '--no-translations', '--no-compiler-runtime',
                    '--skip-plugin-types', 'qmltooling,generic', '--no-system-d3d-compiler',
                    '--no-system-dxc-compiler', str(stage/'ScreenshotTranslateApp.dll')], check=True, env=env)
    crt_dirs = sorted((args.vs_root/'VC/Redist/MSVC').glob('*/x64/Microsoft.VC*.CRT'))
    if not crt_dirs:
        raise RuntimeError('未找到 Visual C++ x64 可分发运行库')
    for file in crt_dirs[-1].glob('*.dll'):
        shutil.copy2(file, runtime/file.name)
    for module in ('qtbase', 'qtdeclarative', 'qtsvg'):
        shutil.copy2(args.qt_root/f'sbom/{module}-6.10.0.spdx.json', stage/f'licenses/{module}-6.10.0.spdx.json')
    (stage/'BUILD-INFO.json').write_text(json.dumps({
        'version': version, 'sourceCommit': commit, 'sourceDirty': dirty,
        'sourceUrl': f'https://github.com/iuuuuuuuu/winshun-screenshot-translate/tree/{commit}',
        'qtVersion': '6.10.0', 'qtBundled': True, 'pythonRequired': False,
        'buildWorkflow': '原生构建与打包', 'runtime': 'Windows x64 / MSVC / Qt 6.10.0',
    }, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    files = {p.relative_to(stage).as_posix(): p for p in stage.rglob('*') if p.is_file()}
    oversized = [name for name, path in files.items() if path.stat().st_size > 64*1024*1024]
    assert not oversized, '超过插件库单文件大小限制：' + ', '.join(oversized)
    total = sum(p.stat().st_size for p in files.values())
    assert total <= 256*1024*1024, '超过插件库解压大小限制'
    package = out/f'{plugin_id}-{version}.wsplugin'
    with zipfile.ZipFile(package, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, file in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 11, 7, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, file.read_bytes(), compresslevel=9)
    sha = hashlib.sha256(package.read_bytes()).hexdigest()
    assert package.stat().st_size <= 64*1024*1024, '超过插件库压缩包大小限制'
    package.with_suffix(package.suffix+'.sha256').write_text(sha+'  '+package.name+'\n', encoding='utf-8')
    report = {'package': str(package), 'stage': str(stage), 'sha256': sha, 'size': package.stat().st_size,
              'unpackedSize': total, 'version': version, 'sourceCommit': commit, 'sourceDirty': dirty,
              'runtime': 'native-x64', 'pythonRequired': False, 'qtBundled': True,
              'files': list(files), 'fileHashes': {n: hashlib.sha256(p.read_bytes()).hexdigest() for n,p in files.items()}}
    (out/'package.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('files','fileHashes')}, ensure_ascii=True))


if __name__ == '__main__':
    main()

"""Install a byte-verified native candidate after QA, backing up the old entry."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import time
import zipfile

root=Path(__file__).resolve().parents[1]
folder=root/"dist/native-0.4.0"
report=json.loads((folder/"package.json").read_text(encoding="utf-8"))
package=Path(report["package"])
assert hashlib.sha256(package.read_bytes()).hexdigest()==report["sha256"]
installed=Path(os.environ["LOCALAPPDATA"])/"WinShun/plugins/iuuuuuuuu.screenshot-translate"
backup=root/"dist"/("before-native-"+datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
if installed.exists():shutil.copytree(installed,backup)
installed.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(package) as archive:
    for name in sorted(archive.namelist(),key=lambda n:n=="plugin.json"):
        target=(installed/name).resolve()
        if not target.is_relative_to(installed.resolve()):raise ValueError("Package escaped directory")
        content=archive.read(name)
        assert hashlib.sha256(content).hexdigest()==report["fileHashes"][name]
        temporary=target.with_name(target.name+".native.tmp");temporary.write_bytes(content)
        try:os.replace(temporary,target)
        except PermissionError:
            if target.suffix.lower() not in (".exe",".dll"):raise
            # Windows permits renaming a loaded image even while its bytes
            # remain mapped. Keep the old mapping intact; the host's watcher
            # can reload the new files without restarting WinShun.
            previous=target.with_name(target.name+".native-old-"+str(time.time_ns()))
            os.rename(target,previous)
            try:os.replace(temporary,target)
            except BaseException:
                os.rename(previous,target)
                raise
            try:previous.unlink()
            except PermissionError:pass
    for name in archive.namelist():
        assert (installed/name).read_bytes()==archive.read(name),name
result={"version":"0.4.0","installed":str(installed),"backup":str(backup),"dataDirectoryPreserved":True,
    "hostRestarted":False,"sha256":report["sha256"]}
(folder/"installation.json").write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(result,ensure_ascii=False))

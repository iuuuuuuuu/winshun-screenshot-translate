"""Install the verified local package with a complete backup; never stop WinShun."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
package = root / "dist/verified-0.3.3/iuuuuuuuu.screenshot-translate-0.3.3.wsplugin"
installed = Path(os.environ["LOCALAPPDATA"]) / "WinShun/plugins/iuuuuuuuu.screenshot-translate"
previous = json.loads((installed / "plugin.json").read_text(encoding="utf-8")).get("version", "unknown") if (installed / "plugin.json").exists() else "none"
backup = root / "dist" / ("installed-backup-" + previous + "-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
if installed.is_dir():
    shutil.copytree(installed, backup)
installed.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(package) as archive:
    names = sorted(archive.namelist(), key=lambda name: name == "plugin.json")
    for name in names:
        target = (installed / name).resolve()
        if not target.is_relative_to(installed.resolve()):
            raise ValueError("Package path escaped the plugin directory")
        target.parent.mkdir(parents=True, exist_ok=True)
        data = archive.read(name)
        temporary = target.with_name(target.name + ".repair.tmp")
        temporary.write_bytes(data)
        os.replace(temporary, target)
    for name in names:
        if (installed / name).read_bytes() != archive.read(name):
            raise ValueError("Installed file mismatch: " + name)
report = {"version": "0.3.3", "installed": str(installed), "backup": str(backup),
          "package": str(package), "filesVerified": len(names),
          "sha256": hashlib.sha256(package.read_bytes()).hexdigest(), "runningHostRestarted": False}
(package.parent / "installation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
(package.parent / (package.name + ".sha256")).write_text(report["sha256"] + "  " + package.name + "\n")
print(json.dumps(report, ensure_ascii=False, indent=2))

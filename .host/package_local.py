"""Build a reviewable local package without staging changes in the source repo."""
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

root = Path(__file__).resolve().parents[1]
source = root / ".test-output" / ("package-source-" + uuid.uuid4().hex[:8])
source.mkdir(parents=True)
files = ["CHANGELOG.md", "LICENSE", "NOTICE.md", "THIRD_PARTY_NOTICES.md", "README.md",
         "broker.py", "desktop.py", "main.py", "ocr.ps1", "plugin.json", "services.py",
         "spatial.py", "ui.py", "qt_ui.py", "native_ui.py", "requirements.txt"]
files += [str(p.relative_to(root)) for folder in ("qml", "assets") for p in (root / folder).rglob("*") if p.is_file()]
for name in files:
    destination = source / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / name, destination)
subprocess.run(["git", "init", "-q", str(source)], check=True)
subprocess.run(["git", "-C", str(source), "config", "core.autocrlf", "false"], check=True)
subprocess.run(["git", "-C", str(source), "add", "."], check=True)
tool = root.parent / "community-index/tools/build.py"
subprocess.run([sys.executable, "-B", str(tool), str(source), "--id", "iuuuuuuuu.screenshot-translate",
                "--out", str(root / "dist/verified-0.3.3")], check=True)
print("Package source:", source)

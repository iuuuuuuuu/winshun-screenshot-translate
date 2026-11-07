"""Build the local native package. This is a development tool, not a runtime."""
import datetime
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import zipfile

root=Path(__file__).resolve().parents[1]
build=Path(tempfile.gettempdir())/"winshun-st-native-040"
if "--build" in sys.argv:build=Path(sys.argv[sys.argv.index("--build")+1])
manifest=json.loads((root/"plugin.json").read_text(encoding="utf-8"))
manifest["version"]="0.4.0"
manifest["minWinShun"]="0.20.0"
manifest["runtime"]={"mode":"resident","program":"ScreenshotTranslate.exe","start":"eager","sandbox":"normal",
    "timeoutMs":25000,"debounceMs":300,"idleExitSec":0,"memoryMB":1024}
for setting in manifest["settings"]:
    if setting["key"]=="provider":setting["presentation"]="exclusive"
    if setting["key"]=="clipHotkey":
        setting["hint"]={"zh":"在鼠标附近打开词义窗口；清空关闭","en":"Open a word popup near the pointer; clear to disable"}
    if setting["key"]=="selectionHotkey":
        setting["default"]="Alt+Shift+W"
        setting["hint"]={"zh":"在选区旁显示词义；固定后跟随新选区。不支持划词的应用可用鼠标 OCR",
            "en":"Show a popup by the selection; pin to follow new selections. Use pointer OCR in unsupported apps"}
manifest["settings"].append({"key":"wordOcrHotkey","type":"hotkey",
    "title":{"zh":"鼠标指词 OCR 热键","en":"Pointer word OCR hotkey"},
    "hint":{"zh":"鼠标停在文字上按热键，识别所在行的短句；无需选中，清空关闭","en":"Point at text to translate the nearby sentence on its line; no selection needed; clear to disable"},
    "default":"Alt+Shift+O"})
files={"plugin.json":json.dumps(manifest,ensure_ascii=False,indent=2).encode("utf-8")+b"\n",
    "ScreenshotTranslate.exe":(build/"ScreenshotTranslate.exe").read_bytes(),
    "ScreenshotTranslateApp.dll":(build/"ScreenshotTranslateApp.dll").read_bytes(),
    "README.md":(root/"native/README.md").read_bytes(),
    "LICENSE":(root/"LICENSE").read_bytes(),
    "THIRD_PARTY_NOTICES.md":(root/"THIRD_PARTY_NOTICES.md").read_bytes()+
        b"\n\nNative WinShun sources: see native/vendor/README.md in the source repository.\n"+
        (root/"native/vendor/README.md").read_bytes()+b"\n\n"+(root/"native/vendor/LICENSE").read_bytes()}
out=root/"dist/native-0.4.0";out.mkdir(parents=True,exist_ok=True)
package=out/"iuuuuuuuu.screenshot-translate-0.4.0-native.wsplugin"
with zipfile.ZipFile(package,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
    for name,content in sorted(files.items()):
        info=zipfile.ZipInfo(name,(2026,11,6,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
        archive.writestr(info,content)
sha=hashlib.sha256(package.read_bytes()).hexdigest()
(out/(package.name+".sha256")).write_text(sha+"  "+package.name+"\n",encoding="utf-8")
report={"package":str(package),"sha256":sha,"size":package.stat().st_size,"version":"0.4.0",
    "runtime":"native-x64","pythonRequired":False,"qtBundled":False,"files":list(files),
    "fileHashes":{name:hashlib.sha256(content).hexdigest() for name,content in files.items()}}
(out/"package.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
print(json.dumps(report,ensure_ascii=False))

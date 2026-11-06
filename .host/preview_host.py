"""Run the built host's production previews on a private Windows desktop."""
import ctypes
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from desktop import api

name = "WinShunHostRepair-" + uuid.uuid4().hex
desktop = api("user32", "CreateDesktopW", w.HANDLE, w.LPCWSTR, ctypes.c_void_p,
              ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p)(name, None, None, 0, 0x1ff, None)
if not desktop:
    raise OSError("Cannot create a private desktop; refusing to launch the host preview")
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
startup.lpDesktop = name
installed = Path(os.environ["LOCALAPPDATA"]) / "Programs/WinShun"
env = dict(os.environ, PATH=str(installed) + os.pathsep + os.environ["PATH"],
           QT_PLUGIN_PATH=str(installed), QML_IMPORT_PATH=str(installed / "qml"),
           PYTHONDONTWRITEBYTECODE="1")
output = ROOT / ".test-output/host-built"
output.mkdir(parents=True, exist_ok=True)
arguments = sys.argv[1:]
program = ROOT / ".host/build-repair/WinShun.exe"
if arguments[:1] == ["--program"]:
    program = program.with_name(arguments[1])
    arguments = arguments[2:]
if "--sdk-runtime" in arguments:
    arguments.remove("--sdk-runtime")
    sdk = ROOT / ".host/qt/6.10.0/msvc2022_64"
    env.update(PATH=str(sdk / "bin") + os.pathsep + os.environ["PATH"],
               QT_PLUGIN_PATH=str(sdk / "plugins"), QML_IMPORT_PATH=str(sdk / "qml"))
if not arguments and program.name == "WinShun.exe":
    arguments = ["--preview-plugins", str(output / "plugins.png"), "--tree"]
try:
    with (output / "process.log").open("w", encoding="utf-8") as diagnostics:
        process = subprocess.Popen([str(program), *arguments], env=env, startupinfo=startup,
                                   creationflags=subprocess.CREATE_NO_WINDOW,
                                   stdout=diagnostics, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError("Private host preview timed out")
    print((output / "process.log").read_text(encoding="utf-8"))
    print("Private host preview exit:", code)
    sys.exit(code)
finally:
    api("user32", "CloseDesktop", w.BOOL, w.HANDLE)(desktop)

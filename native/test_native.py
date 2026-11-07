"""Native runtime QA. Private desktops, synthetic input and local service fixtures.

Python is the development harness only. Child PATH contains Windows and the
selected Qt runtime; shipped modules do not import Python/PySide. Public
packages must pass --runtime with their own bundled runtime directory.
"""
import ctypes
from ctypes import wintypes as w
import http.server
import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import uuid
import msvcrt

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from desktop import api

counts = {}
class Startup(ctypes.Structure):
    _fields_=[("cb",w.DWORD),("reserved",w.LPWSTR),("desktop",w.LPWSTR),("title",w.LPWSTR),
        ("x",w.DWORD),("y",w.DWORD),("cx",w.DWORD),("cy",w.DWORD),("charsX",w.DWORD),("charsY",w.DWORD),
        ("fill",w.DWORD),("flags",w.DWORD),("show",w.WORD),("reservedSize",w.WORD),("reservedData",ctypes.c_void_p),
        ("stdin",w.HANDLE),("stdout",w.HANDLE),("stderr",w.HANDLE)]
class ProcessInfo(ctypes.Structure):
    _fields_=[("process",w.HANDLE),("thread",w.HANDLE),("pid",w.DWORD),("tid",w.DWORD)]
class NativeProcess:
    def __init__(self,args,env,desktop,diagnostics):
        # CPython's Popen STARTUPINFO does not marshal an added lpDesktop field.
        # Pass the actual native field; never launch QA on the default desktop.
        readIn,writeIn=os.pipe();readOut,writeOut=os.pipe()
        os.set_inheritable(readIn,True);os.set_inheritable(writeOut,True)
        os.set_inheritable(diagnostics.fileno(),True)
        startup=Startup();startup.cb=ctypes.sizeof(startup);startup.desktop=desktop;startup.flags=0x101;startup.show=0
        startup.stdin=msvcrt.get_osfhandle(readIn);startup.stdout=msvcrt.get_osfhandle(writeOut);startup.stderr=msvcrt.get_osfhandle(diagnostics.fileno())
        self.info=ProcessInfo();command=ctypes.create_unicode_buffer(subprocess.list2cmdline(args))
        environment=ctypes.create_unicode_buffer("\0".join(k+"="+v for k,v in sorted(env.items(),key=lambda x:x[0].upper()))+"\0\0")
        try:
            ok=api("kernel32","CreateProcessW",w.BOOL,w.LPCWSTR,w.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,w.BOOL,w.DWORD,
                ctypes.c_void_p,w.LPCWSTR,ctypes.POINTER(Startup),ctypes.POINTER(ProcessInfo))(
                    args[0],command,None,None,True,0x08000000|0x400,environment,None,ctypes.byref(startup),ctypes.byref(self.info))
            if not ok:raise ctypes.WinError()
        finally:
            os.close(readIn);os.close(writeOut);os.set_inheritable(diagnostics.fileno(),False)
        api("kernel32","CloseHandle",w.BOOL,w.HANDLE)(self.info.thread)
        self.stdin=os.fdopen(writeIn,"wb",buffering=0);self.stdout=os.fdopen(readOut,"rb",buffering=0)
    def wait(self,timeout=75):
        status=api("kernel32","WaitForSingleObject",w.DWORD,w.HANDLE,w.DWORD)(self.info.process,int(timeout*1000))
        if status==258:raise subprocess.TimeoutExpired("native probe",timeout)
        code=w.DWORD();api("kernel32","GetExitCodeProcess",w.BOOL,w.HANDLE,ctypes.POINTER(w.DWORD))(self.info.process,ctypes.byref(code));return code.value
    def kill(self):
        api("kernel32","TerminateProcess",w.BOOL,w.HANDLE,w.UINT)(self.info.process,99)
    def close(self):
        self.stdin.close();self.stdout.close();api("kernel32","CloseHandle",w.BOOL,w.HANDLE)(self.info.process)
class Server(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass
    def do_GET(self):
        self.respond()
    def do_POST(self):
        self.respond()
    def respond(self):
        path = urllib.parse.urlsplit(self.path).path
        counts[path] = counts.get(path, 0) + 1
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        raw = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        body = (urllib.parse.parse_qs(raw.decode()) if "urlencoded" in self.headers.get("Content-Type", "")
                else json.loads(raw) if raw else {})
        if path == "/blocked":
            threading.Event().wait(30)
            return
        if path in ("/fail", "/queue-fail") or (path == "/zvo-recover" and counts[path] == 1):
            self.send_response(503); self.end_headers(); return
        if path == "/google-slow":
            time.sleep(.35)
        def translate(text):
            examples={"Screenshot translation":"截图翻译","Settings":"设置","History":"历史","Downloads":"下载"}
            if text.strip() in examples:return examples[text.strip()]
            if text.startswith("Native windows keep"):return "原生窗口保持截图的位置与尺寸。"
            if text.startswith("Loading and outside"):return "加载期间也能点击外部关闭。"
            if text=="<button>&":return "&lt;button&gt;&amp;"
            return "你好 + 100%" if text == "Hello + 100%" else text if text == "499" else "翻译：" + text
        if path.startswith("/mymemory"):
            assert len(query["q"][0].encode("utf-8"))<=500
            result = {"responseStatus": 200, "responseData": {"translatedText": translate(query["q"][0])}}
        elif path.startswith("/google"):
            result = [[[translate(query["q"][0]), query["q"][0]]]]
        elif path.startswith("/zvo"):
            result = {"result": 1, "text": [translate(t) for t in json.loads(body["text"][0])]}
        elif path.startswith("/microsoft"):
            assert self.headers.get("Ocp-Apim-Subscription-Key") == "fixture-key"
            result = [{"translations": [{"text": translate(t["Text"])}]} for t in body]
        elif path.startswith("/deepl"):
            assert self.headers.get("Authorization") == "DeepL-Auth-Key fixture-key"
            result = {"translations": [{"text": translate(t)} for t in body["text"]]}
        elif path == "/youdao-dictionary":
            assert query["q"] == ["hello"]
            result = {"ec": {"word": [{"trs": [{"tr": [{"l": {"i": ["int. 你好"]}}]}]}]}}
        elif path == "/youdao":
            text = body["q"][0]
            signed_input = text if len(text) <= 20 else text[:10] + str(len(text)) + text[-10:]
            assert body["appKey"] == ["fixture-app"] and body["signType"] == ["v3"]
            assert body["sign"][0] == hashlib.sha256(('fixture-app' + signed_input + body['salt'][0] + body['curtime'][0] + 'fixture-key').encode()).hexdigest()
            result = {"errorCode": "0", "translation": [translate(text)]}
        elif path.startswith("/sogou"):
            state = {"textTranslate": {"translateData": {"translate": {"errorCode": "0", "dit": translate(query['keyword'][0])}}}}
            page = '<script>window.__INITIAL_STATE__=' + json.dumps(state, ensure_ascii=False) + ';window.extra="ignored";</script>'
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers()
            self.wfile.write(page.encode('gb18030' if path.endswith('-gbk') else 'utf-8'));return
        else:
            assert self.headers.get("Authorization") == "Bearer fixture-key"
            result = {"choices": [{"message": {"content": json.dumps([translate(t) for t in json.loads(body["messages"][1]["content"])])}}]}
        self.send_response(200);self.send_header("Content-Type", "application/json; charset=utf-8");self.end_headers()
        try:
            self.wfile.write(json.dumps(result, ensure_ascii=False).encode())
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

def launch(program, args, data, output, initial=None, gpu=False):
    name = "WinShun_ST_Probe_" + uuid.uuid4().hex
    desktop = api("user32", "CreateDesktopW", w.HANDLE, w.LPCWSTR, ctypes.c_void_p,
                  ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p)(name, None, None, 0, 0x1ff, None)
    if not desktop:
        raise OSError("Private desktop unavailable; no test on user's desktop")
    host = Path(os.environ["LOCALAPPDATA"]) / "Programs/WinShun"
    if "--runtime" in sys.argv:
        host = Path(sys.argv[sys.argv.index("--runtime")+1]).resolve()
    env = dict(os.environ)
    env.update(PATH=str(host) + os.pathsep + str(Path(os.environ["SystemRoot"]) / "System32") + os.pathsep + os.environ["SystemRoot"],
               WS_APP_DIR=str(host), WS_DATA_DIR=str(data), QT_PLUGIN_PATH=str(host), QML_IMPORT_PATH=str(host / "qml"),
               QT_QPA_PLATFORM="windows")
    for key in ("HTTPS_PROXY", "HTTP_PROXY", "ALL_PROXY", "OPENAPI_TRANSLATE_PROXY", "PYTHONPATH", "PYTHONHOME", "QT_SCALE_FACTOR"):
        env.pop(key, None)
    if gpu:
        env.pop("QT_QUICK_BACKEND", None);env["QSG_RHI_BACKEND"] = "d3d11"
    else:
        env["QT_QUICK_BACKEND"] = "software"
    output.mkdir(parents=True, exist_ok=True)
    try:
        with (output / "stderr.log").open("wb") as diagnostics:
            process = NativeProcess([str(program),*args],env,name,diagnostics)
            # Diagnostic OCR can exceed the pipe buffer. Drain while waiting,
            # otherwise a successful child blocks before it can exit.
            chunks=[]
            reader=threading.Thread(target=lambda:chunks.append(process.stdout.read()),daemon=True)
            reader.start()
            if initial:
                process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": initial}).encode()+b"\n")
                process.stdin.flush()
            try:
                code = process.wait(timeout=75)
            except subprocess.TimeoutExpired:
                process.kill();process.wait();reader.join(5);process.close()
                raise RuntimeError("Native private probe timed out")
            reader.join(5)
            assert not reader.is_alive(), "Native output pipe did not close"
            stdout = b"".join(chunks)
            (output / "stdout.jsonl").write_bytes(stdout)
            process.close()
            assert code == 0, (code, (output/"stderr.log").read_text(encoding="utf-8",errors="replace"), (output/"report.json").read_text(encoding="utf-8") if (output/"report.json").exists() else "")
    finally:
        api("user32", "CloseDesktop", w.BOOL, w.HANDLE)(desktop)

def main():
    build = Path(tempfile.gettempdir()) / "winshun-st-native-040"
    if "--build" in sys.argv:
        build = Path(sys.argv[sys.argv.index("--build")+1])
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Server)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:" + str(server.server_port)
    with tempfile.TemporaryDirectory(prefix="WinShunNativeQA-") as temp:
        data = Path(temp)
        profiles = {p: {"endpoint": base+"/"+p, "secret": "", "model": "", "region": ""}
                    for p in ("zvo", "google", "mymemory")}
        (data/"services.json").write_text(json.dumps({"version":2,"profiles":profiles}),encoding="utf-8")
        out = ROOT/".test-output"/("native-gpu" if "--gpu" in sys.argv else "native-software")
        if "--output" in sys.argv:out=Path(sys.argv[sys.argv.index("--output")+1])
        if "--ocr-image" in sys.argv:
            source=Path(sys.argv[sys.argv.index("--ocr-image")+1]).resolve()
            options=[]
            for key in ("--ocr-scale","--ocr-points"):
                if key in sys.argv:options.extend([key,sys.argv[sys.argv.index(key)+1]])
            launch(build/"native_tests.exe",["--ocr-image",str(source),*options],data,out)
            print((out/"stdout.jsonl").read_text(encoding="utf-8"))
        elif "--live" in sys.argv:
            (data/"services.json").write_text('{"version":2,"profiles":{}}')
            options=[]
            for key in ("--proxy", "--live-providers"):
                if key in sys.argv:options.extend([key,sys.argv[sys.argv.index(key)+1]])
            launch(build/"native_tests.exe",["--live",*options],data,out)
            print(json.dumps(json.loads((out/"stdout.jsonl").read_text(encoding="utf-8")),ensure_ascii=True))
        elif "--core" in sys.argv:
            from services import Profiles
            profiles=Profiles(data)
            profiles.save({"openai":{"apiKey":"python-fixture-key"}})
            launch(build/"native_tests.exe",["--endpoint",base],data,out)
            assert Profiles(data).get("openai")["apiKey"]=="native-fixture-key"
            assert counts.get("/fail") == 2, counts  # memory skips, elapsed recovery retries
            print((out/"stdout.jsonl").read_text(encoding="utf-8"))
        else:
            pointer_fixtures=[]
            if "--pointer-fixture" in sys.argv:
                pointer_fixtures=json.loads(Path(sys.argv[sys.argv.index("--pointer-fixture")+1]).read_text(encoding="utf-8"))
            launch(build/"ScreenshotTranslate.exe",["--probe"],data,out,{
                "dataDir":str(data), "probeOutput":str(out), "probeBlockedEndpoint":base+"/blocked",
                "probeRuntime":str(Path(sys.argv[sys.argv.index("--runtime")+1]).resolve()) if "--runtime" in sys.argv else str(build.resolve()/"runtime"),
                "locale":"zh", "appearance":{"theme":"light","renderer":"d3d11" if "--gpu" in sys.argv else "software","locale":"zh"},
                "settings":{"provider":"zvo","fallbackEnabled":False}, "capabilities":{"hostHotkeys":True},
                "probePointerFixtures":pointer_fixtures,
                "probeMemory":"--memory" in sys.argv
            }, "--gpu" in sys.argv)
            report=json.loads((out/"report.json").read_text(encoding="utf-8"))
            assert report["passed"], report
            if "--memory" in sys.argv:
                print(json.dumps({k:v for k,v in report.items() if k.endswith("Memory") or k.endswith("Ms") or k in ("passed","idleWordEngineRetained")},ensure_ascii=True))
                server.shutdown()
                return
            if pointer_fixtures:assert report["wordLocalPixelOcr"],report
            messages=[json.loads(line) for line in (out/"stdout.jsonl").read_text(encoding="utf-8").splitlines()]
            responses={row["id"]:row["result"] for row in messages if "id" in row and "result" in row}
            keys=[row["params"]["arguments"]["keys"] for row in messages if row.get("method")=="winshun/tool"
                  and row["params"]["name"]=="register_hotkeys"]
            assert keys==[["Alt+Shift+T","Alt+Shift+C","Alt+Shift+W","Alt+Shift+O"]],keys
            for flag in ("wordRealEditSelection","wordClipboard","wordPinnedSelection","wordCancellation","wordMouseOcr","wordMousePhraseOcr","wordMouseSpacingOcr","wordOutsideDismiss",
                         "wordDefaultEditable","wordEditableClipboard","wordEditableSelection","wordEditableOcr","wordEditingPausesFollow","wordMouseMixedOcr","wordMouseWrappedOcr",
                         "wordIsolatedSelection","wordSelectionTimeoutRecovery","wordSelectionReplacement","wordMoveRendering","wordRetryStable","wordRetryTarget",
                         "actualProviderLabels","providerFallbackAppearance","wordAutoTranslateOnBlur","wordRestoreHoverOnly"):
                assert report[flag], report
            for request in (200,201,202):assert responses[request]["items"]==[],responses
            assert responses[203]["items"][0]["title"]=="你好 + 100%",responses[203]
            print(json.dumps({k:report[k] for k in ("passed","qtVersion","pythonLoaded","uiRenderer","hostRenderer",
                "selectorShowMs","outsideDismissMs","closeWhileBlockedMs","hotkeyProtocol","nativeHotkeyFallback","queryProtocol",
                "wordClipboard","wordPinnedSelection","wordMouseOcr","wordMousePhraseOcr","wordMouseSpacingOcr",
                "wordDefaultEditable","wordEditableClipboard","wordEditableSelection","wordEditableOcr","wordEditingPausesFollow","wordMouseMixedOcr","wordCloseWhileBlockedMs",
                "actualProviderLabels","providerFallbackAppearance","wordAutoTranslateOnBlur","closedMemory")},ensure_ascii=True))
    server.shutdown()
if __name__ == "__main__":
    main()

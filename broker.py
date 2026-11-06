"""Low-memory resident broker; every GUI/Windows OCR runtime is disposable."""
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

from main import App, log


class Broker(App):
    def __init__(self, input_test=False):
        super().__init__(headless=True, input_test=input_test)
        self.initial = {}
        self.child = None
        self.child_lock = threading.Lock()
        self.child_tools = {}
        self.child_sequence = 0
        self.child_busy = False
        self.child_closing = False
        self.active_query = None
        self.waiting_query = None
        self.query_flow = False
        self.native_region = None

    def initialize(self, identifier, params):
        self.initial = dict(params)
        super().initialize(identifier, params)
        log("memory: resident has no UI or OCR runtime")

    def write_child(self, message):
        with self.child_lock:
            if self.child and self.child.poll() is None:
                try:
                    self.child.stdin.write(json.dumps(message, ensure_ascii=False) + "\n")
                    self.child.stdin.flush()
                    return True
                except (BrokenPipeError, OSError):
                    pass
        return False

    def open_child(self, message):
        if self.child and self.child.poll() is None and not self.child_closing:
            self.write_child(message)
            return
        self.child_sequence += 1
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW | 0x80  # STARTF_FORCEOFFFEEDBACK
        startup.wShowWindow = 0
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        try:
            child = subprocess.Popen([sys.executable, "-B", str(Path(__file__).with_name("main.py")), "--ui-worker"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     text=True, encoding="utf-8", env=env, startupinfo=startup,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
        except OSError:
            self.child_busy = False
            self.active_query = None
            self.query_flow = False
            self.gate.end()
            self.reply(message.get("id"), {"items": [{"id": "error", "title": self.t(
                "无法启动翻译窗口，请检查 Python 安装", "Could not start the translation window; check Python")}], "cache": 0})
            log("ui: start failed; trigger released")
            return
        self.child = child
        self.child_closing = False
        from desktop import api
        from ctypes import wintypes
        api("user32", "AllowSetForegroundWindow", wintypes.BOOL, wintypes.DWORD)(child.pid)
        initial = {**self.initial, "settings": dict(self.settings), "capabilities": {"hostHotkeys": False}}
        self.write_child({"jsonrpc": "2.0", "id": "_ui_init", "method": "initialize", "params": initial})
        self.write_child(message)
        def read_output():
            try:
                for line in child.stdout:
                    if len(line) > 16 * 1024 * 1024:
                        continue
                    try:
                        value = json.loads(line)
                    except ValueError:
                        continue
                    self.events.put(lambda value=value: self.child_message(child, value))
            finally:
                child.wait()
                self.events.put(lambda: self.child_exited(child))
        def read_errors():
            try:
                for line in child.stderr:
                    # Workers only emit counts/state, never text, keys or request bodies.
                    print(line.rstrip(), file=sys.stderr, flush=True)
            except (OSError, ValueError):
                pass
        threading.Thread(target=read_output, daemon=True, name="ui-protocol").start()
        threading.Thread(target=read_errors, daemon=True, name="ui-diagnostics").start()

    def child_message(self, child, message):
        if child is not self.child:
            return
        if message.get("method") == "ui/state":
            if not message.get("params", {}).get("started"):
                return
            busy = bool(message.get("params", {}).get("busy"))
            self.child_closing = bool(message.get("params", {}).get("closing"))
            if busy:
                with self.gate.lock:
                    self.gate.busy = True
            elif not self.native_region and (self.child_busy or self.gate.busy):
                self.gate.end()
            if not self.native_region:
                self.child_busy = busy
            if not busy:
                self.query_flow = False
                self.drain_query()
            return
        if message.get("method") == "ui/cache":
            params = message["params"]
            self.cache[params["command"]] = (time.monotonic(), params["items"])
            return
        if message.get("id") in ("_ui_init", "_ui_action"):
            return
        if not message.get("method") and message.get("id") == self.active_query:
            self.active_query = None
        if message.get("method") == "winshun/tool":
            identifier = f"ui{self.child_sequence}:{message['id']}"
            self.child_tools[identifier] = message["id"]
            message = {**message, "id": identifier}
        self.send(message)

    def child_exited(self, child):
        if child is self.child:
            self.child = None
            self.child_tools.clear()
            self.child_busy = bool(self.native_region)
            self.child_closing = False
            self.query_flow = False
            if not self.native_region:
                self.gate.end()
            if self.active_query is not None:
                self.reply(self.active_query, {"items": [{"id": "error", "title": self.t(
                    "翻译窗口意外退出，请重试", "Translation window exited unexpectedly; retry")}], "cache": 0})
                self.active_query = None
            self.release_idle()
            log(f"memory: UI worker exited code={child.returncode}; screenshot and OCR runtimes released")
            self.drain_query()
        for stream in (child.stdin, child.stdout, child.stderr):
            try:
                stream.close()
            except OSError:
                pass

    def drain_query(self):
        if self.running and self.waiting_query and not self.gate.busy:
            message, self.waiting_query = self.waiting_query, None
            self.dispatch(message)

    def start_shot(self, kind, delay=0):
        if self.input_test:
            return super().start_shot(kind, delay)
        if not self.gate.begin():
            return
        self.child_busy = True
        if kind == "region":
            from native_ui import Region
            def finished(rect):
                def selected():
                    region, self.native_region = self.native_region, None
                    if not self.running:
                        return
                    if not rect:
                        self.child_busy = False
                        self.gate.end()
                        self.release_idle()
                        if region and region.error:
                            log("selector: failed; trigger released")
                        self.drain_query()
                        return
                    log(f"selector: shownMs={region.show_ms:.0f}" if region and region.show_ms is not None else "selector: closed")
                    self.open_child({"method": "ui/capture", "params": {"rect": rect}})
                self.events.put(selected)
            self.native_region = Region(finished)
            self.write_child({"method": "ui/prepare-capture"})
            if delay:
                timer = threading.Timer(delay / 1000, self.native_region.show)
                timer.daemon = True
                timer.start()
            else:
                self.native_region.show()
            return
        self.open_child({"method": "ui/start", "params": {"kind": kind, "delay": delay}})

    def text_flow(self, command):
        if self.input_test:
            return super().text_flow(command)
        if not self.gate.begin():
            return
        self.child_busy = True
        self.open_child({"id": "_ui_action", "method": "action",
                         "params": {"action": "retry-clip" if command == "clip" else "read-selection"}})

    def dispatch(self, message):
        method = message.get("method", "")
        identifier = message.get("id")
        if not method and identifier in self.child_tools:
            original = self.child_tools.pop(identifier)
            self.write_child({**message, "id": original})
            return
        if method == "showSettings":
            self.reply(identifier, {})
            self.open_child({"id": "_ui_action", "method": "showSettings"})
            return
        if method == "query" and message.get("params", {}).get("command") in ("clip", "jsel"):
            if message["params"]["command"] in self.cache:
                self.query(identifier, message["params"])
                return
            if not self.gate.begin():
                if self.query_flow or self.waiting_query:
                    if self.waiting_query:
                        self.reply(self.waiting_query.get("id"), {"items": [], "cache": 0})
                    self.waiting_query = message
                else:
                    self.reply(identifier, {"items": [{"id": "busy", "title": self.t(
                        "正在处理截图，请稍后重试", "A screenshot is being processed; retry shortly")}], "cache": 0})
                return
            self.child_busy = True
            self.active_query = identifier
            self.query_flow = True
            self.open_child(message)
            return
        if method == "cancel":
            query_id = message.get("params", {}).get("queryId")
            if self.waiting_query and query_id == self.waiting_query.get("params", {}).get("queryId", self.waiting_query.get("id")):
                self.reply(self.waiting_query.get("id"), {"items": [], "cache": 0})
                self.waiting_query = None
            self.write_child(message)
            return
        super().dispatch(message)
        if method in ("settingsChanged", "appearance"):
            if method == "appearance":
                self.initial.update(message.get("params", {}))
            self.write_child(message)

    def shutdown(self):
        self.running = False
        if self.native_region:
            self.native_region.close()
            self.native_region = None
        self.waiting_query = None
        if self.child and self.child.poll() is None:
            self.write_child({"method": "shutdown"})
            try:
                self.child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.child.kill()
                self.child.wait()
            self.child_exited(self.child)
        super().shutdown()


if __name__ == "__main__":
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8")
    Broker(input_test="--input-test" in sys.argv).run()

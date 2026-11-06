"""WinShun protocol core and disposable GUI worker."""
import gc
import json
import os
from pathlib import Path
import queue
import sys
import threading
import time

from services import LANGUAGES, Profiles, Translator, Usage, selected, target_for, detect

PLUGIN_ID = "iuuuuuuuu.screenshot-translate"
VERSION = "0.2.1"
DEFAULTS = {"provider": "mymemory", "proxy": "", "target": "auto",
            "shotMode": "multi", "shotHotkey": "Alt+Shift+T", "clipHotkey": "Alt+Shift+C", "selectionHotkey": ""}
TRIGGER_ACTIONS = {"start-region", "start-full", "start-window", "retry-shot", "retry-clip", "read-selection"}


def log(message):
    print("[screenshot-translate] " + message, file=sys.stderr, flush=True)


def row(identifier, title, subtitle="", valid=False):
    return {"id": identifier, "title": title, "subtitle": subtitle, "icon": "glyph:U+E8C1", "valid": valid}


def result_items(data, command="clip"):
    data = {**data, "command": command}
    actions = [{"id": "copy-translation", "title": "Copy translation", "key": "Enter"},
               {"id": "copy-source", "title": "Copy source", "key": "Ctrl+Enter"},
               {"id": "retry-text", "title": "Translate again", "key": "Shift+Enter"}]
    return [{"id": key, "title": data[field], "copy": data[field],
             "subtitle": data["sourceLanguage"] + " \u2192 " + data["targetLanguage"],
             "icon": "glyph:U+E8C1", "data": data, "actions": actions}
            for key, field in (("translation", "translated"), ("source", "source"))]


class TriggerGate:
    def __init__(self, clock=time.monotonic):
        self.clock, self.lock, self.epoch, self.busy = clock, threading.Lock(), 0, False

    def receive(self):
        with self.lock:
            return self.clock(), self.epoch, self.busy

    def reason(self, receipt):
        with self.lock:
            if receipt[2] or self.busy:
                return "busy"
            if receipt[1] != self.epoch:
                return "completed-operation"
            return "expired" if self.clock() - receipt[0] > 1 else ""

    def begin(self):
        with self.lock:
            if self.busy:
                return False
            self.busy = True
            return True

    def end(self):
        with self.lock:
            self.busy = False
            self.epoch += 1


class Job:
    def __init__(self):
        self.cancelled = threading.Event()
        self.worker, self.thread = None, None
        self.pin = None

    def cancel(self):
        self.cancelled.set()
        if self.worker:
            self.worker.cancel()


class App:
    def __init__(self, headless=False, input_test=False):
        self.headless, self.input_test = headless, input_test
        self.root = self.pin = self.region = self.config = None
        self.gate, self.inbox, self.events = TriggerGate(), queue.Queue(512), queue.Queue()
        self.settings, self.chinese, self.running = dict(DEFAULTS), True, True
        self.profiles = self.usage = self.translator = None
        self.send_lock, self.job = threading.Lock(), None
        self.tools, self.sequence, self.hotkeys = {}, 0, None
        self.cache, self.query_cancel, self.jobs = {}, {}, set()
        self.keywords = {"shot": "jst", "clip": "jft", "jsel": "jsel"}
        self.dirty, self.activity = False, time.monotonic()
        if not headless:
            import desktop
            import tkinter as tk
            from ui import Theme, style
            desktop.dpi_awareness()
            self.root = tk.Tk()
            self.root.withdraw()
            self.theme = Theme()
            style(self.root, self.theme)

    def t(self, zh, en):
        return zh if self.chinese else en

    def active_provider(self):
        return selected(self.settings)

    def send(self, message):
        with self.send_lock:
            print(json.dumps(message, ensure_ascii=False, separators=(",", ":")), flush=True)

    def reply(self, identifier, result):
        if identifier is not None:
            self.send({"jsonrpc": "2.0", "id": identifier, "result": result})

    def tool(self, name, arguments, callback=None):
        self.sequence += 1
        identifier = f"p{self.sequence}"
        self.tools[identifier] = (time.monotonic(), callback)
        self.send({"jsonrpc": "2.0", "id": identifier, "method": "winshun/tool",
                   "params": {"name": name, "arguments": arguments}})

    def read(self):
        try:
            for line in sys.stdin:
                if len(line) > 16 * 1024 * 1024:
                    continue
                try:
                    message = json.loads(line)
                    if not isinstance(message, dict):
                        continue
                except ValueError:
                    log("ignored non-JSON input")
                    continue
                method = message.get("method", "")
                kind = method == "hotkey" or (method == "action" and message.get("params", {}).get("action") in TRIGGER_ACTIONS)
                if kind:
                    message["_receipt"] = self.gate.receive()
                    if message["_receipt"][2]:
                        log("input: ignored reason=busy")
                        self.reply(message.get("id"), {"effects": [{"type": "keep"}]} if method == "action" else {})
                        continue
                self.inbox.put(message)
                message, line = None, ""
        finally:
            self.inbox.put({"method": "_eof"})

    def register(self):
        keys = list(dict.fromkeys(self.settings.get(k, "").strip() for k in
                                  ("shotHotkey", "clipHotkey", "selectionHotkey") if self.settings.get(k, "").strip()))
        if keys == self.hotkeys:
            return
        self.send({"jsonrpc": "2.0", "method": "winshun/hotkeys", "params": {"keys": keys}})
        def done(answer):
            self.hotkeys = keys
            if answer.get("errors"):
                log("some host hotkeys could not be registered")
        self.tool("register_hotkeys", {"keys": keys}, done)

    def initialize(self, identifier, params):
        self.settings = {**DEFAULTS, **params.get("settings", {})}
        self.chinese = not params.get("locale", "zh").startswith("en")
        directory = os.environ.get("WS_DATA_DIR") or params.get("dataDir") or str(
            Path(os.environ.get("LOCALAPPDATA", ".")) / "WinShun/plugins" / (PLUGIN_ID + ".data"))
        self.profiles, self.usage = Profiles(directory), Usage(directory)
        self.translator = Translator(self.usage)
        self.host_hotkeys = bool(params.get("capabilities", {}).get("hostHotkeys"))
        self.reply(identifier, {"protocolVersion": "1", "name": PLUGIN_ID})
        if not self.headless:
            self.appearance(params.get("appearance", params))
        if self.host_hotkeys:
            self.register()
        log(f"startup: version={VERSION} runtime=python ocr=short-lived-worker")

    def appearance(self, params):
        if "locale" in params:
            self.chinese = not params["locale"].startswith("en")
        if not self.headless:
            from ui import Theme, style
            self.theme = Theme(params.get("theme") == "dark")
            style(self.root, self.theme)
            if self.pin:
                self.pin.redraw()

    def dispatch(self, message):
        method, identifier, params = message.get("method", ""), message.get("id"), message.get("params") or {}
        if not method:
            pending = self.tools.pop(identifier, None)
            if pending and pending[1] and "result" in message:
                pending[1](message["result"])
            return
        if "_receipt" in message:
            reason = self.gate.reason(message["_receipt"])
            if reason:
                log("input: ignored reason=" + reason)
                self.reply(identifier, {"effects": [{"type": "keep"}]} if method == "action" else {})
                return
        if method == "initialize":
            self.initialize(identifier, params)
        elif method == "settingsChanged":
            self.settings = {**DEFAULTS, **params.get("settings", self.settings)}
            if self.host_hotkeys:
                self.register()
        elif method == "appearance":
            self.appearance(params)
        elif method == "query":
            self.query(identifier, params)
        elif method == "cancel":
            query = params.get("queryId")
            if query in self.query_cancel:
                self.query_cancel[query].set()
        elif method == "action":
            self.action(identifier, params)
        elif method == "hotkey":
            key = params.get("key", "").lower()
            if key and key == self.settings.get("shotHotkey", "").lower():
                self.start_shot("region")
            elif key:
                for setting, command in (("clipHotkey", "clip"), ("selectionHotkey", "jsel")):
                    if key == self.settings.get(setting, "").lower():
                        self.text_flow(command)
                        break
        elif method == "showSettings":
            self.reply(identifier, {})
            if not self.headless:
                from ui import Settings
                if not self.config:
                    self.config = Settings(self)
                self.config.show()
        elif method in ("shutdown", "_eof"):
            self.shutdown()
            self.reply(identifier, {})
        else:
            self.reply(identifier, {})

    def query(self, identifier, params):
        command, text = params.get("command", ""), params.get("text", "").strip()
        if params.get("keyword"):
            self.keywords[command] = params["keyword"]
        if command in self.cache:
            self.reply(identifier, {"items": self.cache.pop(command)[1], "cache": 0})
            return
        if command == "shot":
            self.reply(identifier, {"items": [row("hint-start", self.t("\u6846\u9009\u5c4f\u5e55\u533a\u57df\u7ffb\u8bd1", "Translate a screen region"), valid=True)]})
            return
        if command not in ("clip", "jsel"):
            self.reply(identifier, {"items": []})
            return
        if not text and command == "jsel":
            self.reply(identifier, {"items": [row("selection", self.t("\u8bf7\u7528\u5212\u8bcd\u70ed\u952e\u8bfb\u53d6\u9009\u4e2d\u6587\u5b57", "Use the selection hotkey to read selected text"))]})
            return
        if not text:
            text = self.clipboard()
        if not text:
            self.reply(identifier, {"items": [row("empty", self.t("\u526a\u8d34\u677f\u91cc\u6ca1\u6709\u6587\u5b57", "The clipboard has no text"))]})
            return
        query_id = params.get("queryId", identifier)
        cancelled = threading.Event()
        self.query_cancel[query_id] = cancelled
        snapshot = dict(self.settings)
        self.send({"jsonrpc": "2.0", "method": "query/partial", "params": {
            "queryId": query_id, "items": [row("working", self.t("\u7ffb\u8bd1\u4e2d\u2026", "Translating\u2026"))], "replace": False}})
        def work(job):
            data = self.translator.translate(self.profiles.current(snapshot), [text], target=target_for(text, snapshot["target"]))
            return result_items(data, command)
        def done(value, failure):
            self.query_cancel.pop(query_id, None)
            if cancelled.is_set():
                self.reply(identifier, {"items": [], "cache": 0})
                return
            items = value if not failure else [row("error", self.t("\u7ffb\u8bd1\u5931\u8d25", "Translation failed"), failure)]
            self.reply(identifier, {"items": items, "cache": 0})
        if not self.launch(work, done):
            self.query_cancel.pop(query_id, None)
            self.reply(identifier, {"items": [row("busy", self.t("\u6b63\u5728\u5904\u7406", "A translation is in progress"))], "cache": 0})

    def action(self, identifier, params):
        action = params.get("action", "")
        data = params.get("item", {}).get("data", {})
        if action in ("copy-source", "copy-translation"):
            text = data.get("source" if action == "copy-source" else "translated", params.get("item", {}).get("copy", ""))
            self.reply(identifier, {"effects": [{"type": "copy", "text": text}, {"type": "hide"}]})
        elif action == "retry-text":
            self.reply(identifier, {"effects": [{"type": "setQuery", "text": self.keywords.get(data.get("command"), "jft") + " " + data.get("source", "")}]})
        else:
            self.reply(identifier, {"effects": [{"type": "hide"}]})
            kind = {"start-region": "region", "retry-shot": "region", "start-full": "full", "start-window": "window"}.get(action)
            if kind:
                self.start_shot(kind, delay=350)
            elif action in ("retry-clip", "read-selection"):
                self.text_flow("clip" if action == "retry-clip" else "jsel")

    def launch(self, work, done, reserved=False, pin=None):
        if not reserved and not self.gate.begin():
            return False
        job = Job()
        job.pin = pin
        self.job = job
        self.jobs.add(job)
        def run():
            result, failure = None, ""
            try:
                result = work(job)
            except InterruptedError:
                pass
            except Exception as ex:
                # Only controlled error messages reach UI; never raw request payloads.
                failure = str(ex) if isinstance(ex, (ValueError, TimeoutError)) else "Operation failed. Check network or installed OCR languages."
            self.events.put(lambda: finish(result, failure))
        def finish(value, failure):
            self.jobs.discard(job)
            if self.job is job:
                self.job = None
                self.gate.end()
            if not job.cancelled.is_set() and self.running:
                done(value, failure)
            self.release_idle()
        job.thread = threading.Thread(target=run, daemon=True, name="translation")
        job.thread.start()
        return True

    def ui_call(self, callback, job):
        ready, output = threading.Event(), []
        def apply():
            if self.running and not job.cancelled.is_set():
                output.append(callback())
            ready.set()
        self.events.put(apply)
        while not ready.wait(.1):
            if job.cancelled.is_set() or not self.running:
                raise InterruptedError("Cancelled")
        if not output:
            raise InterruptedError("Cancelled")
        return output[0]

    def start_shot(self, kind, delay=0):
        if not self.gate.begin():
            return
        if self.input_test:
            self.launch(lambda job: time.sleep(.08), lambda value, failure: log("input-test: shot flow"), reserved=True)
            return
        if self.headless:
            self.gate.end()
            return
        import desktop
        from ui import Region
        previous_window = desktop.foreground()
        def selected_rect(rect):
            if not rect or not self.running:
                self.gate.end()
                return
            self.capture_flow(rect)
        def start():
            if kind == "region":
                self.region = Region(self, selected_rect)
                self.region.show()
            else:
                try:
                    rect = desktop.window_rect(previous_window) if kind == "window" else (
                        desktop.monitor_rect() if self.settings["shotMode"] == "single" else desktop.virtual_screen())
                    selected_rect(rect)
                except OSError:
                    self.gate.end()
        self.root.after(delay, start)

    def capture_flow(self, rect):
        import desktop
        from ui import Pin
        if self.pin:
            self.pin.close()
        snapshot = dict(self.settings)
        holder = []
        def work(job):
            image = desktop.capture(rect)
            def open_pin():
                self.pin = Pin(self, image)
                job.pin = self.pin
                self.pin.show()
                holder.append(self.pin)
                return self.pin
            pin = self.ui_call(open_pin, job)
            job.worker = desktop.Worker()
            blocks = job.worker.recognize(image)
            # The PowerShell process has exited before any HTTP request begins.
            log(f"ocr: blocks={len(blocks)} workerExited={job.worker.process is None}")
            if not blocks:
                raise ValueError(self.t("\u6ca1\u6709\u8bc6\u522b\u5230\u6587\u5b57", "No text recognized"))
            self.ui_call(lambda: pin.update_status(self.t("\u6b63\u5728\u7ffb\u8bd1\u2026", "Translating\u2026")), job)
            parts = [block["text"] for block in blocks]
            source = detect("\n".join(parts))
            data = self.translator.translate(self.profiles.current(snapshot), parts, source, target_for("\n".join(parts), snapshot["target"]))
            return blocks, data
        def done(value, failure):
            if holder and not holder[0].closed:
                if failure:
                    holder[0].update_status(failure, False)
                elif value:
                    holder[0].set_result(*value)
            elif failure:
                self.send({"jsonrpc": "2.0", "method": "winshun/notify", "params": {"message": failure}})
            holder.clear()
        self.launch(work, done, reserved=True)

    def retranslate(self, pin, source=None, target=None, parts=None):
        if pin.closed:
            return
        snapshot = dict(self.settings)
        values = list(parts or [b["text"] for b in pin.blocks])
        origin, destination = source or pin.source.get(), target or pin.target.get()
        original = pin.data.get("source", "\n".join(values))
        def work(job):
            data = self.translator.translate(self.profiles.current(snapshot), values, origin, destination)
            # Copy-source remains the OCR original even when reversing translation.
            data["source"] = original
            return data
        def done(value, failure):
            if not pin.closed:
                if failure:
                    pin.update_status(failure, False)
                elif value:
                    pin.set_result(pin.blocks, value)
        if self.launch(work, done, pin=pin):
            pin.update_status(self.t("\u6b63\u5728\u7ffb\u8bd1\u2026", "Translating\u2026"))

    def text_flow(self, command):
        if self.input_test:
            self.launch(lambda job: time.sleep(.08), lambda value, failure: log("input-test: text flow"))
            return
        snapshot = dict(self.settings)
        text = self.clipboard() if command == "clip" else None
        def work(job):
            value = text
            if command == "jsel":
                import desktop
                job.worker = desktop.Worker()
                value = job.worker.run({"mode": "selection"})["text"]
            if not value:
                raise ValueError("No clipboard or selected text was found")
            return result_items(self.translator.translate(self.profiles.current(snapshot), [value],
                                                         target=target_for(value, snapshot["target"])), command)
        def done(value, failure):
            self.cache[command] = (time.monotonic(), value if not failure else [row("error", "Translation failed", failure)])
            self.tool("show_launcher", {"text": self.keywords[command] + " " + self.t("\u521a\u521a", "just now")})
        self.launch(work, done)

    def clipboard(self):
        if self.headless:
            return ""
        try:
            return self.root.clipboard_get().strip()
        except Exception:
            return ""

    def copy(self, text):
        self.tool("clipboard_copy", {"text": text})

    def cancel_pin(self, pin):
        if self.job and self.job.pin is pin:
            self.job.cancel()

    def release_idle(self):
        self.dirty, self.activity = True, time.monotonic()

    def maintenance(self):
        now = time.monotonic()
        self.cache = {key: value for key, value in self.cache.items() if now - value[0] < 120}
        self.tools = {key: value for key, value in self.tools.items() if now - value[0] < 10}
        if self.dirty and not self.job and now - self.activity >= 3:
            gc.collect()
            self.dirty = False
            log("memory: idle screenshotBuffers=0" if not self.pin else "memory: visible translation retained")

    def tick(self):
        for _ in range(50):
            try:
                callback = self.events.get_nowait()
            except queue.Empty:
                break
            callback()
        for _ in range(50):
            try:
                message = self.inbox.get_nowait()
            except queue.Empty:
                break
            try:
                self.dispatch(message)
            except Exception:
                self.reply(message.get("id"), {"items": [row("error", "Plugin operation failed; existing configuration was preserved")]})
                log("request failed; payload omitted")
            if not self.running:
                break
        self.maintenance()
        if self.running and self.root:
            self.root.after(20, self.tick)

    def shutdown(self):
        self.running = False
        for job in list(self.jobs):
            job.cancel()
        if self.pin:
            self.pin.close()
        if self.config:
            self.config.close()
        if self.region:
            self.region.close(None)
        if self.root:
            self.root.quit()

    def run(self):
        threading.Thread(target=self.read, daemon=True, name="plugin-protocol").start()
        if self.root:
            self.root.after(0, self.tick)
            self.root.mainloop()
        else:
            while self.running:
                self.tick()
                time.sleep(.01)
        self.shutdown()
        for job in list(self.jobs):
            job.thread.join(timeout=9)
        if self.root:
            self.root.destroy()


class UiWorkerApp(App):
    def __init__(self):
        super().__init__()
        self.started = False
        self.last_state = None

    def dispatch(self, message):
        if message.get("method") == "ui/start":
            self.started = True
            params = message.get("params", {})
            self.start_shot(params.get("kind", "region"), params.get("delay", 0))
            return
        if message.get("method") in ("action", "hotkey", "showSettings", "query"):
            self.started = True
        super().dispatch(message)

    def tool(self, name, arguments, callback=None):
        if name == "show_launcher":
            for command, (_, items) in self.cache.items():
                self.send({"jsonrpc": "2.0", "method": "ui/cache", "params": {"command": command, "items": items}})
            self.cache.clear()
        super().tool(name, arguments, callback)

    def tick(self):
        super().tick()
        closing = self.started and not any((self.pin, self.region, self.config, self.job, self.gate.busy))
        state = (self.gate.busy, closing)
        if self.last_state != state:
            self.last_state = state
            self.send({"jsonrpc": "2.0", "method": "ui/state", "params": {"busy": self.gate.busy, "closing": closing}})
        if closing and self.running:
            self.shutdown()


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    if "--ui-worker" in sys.argv:
        UiWorkerApp().run()
    else:
        App(headless="--headless" in sys.argv, input_test="--input-test" in sys.argv).run()

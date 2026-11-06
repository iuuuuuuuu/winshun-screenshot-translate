"""Installed-style protocol and real disposable workers; no desktop input."""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from services import Profiles


class Resident:
    def __init__(self, directory):
        self.output, self.logs = queue.Queue(), queue.Queue()
        env = {k: v for k, v in os.environ.items() if "PROXY" not in k.upper()}
        env.update(WS_DATA_DIR=directory, NO_PROXY="127.0.0.1")
        self.p = subprocess.Popen(
            [sys.executable, "-B", str(ROOT / "broker.py")], cwd=ROOT, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
        for stream, target in ((self.p.stdout, self.output), (self.p.stderr, self.logs)):
            threading.Thread(target=lambda s=stream, q=target: [q.put(line) for line in s], daemon=True).start()

    def send(self, method, params=None, identifier=None):
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": identifier}) + "\n")
        self.p.stdin.flush()

    def until(self, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = json.loads(self.output.get(timeout=max(.01, deadline - time.monotonic())))
            if predicate(value):
                return value
        raise AssertionError("Expected protocol message was not received")

    def close(self):
        if self.p.poll() is None:
            self.send("shutdown", identifier=999)
            try:
                self.p.wait(timeout=6)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
        for stream in (self.p.stdin, self.p.stdout, self.p.stderr):
            stream.close()


@unittest.skipUnless(os.name == "nt", "Windows resident worker")
class BrokerIntegrationTests(unittest.TestCase):
    def test_host_initialization_registers_custom_hotkeys_and_retries_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            resident = Resident(directory)
            try:
                settings = {"shotHotkey": "Ctrl+Alt+D", "clipHotkey": "", "selectionHotkey": ""}
                resident.send("initialize", {"settings": settings, "capabilities": {"hostHotkeys": True}}, 1)
                resident.until(lambda m: m.get("id") == 1)
                notification = resident.until(lambda m: m.get("method") == "winshun/hotkeys")
                self.assertEqual(notification["params"]["keys"], ["Ctrl+Alt+D"])
                call = resident.until(lambda m: m.get("method") == "winshun/tool")
                self.assertEqual(call["params"], {"name": "register_hotkeys", "arguments": {"keys": ["Ctrl+Alt+D"]}})
                resident.p.stdin.write(json.dumps({"id": call["id"], "result": {"errors": {"Ctrl+Alt+D": 1409}}}) + "\n")
                resident.p.stdin.flush()
                resident.send("settingsChanged", {"settings": settings})
                resident.until(lambda m: m.get("method") == "winshun/hotkeys")
                call = resident.until(lambda m: m.get("method") == "winshun/tool")
                resident.p.stdin.write(json.dumps({"id": call["id"], "result": {"errors": {}}}) + "\n")
                resident.p.stdin.flush()
                self.assertIsNone(resident.p.poll())
            finally:
                resident.close()

    def test_typing_and_cancelling_queries_eventually_translates_latest_text(self):
        entered, release = threading.Event(), threading.Event()
        submitted = []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                parts = json.loads(data["messages"][-1]["content"])
                submitted.append(parts)
                if len(submitted) == 1:
                    entered.set()
                    release.wait(5)
                answer = json.dumps({"choices": [{"message": {"content": json.dumps(["Translated: " + x for x in parts])}}]}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(answer)))
                self.end_headers()
                self.wfile.write(answer)

            def log_message(self, *args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            with tempfile.TemporaryDirectory() as directory:
                Profiles(directory).save({"openai": {"apiKey": "synthetic", "endpoint": f"http://127.0.0.1:{server.server_port}/v1"}})
                resident = Resident(directory)
                try:
                    resident.send("initialize", {"settings": {"provider": "openai"}}, 1)
                    resident.until(lambda m: m.get("id") == 1)
                    resident.send("query", {"command": "clip", "text": "H", "queryId": 2}, 2)
                    self.assertTrue(entered.wait(5))
                    resident.send("cancel", {"queryId": 2})
                    resident.send("query", {"command": "clip", "text": "Hello", "queryId": 3}, 3)
                    resident.send("cancel", {"queryId": 3})
                    resident.send("query", {"command": "clip", "text": "Hello world", "queryId": 4}, 4)
                    release.set()
                    answer = resident.until(lambda m: m.get("id") == 4)
                    self.assertEqual(answer["result"]["items"][0]["title"], "Translated: Hello world")
                    self.assertEqual(submitted, [["H"], ["Hello world"]])
                    resident.send("query", {"command": "clip", "text": "Fresh query", "queryId": 5}, 5)
                    answer = resident.until(lambda m: m.get("id") == 5)
                    self.assertEqual(answer["result"]["items"][0]["title"], "Translated: Fresh query")
                    self.assertIsNone(resident.p.poll())
                finally:
                    release.set()
                    resident.close()
        finally:
            release.set()
            server.shutdown()
            server.server_close()

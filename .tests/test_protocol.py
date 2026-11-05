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


class ProtocolTests(unittest.TestCase):
    def test_real_stdin_busy_hotkeys_and_new_press(self):
        with tempfile.TemporaryDirectory() as directory:
            env = dict(os.environ, WS_DATA_DIR=directory, PYTHONDONTWRITEBYTECODE="1")
            flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            p = subprocess.Popen([sys.executable, "-B", "broker.py", "--input-test"], cwd=ROOT, env=env,
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, encoding="utf-8", creationflags=flags)
            output, logs = queue.Queue(), queue.Queue()
            def read(stream, target):
                for line in stream:
                    target.put(line)
            readers = [threading.Thread(target=read, args=(stream, target), daemon=True) for stream, target in
                       ((p.stdout, output), (p.stderr, logs))]
            for thread in readers:
                thread.start()
            def send(message):
                p.stdin.write(json.dumps(message) + "\n")
                p.stdin.flush()
            try:
                send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"settings": {}, "capabilities": {"hostHotkeys": False}}})
                answer = json.loads(output.get(timeout=5))
                self.assertEqual(answer["id"], 1)
                send({"method": "hotkey", "params": {"key": "Alt+Shift+T"}})
                time.sleep(.03)
                for _ in range(300):
                    send({"method": "hotkey", "params": {"key": "Alt+Shift+T"}})
                time.sleep(.4)
                first_logs = []
                while not logs.empty():
                    first_logs.append(logs.get_nowait())
                self.assertEqual(sum("input-test: shot flow" in line for line in first_logs), 1)
                self.assertTrue(any("reason=busy" in line for line in first_logs))
                send({"method": "hotkey", "params": {"key": "Alt+Shift+T"}})
                time.sleep(.25)
                new_logs = []
                while not logs.empty():
                    new_logs.append(logs.get_nowait())
                self.assertEqual(sum("input-test: shot flow" in line for line in new_logs), 1)
                send({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
                self.assertEqual(json.loads(output.get(timeout=5))["id"], 2)
                p.wait(timeout=5)
                self.assertEqual(p.returncode, 0)
            finally:
                if p.poll() is None:
                    p.kill()
                p.wait()
                for stream in (p.stdin, p.stdout, p.stderr):
                    stream.close()

    def test_actual_local_http_query_and_no_capture_launcher(self):
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                text = json.loads(body["messages"][-1]["content"])
                answer = json.dumps({"choices": [{"message": {"content": json.dumps(["Translated: " + x for x in text])}}]}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(answer)))
                self.end_headers()
                self.wfile.write(answer)
            def log_message(self, *args):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as directory:
                sys.path.insert(0, str(ROOT))
                from services import Profiles
                if os.name != "nt":
                    self.skipTest("Windows profile encryption")
                Profiles(directory).save({"openai": {"apiKey": "fake-only", "endpoint": f"http://127.0.0.1:{server.server_port}/v1"}})
                env = {k: v for k, v in os.environ.items() if "PROXY" not in k.upper()}
                env.update(WS_DATA_DIR=directory, NO_PROXY="127.0.0.1")
                input_lines = [
                    {"id": 1, "method": "initialize", "params": {"settings": {"provider": "ai", "aiProvider": "openai"}}},
                    {"id": 2, "method": "query", "params": {"command": "clip", "text": "Hello", "queryId": 2}},
                ]
                p = subprocess.Popen([sys.executable, "-B", "broker.py"], cwd=ROOT, env=env,
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
                diagnostics = queue.Queue()
                threading.Thread(target=lambda: [diagnostics.put(line) for line in p.stderr], daemon=True).start()
                try:
                    for message in input_lines:
                        p.stdin.write(json.dumps(message) + "\n")
                    p.stdin.flush()
                    messages = []
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        message = json.loads(p.stdout.readline())
                        messages.append(message)
                        if message.get("id") == 2:
                            break
                    self.assertIn("Translated: Hello", messages[-1]["result"]["items"][0]["title"])
                    self.assertFalse(any(m.get("method") == "winshun/tool" for m in messages))
                    seen_exit = False
                    until = time.monotonic() + 5
                    while time.monotonic() < until:
                        try:
                            line = diagnostics.get(timeout=.2)
                        except queue.Empty:
                            continue
                        if "UI worker exited" in line:
                            seen_exit = True
                            break
                    self.assertTrue(seen_exit, "UI worker remained resident after its operation")
                    p.stdin.write('{"id":3,"method":"shutdown"}\n')
                    p.stdin.flush()
                    p.wait(timeout=5)
                    self.assertEqual(p.returncode, 0)
                finally:
                    if p.poll() is None:
                        p.kill()
                    p.wait()
                    for stream in (p.stdin, p.stdout, p.stderr):
                        stream.close()
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()

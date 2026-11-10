"""Run native QA through the official WinShun 0.23+ runner on a private desktop.

Arguments match test_native.py. Run from an elevated developer shell to test
its elevated environment and own-folder Qt constraint. No installed plugin,
protected administrator copy, user clipboard or visible desktop is modified.
"""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid

import test_native as qa
from test_native import api, w

DirectProcess = qa.NativeProcess


class RunnerProcess:
    def __init__(self, args, env, desktop, diagnostics):
        self.output = Path(diagnostics.name).parent
        self.events = []
        self.failure = None
        self.code = None
        self.ready = threading.Event()
        self.finished = threading.Event()
        self.hello = threading.Event()
        self.lock = threading.Lock()
        self.key = uuid.uuid4().hex
        name = 'WinShun_ST_QA_' + uuid.uuid4().hex
        self.pipe = api('kernel32', 'CreateNamedPipeW', w.HANDLE, w.LPCWSTR, w.DWORD,
                        w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, ctypes.c_void_p)(
            '\\\\.\\pipe\\' + name, 3 | 0x80000, 0, 1, 65536, 65536, 0, None)
        if self.pipe == ctypes.c_void_p(-1).value:
            raise ctypes.WinError()
        read, write = os.pipe()
        self.stdout = os.fdopen(read, 'rb', buffering=0)
        self.stdout_writer = os.fdopen(write, 'wb', buffering=0)
        self.stdin = self
        host = Path(env['WS_QT_DIR']).resolve()
        self.runner = DirectProcess([str(host/'WinShunPluginHost.exe'), '--pipe', name,
                                     '--key', self.key], env, desktop, diagnostics)

        def receive():
            try:
                connected = api('kernel32', 'ConnectNamedPipe', w.BOOL, w.HANDLE, ctypes.c_void_p)(self.pipe, None)
                if not connected and ctypes.get_last_error() != 535:
                    raise ctypes.WinError()
                buffer = ctypes.create_string_buffer(65536)
                pending = b''
                while True:
                    available = w.DWORD()
                    if not api('kernel32', 'PeekNamedPipe', w.BOOL, w.HANDLE, ctypes.c_void_p,
                               w.DWORD, ctypes.c_void_p, ctypes.POINTER(w.DWORD), ctypes.c_void_p)(
                            self.pipe, None, 0, None, ctypes.byref(available), None):
                        if self.finished.is_set():
                            break
                        raise ctypes.WinError()
                    if not available.value:
                        time.sleep(.01)
                        continue
                    count = w.DWORD()
                    if not api('kernel32', 'ReadFile', w.BOOL, w.HANDLE, ctypes.c_void_p,
                               w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p)(
                            self.pipe, buffer, min(len(buffer), available.value), ctypes.byref(count), None):
                        if not self.finished.is_set():
                            raise ctypes.WinError()
                        break
                    if not count.value:
                        break
                    pending += buffer.raw[:count.value]
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        if line.startswith(b'1 '):
                            self.stdout_writer.write(line[2:] + b'\n')
                            continue
                        assert line.startswith(b'_ '), line
                        event = json.loads(line[2:])
                        if event['op'] == 'hello':
                            assert event['key'] == self.key
                            event.pop('key')
                            self.hello.set()
                        self.events.append(event)
                        if event['op'] == 'started':
                            self.ready.set()
                        elif event['op'] == 'stderr':
                            diagnostics.write((event['text'] + '\n').encode('utf-8'))
                            diagnostics.flush()
                        elif event['op'] == 'failed':
                            raise RuntimeError(event)
                        elif event['op'] == 'exited':
                            self.code = event['code']
                            self.finished.set()
                            return
            except BaseException as exc:
                self.failure = str(exc)
                self.ready.set()
                self.hello.set()
                self.finished.set()
            finally:
                self.stdout_writer.close()

        self.reader = threading.Thread(target=receive, daemon=True)
        self.reader.start()
        try:
            assert self.hello.wait(10) and not self.failure, self.failure or 'runner hello timed out'
            # Let the runner alone set Qt paths. This catches missing injections.
            extra = [[key, value] for key, value in env.items()
                     if key.startswith('WS_') and key not in ('WS_QT_DIR', 'WS_QT_QML_DIR')]
            self.control(dict(op='start', id=1, exe=str(Path(args[0]).resolve()), args=args[1:],
                              cwd=str(Path(args[0]).resolve().parent), env=extra, qtDir=str(host),
                              memoryMB=1024, processes=True, clipboardRead=True, clipboardWrite=True,
                              ui=True, strict=False, encoding='utf-8', stdin=True))
            assert self.ready.wait(10) and not self.failure, self.failure or 'runner start timed out'
        except BaseException:
            self.kill()
            self.close()
            raise

    def send(self, data):
        with self.lock:
            count = w.DWORD()
            if not api('kernel32', 'WriteFile', w.BOOL, w.HANDLE, ctypes.c_void_p, w.DWORD,
                       ctypes.POINTER(w.DWORD), ctypes.c_void_p)(self.pipe, data, len(data), ctypes.byref(count), None):
                raise ctypes.WinError()
            assert count.value == len(data)

    def control(self, value):
        self.send(b'_ ' + json.dumps(value).encode('utf-8') + b'\n')

    def write(self, value):
        for line in value.splitlines():
            self.send(b'1 ' + line + b'\n')
        return len(value)

    def flush(self):
        pass

    def wait(self, timeout=75):
        if not self.finished.wait(timeout):
            raise subprocess.TimeoutExpired('official runner probe', timeout)
        if self.failure:
            raise RuntimeError(self.failure)
        return self.code

    def kill(self):
        self.control(dict(op='stop', id=1))

    def close(self):
        try:
            self.control(dict(op='shutdown'))
            self.runner.wait(10)
        finally:
            self.reader.join(5)
            self.stdout.close()
            self.runner.close()
            api('kernel32', 'CloseHandle', w.BOOL, w.HANDLE)(self.pipe)
            (self.output/'runner-events.json').write_text(
                json.dumps(self.events, ensure_ascii=False, indent=2), 'utf-8')


if __name__ == '__main__':
    qa.NativeProcess = RunnerProcess
    qa.main()

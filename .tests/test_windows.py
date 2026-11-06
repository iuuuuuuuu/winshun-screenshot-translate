"""Run the actual Qt UI/OCR checks on a private desktop in a fresh process."""
import os
from pathlib import Path
import subprocess
import sys
import unittest
import time
import ctypes
from ctypes import wintypes as w


@unittest.skipUnless(os.name == "nt", "Windows Qt and OCR")
class WindowsTests(unittest.TestCase):
    def test_hovered_close_buttons_and_escape_still_dismiss_windows(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run([sys.executable, "-B", str(root / ".tests/close_hover_probe.py")],
                                cwd=root, capture_output=True, text=True, encoding="utf-8",
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=12)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"escapeClosesPin": true', result.stdout)

    def test_native_selector_and_outside_clicks_run_without_blocking_qt(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run([sys.executable, "-B", str(root / ".tests/native_probe.py")],
                                cwd=root, capture_output=True, text=True, encoding="utf-8",
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=12)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("queued raw mouse clicks with blocked Qt", result.stdout)

    def test_closing_a_busy_pin_exits_immediately_and_reaps_child_processes(self):
        from desktop import api
        root = Path(__file__).resolve().parents[1]
        process = subprocess.Popen([sys.executable, "-B", str(root / ".tests/fast_exit_probe.py")],
                                   cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding="utf-8",
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        handle = None
        lines = []
        try:
            for line in process.stdout:
                lines.append(line)
                if line.startswith("CHILD "):
                    handle = api("kernel32", "OpenProcess", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)(0x100000, False, int(line.split()[1]))
                    self.assertTrue(handle)
                if line.startswith("CLOSED "):
                    self.assertLess(float(line.split()[1]), 100)
                    closed_at = time.monotonic()
                    process.wait(timeout=.75)
                    self.assertLess(time.monotonic() - closed_at, .75)
                    break
            self.assertEqual(process.returncode, 0, "".join(lines) + process.stderr.read())
            self.assertTrue(any(line.startswith("CLOSED ") for line in lines), "".join(lines))
            self.assertEqual(api("kernel32", "WaitForSingleObject", w.DWORD, w.HANDLE, w.DWORD)(handle, 1000), 0)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            if handle:
                api("kernel32", "CloseHandle", w.BOOL, w.HANDLE)(handle)
            for stream in (process.stdin, process.stdout, process.stderr):
                stream.close()

    def test_closing_settings_during_batch_does_not_keep_ui_process_alive(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run([sys.executable, "-B", str(root / ".tests/installed_ui_probe.py"), "--source", "--busy-tests"],
                                cwd=root, capture_output=True, text=True, encoding="utf-8",
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=12)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("closing state", result.stdout)

    def test_gui_worker_exits_after_last_qml_window_closes(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run([sys.executable, "-B", str(root / ".tests/installed_ui_probe.py"), "--source"],
                                cwd=root, capture_output=True, text=True, encoding="utf-8",
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=12)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("closing state", result.stdout)

    def test_private_desktop_ui_actions_layout_profiles_ocr_and_cleanup(self):
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run([sys.executable, "-B", str(root / ".tests/qt_probe.py")],
                                cwd=root, capture_output=True, text=True, encoding="utf-8",
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=35)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS: Qt", result.stdout)


if __name__ == "__main__":
    unittest.main()

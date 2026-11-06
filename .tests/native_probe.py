"""Real native message loops on a private desktop; never send system input."""
import ctypes
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
from unittest.mock import Mock

runtime = (Path(os.environ["LOCALAPPDATA"]) / "WinShun/plugins/iuuuuuuuu.screenshot-translate"
           if "--installed" in sys.argv else Path(__file__).resolve().parents[1])
sys.path.insert(0, str(runtime))
from qt_probe import isolate, pump
isolate()

import desktop
from native_ui import Region, OutsideClick, RawHeader, RawMouse

output = Path(".test-output/native-performance" + ("-installed" if "--installed" in sys.argv else ""))
output.mkdir(parents=True, exist_ok=True)
report = {}
result = []
region = Region(result.append)
region.show()
assert region.ready.wait(2) and region.error is None, region.error
assert region.show_ms < 150, region.show_ms
assert "PySide6" not in sys.modules
post = desktop.api("user32", "PostMessageW", w.BOOL, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
for display, handle in zip(desktop.monitors(), region.masks):
    bounds = desktop.Rect()
    assert desktop.api("user32", "GetWindowRect", w.BOOL, w.HWND, ctypes.POINTER(desktop.Rect))(handle, ctypes.byref(bounds))
    assert (bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top) == display["rect"]
points = iter([(30, 40), (3100, 650), (3100, 650)])
region.current_point = lambda: next(points)
assert post(region.masks[0], 0x201, 1, 0)
assert post(region.masks[0], 0x200, 1, 0)
assert post(region.masks[0], 0x202, 0, 0)
assert region.stopped.wait(2)
assert result == [(30, 40, 3070, 610)], result
assert not any(desktop.api("user32", "IsWindow", w.BOOL, w.HWND)(handle) for handle in region.windows)
report["selectorShownMs"] = region.show_ms

# The resident route must show the selector before creating a Qt worker.
from broker import Broker
from fixtures import memory_stats
resident = Broker()
resident.open_child = Mock()
resident.start_shot("region")
selector = resident.native_region
assert selector.ready.wait(2) and not selector.error
assert resident.child is None and "PySide6" not in sys.modules
assert resident.gate.busy
resident.start_shot("region")  # a held/repeated hotkey does not enqueue a second
assert resident.native_region is selector
selector.finish((10, 20, 300, 100))
assert selector.stopped.wait(2)
resident.tick()
resident.open_child.assert_called_once_with({"method": "ui/capture", "params": {"rect": (10, 20, 300, 100)}})
resident.gate.end()
report["residentAfterSelectorMB"] = memory_stats()["workingMB"]

from main import App, Job
from ui import Pin
from services import Profiles, Usage, Translator
from fixtures import image_fixture
app = App()
app.copy = Mock()
with tempfile.TemporaryDirectory() as directory:
    app.profiles, app.usage = Profiles(directory), Usage(directory)
    app.translator = Translator(app.usage)
    pin = Pin(app, image_fixture(800, 400))
    app.pin = pin
    pin.show()
    pump(app)
    observer = pin.outside_click
    assert observer.input.ready.wait(2) and observer.input.error is None

    # Feed RAWMOUSE packets through the real independent HWND message pump.
    # No SendInput, physical pointer movement, foreground desktop or clipboard.
    header_size = ctypes.sizeof(RawHeader)
    packet = ctypes.create_string_buffer(header_size + ctypes.sizeof(RawMouse))
    header = RawHeader.from_buffer(packet)
    header.kind, header.size = 0, len(packet)
    mouse = RawMouse.from_buffer(packet, header_size)
    def read_raw(handle, command, target, size, header_bytes):
        ctypes.cast(size, ctypes.POINTER(w.UINT)).contents.value = len(packet)
        if target:
            ctypes.memmove(target, packet, len(packet))
            return len(packet)
        return 0
    observer.input.read_raw = read_raw
    point = desktop.Rect()
    desktop.api("user32", "GetWindowRect", w.BOOL, w.HWND, ctypes.POINTER(desktop.Rect))(int(pin.window.winId()), ctypes.byref(point))
    observer.input.current_point = lambda: (point.left + 20, point.top + 20)
    mouse.split.flags = 1
    post(observer.input.sink, 0xff, 0, 1234)
    time.sleep(.04)
    assert app.events.empty()  # clicking inside preserves the pin
    observer.input.current_point = lambda: (-30000, -30000)
    mouse.split.flags = 0
    post(observer.input.sink, 0xff, 0, 1234)
    time.sleep(.04)
    assert app.events.empty()  # mouse movement never closes a pin
    mouse.split.flags = 1
    started = time.perf_counter()
    post(observer.input.sink, 0xff, 0, 1234)
    # Deliberately stop pumping Qt. Raw notifications must still arrive.
    deadline = time.monotonic() + .25
    while app.events.empty() and time.monotonic() < deadline:
        time.sleep(.002)
    assert not app.events.empty(), "outside click was lost while UI was busy"
    report["outsideNotificationMs"] = (time.perf_counter() - started) * 1000
    app.events.get_nowait()()
    assert pin.closed and not pin.window.isVisible() and not pin.toolbar_window.isVisible()
    assert observer.input.stopped.wait(2)
    pump(app)

    pin = Pin(app, image_fixture(800, 400))
    app.pin = pin
    pin.show()
    pump(app)
    worker = desktop.Worker()
    worker.lock.acquire()
    try:
        started = time.perf_counter()
        worker.cancel()
        assert (time.perf_counter() - started) < .03
    finally:
        worker.lock.release()
    app.gate.begin()
    job = Job()
    job.pin, job.worker = pin, worker
    app.job = job
    app.jobs.add(job)
    started = time.perf_counter()
    pin.close()
    report["closeCallbackMs"] = (time.perf_counter() - started) * 1000
    assert report["closeCallbackMs"] < 100, report
    assert app.job is None and not app.gate.busy and job.cancelled.is_set()
    assert not pin.window.isVisible() and not pin.toolbar_window.isVisible()
    pump(app)
    app.jobs.clear()

    # Toggling rapidly must not let an old thread unregister a new input sink.
    pin = Pin(app, image_fixture(800, 400))
    app.pin = pin
    pin.show()
    pump(app)
    for _ in range(4):
        app.settings["closeOnOutsideClick"] = False
        pin.apply_preferences()
        app.settings["closeOnOutsideClick"] = True
        pin.apply_preferences()
    observer = pin.outside_click
    assert observer.input.ready.wait(2)
    assert OutsideClick.registration_owner == observer.input.sink
    pin.close()
    assert observer.input.stopped.wait(2)
    pump(app)
app.root.destroy()
(output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print("PASS: native selector, queued raw mouse clicks with blocked Qt, immediate cancellation and close", json.dumps(report))

"""Measure a never-shown resident after synthetic translation windows close."""
import gc
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from desktop import Worker
from fixtures import image_fixture, memory_stats
from qt_probe import isolate
isolate()
from main import App
from ui import Pin

app = App()
initial = memory_stats()
peaks, after = [], []
for i in range(12):
    width, height = ((600, 180), (1920, 1080), (2560, 1440))[i % 3]
    image = image_fixture(width, height)
    if i in (0, 3, 6, 9):
        worker = Worker()
        blocks = worker.recognize(image)
        assert blocks and worker.process is None
        del worker, blocks
    pin = Pin(app, image)
    assert not pin.window.isVisible()
    pin.set_result([{"text": "Hello world", "box": [24, 20, 190, 28], "glyphHeight": 28}],
                   {"source": "Hello world", "translated": "\u4f60\u597d\u4e16\u754c", "segments": ["\u4f60\u597d\u4e16\u754c"],
                    "sourceLanguage": "en", "targetLanguage": "zh"})
    peaks.append(memory_stats())
    pin.close()
    assert not pin.provider.images
    del pin, image
    app.root.update()
    gc.collect()
    after.append(memory_stats())
log = {"initial": initial, "peak": max(peaks, key=lambda m: m["workingMB"]),
       "after12": after[-1], "workerExited": True, "imageHandlesReleased": True}
print(json.dumps(log), flush=True)
seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 60
deadline = time.monotonic() + seconds
while time.monotonic() < deadline:
    app.root.update()
    time.sleep(.2)
gc.collect()
print(json.dumps({"idleSeconds": seconds, "idle": memory_stats()}), flush=True)
app.root.destroy()

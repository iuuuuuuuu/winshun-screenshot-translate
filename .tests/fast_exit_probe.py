"""Close a real disposable worker while its network thread is still blocked."""
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import subprocess

runtime = (Path(os.environ["LOCALAPPDATA"]) / "WinShun/plugins/iuuuuuuuu.screenshot-translate"
           if "--installed" in sys.argv else Path(__file__).resolve().parents[1])
sys.path.insert(0, str(runtime))
from qt_probe import isolate
isolate()
from main import UiWorkerApp
from ui import Pin
from fixtures import image_fixture

with tempfile.TemporaryDirectory() as directory:
    app = UiWorkerApp()
    app.initialize(1, {"dataDir": directory})
    app.pin = pin = Pin(app, image_fixture(800, 400))
    pin.show()
    app.started = True
    # Keep the simulated protocol connection open; do not let stdin EOF cause
    # shutdown before the scheduled close action is exercised.
    app.read = lambda: threading.Event().wait()
    # This child is intentionally not managed by Worker.cancel. The Windows
    # disposable job must reap it when the UI exits.
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                             creationflags=subprocess.CREATE_NO_WINDOW,
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("CHILD", child.pid, flush=True)
    blocked = threading.Event()
    app.launch(lambda job: blocked.wait(30), lambda *args: None, pin=pin)
    def finish():
        started = time.monotonic()
        pin.close()
        print("CLOSED", round((time.monotonic() - started) * 1000, 1), flush=True)
    app.root.after(150, finish)
    app.run()

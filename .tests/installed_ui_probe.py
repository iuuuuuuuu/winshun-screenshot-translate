"""Load the installed plugin's Qt assets on an isolated desktop and auto-close."""
from pathlib import Path
import os
import sys
import tempfile
import faulthandler
faulthandler.dump_traceback_later(8, exit=True)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qt_probe import isolate
isolate()
installed = (Path(__file__).resolve().parents[1] if "--source" in sys.argv else
             Path(os.environ["LOCALAPPDATA"]) / "WinShun/plugins/iuuuuuuuu.screenshot-translate")
sys.path.insert(0, str(installed))
from main import UiWorkerApp, VERSION

with tempfile.TemporaryDirectory() as directory:
    os.environ["WS_DATA_DIR"] = directory
    app = UiWorkerApp()
    messages = []
    app.send = messages.append
    app.initialize(1, {"dataDir": directory, "locale": "zh-CN"})
    app.dispatch({"method": "showSettings"})
    if "--busy-tests" in sys.argv:
        import threading
        from unittest.mock import Mock
        blocked = threading.Event()
        app.translator = Mock(translate=lambda *args: blocked.wait(30))
        app.config.action("testAll")
        assert app.config.testing
    def finish():
        assert app.config is not None
        output = Path(__file__).resolve().parents[1] / ".test-output/installed-settings.png"
        output.parent.mkdir(parents=True, exist_ok=True)
        assert app.config.window.grabWindow().save(str(output))
        app.config.close()
    app.root.after(250, finish)
    app.root.after(0, app.tick)
    app.root.mainloop()
    faulthandler.cancel_dump_traceback_later()
    assert not app.running and app.config is None
    assert any(m.get("params", {}).get("closing") for m in messages)
    app.root.destroy()
    print("PASS: installed", VERSION, "Qt settings loaded, rendered and exited with closing state")

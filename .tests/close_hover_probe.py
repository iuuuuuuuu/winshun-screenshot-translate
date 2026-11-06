"""Exercise real hover and click delivery on a private desktop only."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time

repo = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repo / ".tests"))
from qt_probe import isolate, pump
isolate()
runtime = (Path(os.environ["LOCALAPPDATA"]) / "WinShun/plugins/iuuuuuuuu.screenshot-translate"
           if "--installed" in sys.argv else repo)
sys.path.insert(0, str(runtime))
from PySide6.QtCore import QCoreApplication, QEvent, QObject, QPointF, Qt, qInstallMessageHandler
from PySide6.QtGui import QEnterEvent, QMouseEvent, QKeyEvent
from PySide6.QtQml import QQmlExpression, qmlContext
from main import App, VERSION
from ui import Pin, Settings
from fixtures import image_fixture
from services import Profiles, Usage, Translator

warnings = []
qInstallMessageHandler(lambda kind, context, message: warnings.append(message))
app = App()
app.settings["closeOnOutsideClick"] = False
output = repo / ".test-output" / ("close-hover-installed" if "--installed" in sys.argv else "close-hover-source")
output.mkdir(exist_ok=True)
results = []


def exercise(owner, view, name):
    window = view.window
    owner.show()
    pump(app)
    buttons = [item for item in window.findChildren(QObject)
               if item.property("glyph") == "\ue8bb" and item.property("tip")]
    assert len(buttons) == 1
    button = buttons[0]
    local = button.mapToScene(QPointF(button.width() / 2, button.height() / 2))
    global_point = window.mapToGlobal(local)
    QCoreApplication.sendEvent(window, QEnterEvent(local, local, global_point))
    QCoreApplication.sendEvent(window, QMouseEvent(
        QEvent.MouseMove, local, global_point, Qt.NoButton, Qt.NoButton, Qt.NoModifier))
    pump(app, .75)  # Longer than the real tooltip delay.
    hovered = [item for item in button.findChildren(QObject)
               if "HoverHandler" in item.metaObject().className() and item.property("hovered")]
    assert hovered, "Synthetic mouse must really hover over the close button"
    tooltip = QQmlExpression(qmlContext(button), button, "ToolTip.visible")
    assert tooltip.evaluate()[0] is False and not tooltip.hasError(), tooltip.error().toString()
    label = QQmlExpression(qmlContext(button), button, "Accessible.name")
    assert label.evaluate()[0] in ("关闭 · Esc", "关闭")
    assert window.grabWindow().save(str(output / (name + ".png")))
    started = time.perf_counter()
    for event_type, buttons_down in ((QEvent.MouseButtonPress, Qt.LeftButton),
                                      (QEvent.MouseButtonRelease, Qt.NoButton)):
        QCoreApplication.sendEvent(window, QMouseEvent(
            event_type, local, global_point, Qt.LeftButton, buttons_down, Qt.NoModifier))
    elapsed = (time.perf_counter() - started) * 1000
    assert owner.closed and not window.isVisible(), "Real click delivery must hide the window"
    pump(app)
    results.append({"window": name, "hoveredWithoutTooltip": True, "clickCloseMs": round(elapsed, 2)})


with tempfile.TemporaryDirectory() as directory:
    app.profiles, app.usage = Profiles(directory), Usage(directory)
    app.translator = Translator(app.usage)
    pin = Pin(app, image_fixture())
    exercise(pin, pin.bar, "pin-toolbar")
    settings = Settings(app)
    exercise(settings, settings.view, "settings")
    pin = Pin(app, image_fixture())
    pin.show()
    pump(app)
    close_button = next(item for item in pin.toolbar_window.findChildren(QObject)
                        if item.property("glyph") == "\ue8bb" and item.property("tip"))
    close_button.forceActiveFocus(Qt.OtherFocusReason)
    QCoreApplication.sendEvent(pin.toolbar_window, QKeyEvent(QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    assert pin.closed, "Esc must still close the pin"
    pump(app)
app.root.destroy()
assert not warnings, warnings
report = {"version": VERSION, "checks": results, "escapeClosesPin": True, "qmlWarnings": warnings}
(output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))

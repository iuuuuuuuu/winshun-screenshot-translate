"""Exercise the host patch's real QML with Qt's offscreen platform."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["QT_QUICK_BACKEND"] = "software"
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / ".test-output/qt"))
from PySide6.QtCore import QObject, QPoint, QUrl, Qt
from PySide6.QtGui import QGuiApplication, QFont, QFontDatabase
from PySide6.QtQuick import QQuickView
from PySide6.QtTest import QTest

THEME = '''pragma Singleton
import QtQuick
QtObject {
    property int fontBody: 14
    property int fontStrong: Font.Bold
    property int largeCardRadius: 8
    property int largeControlRadius: 6
    property color track: "#ededed"
    property color chipActive: "#ffffff"
    property color chipHover: "#e4e4e4"
    property color chipBorder: "#dcdcdc"
    property color accent: "#0067c0"
    property color text: "#1b1b1b"
    property color subtext: "#666666"
    function snap(v, dpr) { return Math.round(v * dpr) / dpr }
    function snapUp(v, dpr) { return Math.ceil(v * dpr) / dpr }
}'''
HARNESS = '''import QtQuick
import WinShun
Rectangle {
    width: 560; height: 180; color: "#f3f3f3"
    ChoiceTabs {
        id: tabs; objectName: "tabs"; x: 12; y: 12
        width: parent.width - 24
        labels: ["MyMemory", "Google", "Edge", "Zvo", "Microsoft", "DeepL", "OpenAI", "DeepSeek", "Doubao", "Hunyuan"]
        onActivated: index => current = index
    }
}'''


def run():
    app = QGuiApplication([])
    font_id = QFontDatabase.addApplicationFont("C:/Windows/Fonts/segoeui.ttf")
    assert font_id >= 0, "An actual font is required for width and pixel validation"
    app.setFont(QFont(QFontDatabase.applicationFontFamilies(font_id)[0]))
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        module = directory / "WinShun"
        module.mkdir()
        (module / "qmldir").write_text("module WinShun\nsingleton Theme 1.0 Theme.qml\nChoiceTabs 1.0 ChoiceTabs.qml\n", encoding="utf-8")
        (module / "Theme.qml").write_text(THEME, encoding="utf-8")
        shutil.copyfile(root / ".host/WinShun/src/app/qml/ChoiceTabs.qml", module / "ChoiceTabs.qml")
        (directory / "test.qml").write_text(HARNESS, encoding="utf-8")
        view = QQuickView()
        view.engine().addImportPath(str(directory))
        view.setSource(QUrl.fromLocalFile(str(directory / "test.qml")))
        assert not view.errors(), [e.toString() for e in view.errors()]
        view.setResizeMode(QQuickView.SizeRootObjectToView)
        view.show()
        tabs = view.rootObject().findChild(QObject, "tabs")
        def visual_children(item):
            for child in item.childItems():
                yield child
                yield from visual_children(child)
        for width in (320, 560, 1000, 400):
            view.resize(width, 260)
            QTest.qWait(80)
            lookup = {c.objectName(): c for c in visual_children(tabs)}
            chips = [lookup[f"choice-{i}"] for i in range(10)]
            boxes = [(c.x(), c.y(), c.width(), c.height()) for c in chips]
            assert len(chips) == 10
            for i, (x, y, w, h) in enumerate(boxes):
                assert lookup[f"label-{i}"].property("lineCount") == 1, f"A short service name unexpectedly wrapped: {i}"
                assert x >= 0 and y >= 0 and x + w <= tabs.width() - 6 + 1
                assert y + h <= tabs.height() - 6 + 1
                for xx, yy, ww, hh in boxes[:i]:
                    assert min(x + w, xx + ww) <= max(x, xx) or min(y + h, yy + hh) <= max(y, yy)
            if width < 600:
                assert tabs.height() > 40
            for i, chip in enumerate(chips):
                center = chip.mapToScene(chip.boundingRect().center())
                QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(round(center.x()), round(center.y())))
                assert tabs.property("current") == i
            QTest.keyClick(view, Qt.Key_Left)
            assert tabs.property("current") == 8
            view.grabWindow().save(str(root / f".test-output/host-choices-{width}.png"))
        tabs.setProperty("labels", ["A very long service label " * 8, "Short"])
        view.resize(240, 600)
        QTest.qWait(80)
        first = next(c for c in visual_children(tabs) if c.objectName() == "choice-0")
        assert first.width() <= tabs.width() - 6 and first.height() > 32
        view.close()
        view.setSource(QUrl())
    print("PASS: real host QML, 4 widths, all 10 choices, exclusive selection, keyboard, long labels")


if __name__ == "__main__":
    run()

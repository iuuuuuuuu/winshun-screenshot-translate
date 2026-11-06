"""Actual Qt render/action tests on a private desktop, with synthetic pixels."""
import ctypes
from ctypes import wintypes as w
import gc
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
import uuid
from unittest.mock import Mock, patch

RUNTIME = (Path(os.environ["LOCALAPPDATA"]) / "WinShun/plugins/iuuuuuuuu.screenshot-translate"
           if "--installed" in sys.argv else Path(__file__).resolve().parents[1])
sys.path.insert(0, str(RUNTIME))
from desktop import api, Image, png_rgb


def isolate():
    name = "WinShunQt-" + uuid.uuid4().hex
    handle = api("user32", "CreateDesktopW", w.HANDLE, w.LPCWSTR, ctypes.c_void_p,
                 ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p)(name, None, None, 0, 0x1ff, None)
    if not handle or not api("user32", "SetThreadDesktop", w.BOOL, w.HANDLE)(handle):
        raise OSError("Private desktop unavailable; refusing to use the user's desktop")
    os.environ["QSG_RHI_BACKEND"] = "software"
    os.environ["QT_QUICK_BACKEND"] = "software"
    return handle


def pump(app, seconds=.12):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        app.root.update()
        time.sleep(.005)


def run():
    isolate()
    if "--gpu" in sys.argv:
        os.environ.pop("QT_QUICK_BACKEND", None)
        os.environ["QSG_RHI_BACKEND"] = "d3d11"
    from PySide6.QtCore import qInstallMessageHandler, QObject, Qt, QMetaObject
    from PySide6.QtGui import QImage, QPainter, QColor, QFont
    from main import App
    from ui import Pin, Settings, Theme, Region
    from services import Profiles, Usage, Translator
    from fixtures import image_fixture, memory_stats
    warnings = []
    def warning(kind, context, message):
        warnings.append(message)
        print(message, file=sys.stderr)
    qInstallMessageHandler(warning)
    suffix = ("-network" if "--network" in sys.argv else "") + ("-scale" + os.environ["QT_SCALE_FACTOR"] if os.environ.get("QT_SCALE_FACTOR") else "")
    suffix += ("-installed" if "--installed" in sys.argv else "") + ("-gpu" if "--gpu" in sys.argv else "")
    output = Path(".test-output/qt-ui" + suffix)
    output.mkdir(parents=True, exist_ok=True)
    app = App()
    app.copy, app.retranslate = Mock(), Mock()
    with tempfile.TemporaryDirectory() as directory:
        app.profiles, app.usage = Profiles(directory), Usage(directory)
        app.translator = Translator(app.usage)
        started = time.monotonic()
        config = Settings(app)
        settings_load = time.monotonic() - started
        config.show()
        pump(app)
        assert config.window.grabWindow().save(str(output / "settings-free.png"))
        config.action("select", 6)
        config.action("apiKey", "test-only")
        config.action("model", "draft-model")
        config.action("select", 7)
        assert config.backend.values["apiKey"] == ""
        config.action("select", 6)
        assert config.backend.values["model"] == "draft-model"
        pump(app)
        assert config.window.grabWindow().save(str(output / "settings.png"))
        config.window.setWidth(640)
        config.window.setHeight(420)
        pump(app)
        assert config.window.grabWindow().save(str(output / "settings-narrow.png"))
        tabs = config.window.findChild(QObject, "service-tabs")
        assert tabs is not None and tabs.property("implicitHeight") > 45
        config.close()
        pump(app)
        assert app.profiles.get("openai")["model"] == ""
        assert app.active_provider() == "mymemory"
        config = Settings(app)
        real_translator = app.translator
        import threading
        calls, active = [], [0, 0]
        lock, release = threading.Lock(), threading.Event()
        def translate(profile, parts, source, target):
            assert profile["fallbackEnabled"] is False
            assert parts == ["Hello world"]
            with lock:
                calls.append(dict(profile))
                active[0] += 1
                active[1] = max(active)
            release.wait(2)
            with lock:
                active[0] -= 1
            if profile["provider"] == "google":
                raise ValueError("HTTP 429")
            return {"translated": "你好"}
        app.translator = Mock(translate=translate)
        config.action("select", 6)
        config.action("apiKey", "draft-test-key")
        config.action("testAll")
        pump(app)
        assert config.testing and active[1] <= 3
        # A network batch must leave the UI free to switch services.
        config.action("select", 9)
        assert config.selected == "hunyuan"
        config.show()
        pump(app)
        config.window.grabWindow().save(str(output / "settings-testing.png"))
        release.set()
        deadline = time.monotonic() + 3
        while config.testing and time.monotonic() < deadline:
            while not app.events.empty():
                app.events.get_nowait()()
            pump(app, .01)
        assert not config.testing
        assert len(config.test_results) == 10
        assert config.test_results["google"]["status"] == "failed"
        assert config.test_results["deepl"]["status"] == "skipped"
        assert {p["provider"] for p in calls} == {"mymemory", "google", "edge", "zvo", "openai"}
        assert next(p for p in calls if p["provider"] == "openai")["apiKey"] == "draft-test-key"
        assert active[1] == 3
        assert app.active_provider() == "mymemory"
        config.action("showTests")
        pump(app)
        config.window.grabWindow().save(str(output / "settings-test-results.png"))
        config.close()
        pump(app)
        app.translator = real_translator
        config = Settings(app)
        config.action("select", 6)
        config.action("model", "saved-model")
        config.save()
        pump(app)
        assert app.profiles.get("openai")["model"] == "saved-model"
        assert app.active_provider() == "mymemory"
        picture = QImage(680, 154, QImage.Format_RGB32)
        picture.fill(QColor("#f6f7f8"))
        painter = QPainter(picture)
        font = QFont("Segoe UI"); font.setPixelSize(20); painter.setFont(font)
        painter.setPen(QColor("#202020"))
        for x, label in ((40, "Overview"), (204, "Pull requests"), (402, "Actions"), (542, "Security")):
            painter.drawText(x, 42, label)
        painter.drawText(24, 113, "Keep text in its original position.")
        painter.setPen(QColor("#666666"))
        painter.drawEllipse(18, 26, 12, 12)
        painter.end()
        picture = picture.convertToFormat(QImage.Format_RGB888)
        rgb = bytes(picture.constBits())
        sample = Image((100, 100, 680, 154), 680, 154, rgb, png_rgb(680, 154, rgb))
        from desktop import Worker
        blocks = Worker().recognize(sample)
        assert len(blocks) >= 5
        # Fixed translations of the synthetic fixture, independent of OCR
        # spelling/capitalization differences between Windows language packs.
        labels = ((40, "概览"), (204, "拉取请求"), (402, "操作"), (542, "安全"))
        translated = [min(labels, key=lambda pair: abs(pair[0] - b["box"][0]))[1]
                      if b["box"][1] < 70 else "保持文字在原来的位置。" for b in blocks]
        live_data = None
        if "--network" in sys.argv:
            settings = {"provider": "google", "proxy": "http://127.0.0.1:7890",
                        "fallbackEnabled": True, "fallbackRecoveryMinutes": 1}
            live_data = app.translator.translate(app.profiles.current(settings),
                                                [b["text"] for b in blocks], "en", "zh")
            translated = live_data["segments"]
            assert live_data["fallback"] and live_data["preferredProvider"] == "google"
            print("PASS: real Google failure -> " + live_data["provider"] + " fallback, "
                  + str(len(translated)) + " OCR blocks", flush=True)
        pin = Pin(app, sample)
        pin.show()
        pump(app)
        capture_size = pin.window.size()
        pin.window.grabWindow().save(str(output / "loading.png"))
        pin.set_source(blocks, "en", "zh")
        pin.update_status("正在翻译…")
        pump(app)
        assert pin.window.size() == capture_size
        pin.update_status("HTTP 429", False)
        pin.copy(True)
        assert app.copy.call_count == 1
        pin.action("target", "en")
        app.retranslate.assert_called_with(pin)
        pin.set_result(blocks, live_data or {"source": "\n".join(b["text"] for b in blocks), "translated": "\n".join(translated),
                                           "segments": translated, "sourceLanguage": "en", "targetLanguage": "zh"})
        pump(app)
        assert pin.window.size() == capture_size
        def save_pair(name):
            top = pin.window.grabWindow()
            bar = pin.toolbar_window.grabWindow()
            width = max(top.width(), bar.width()) + 24
            image = QImage(width, top.height() + bar.height() + 36, QImage.Format_RGB32)
            image.fill(QColor("#f3f3f3"))
            painter = QPainter(image)
            painter.drawImage((width - top.width()) // 2, 12, top)
            painter.drawImage((width - bar.width()) // 2, top.height() + 24, bar)
            painter.end()
            assert image.save(str(output / name))
        save_pair("pin.png")
        render_version = pin.version
        pin.action("compare")
        pump(app)
        assert pin.version == render_version  # comparison does not repaint OCR
        source_item = pin.window.findChild(QObject, "source-image")
        result_item = pin.window.findChild(QObject, "translation-image")
        assert result_item.property("y") < source_item.property("y")
        assert result_item.property("x") == source_item.property("x")
        save_pair("compare.png")
        app.settings["comparisonOrder"] = "source-first"
        pin.redraw()
        pump(app)
        assert result_item.property("y") > source_item.property("y")
        save_pair("compare-source-first.png")
        app.settings["comparisonOrder"] = "translation-first"
        pin.redraw()
        for name in ("source-menu", "target-menu"):
            menu = pin.toolbar_window.findChild(QObject, name)
            assert menu is not None
            QMetaObject.invokeMethod(menu, "open")
            assert menu.isVisible()
            pump(app)
            # Windows may briefly activate and then deactivate a window on a
            # desktop that is never switched to. Inspect the already-opened
            # menu without requesting foreground activation again.
            if not menu.isVisible() and not menu.isActive():
                menu.setProperty("receivedFocus", False)
                menu.show()
                pump(app)
            assert menu.isVisible()
            assert menu.height() > pin.toolbar_window.height() * 5
            assert menu.y() + menu.height() <= pin.backend.values["menuBottom"] + 1
            assert menu.grabWindow().save(str(output / (name + ".png")))
            # A click in the top-level menu counts as inside the translation UI.
            native = __import__("desktop")
            bounds = native.Rect()
            native.api("user32", "GetWindowRect", w.BOOL, w.HWND, ctypes.POINTER(native.Rect))(int(menu.winId()), ctypes.byref(bounds))
            pin.outside_click.observe((bounds.left + 10, bounds.top + 10))
            assert app.events.empty()
            click_timestamp = pin.outside_click.clock()
            QMetaObject.invokeMethod(menu, "choose", __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", 2))
            pump(app)
            assert not menu.isVisible()
            pin.outside_click.observe((bounds.left + 10, bounds.top + 10), click_timestamp)
            assert app.events.empty()  # queued click stays internal after hide
        pin.show()
        app.settings["closeOnOutsideClick"] = False
        pin.apply_preferences()
        assert pin.outside_click is None
        app.settings["closeOnOutsideClick"] = True
        pin.apply_preferences()
        assert pin.outside_click is not None
        pin.swap()
        assert app.retranslate.call_args.args == (pin, "zh", "en", translated)
        pin.action("opacity", -1)
        assert abs(pin.opacity - .95) < .001
        pin.close()
        pump(app)
        assert not pin.provider.images and pin.image is None
        pin = Pin(app, image_fixture(120, 42))
        text = "这是一段比原文更长的中文译文，保持正常字号并自动换行。" * 4
        pin.set_result([{"text": "Hello", "box": [5, 5, 100, 24], "glyphHeight": 24}],
                       {"source": "Hello", "translated": text, "segments": [text], "sourceLanguage": "en", "targetLanguage": "zh"})
        pin.show()
        pump(app)
        save_pair("small.png")
        assert pin.content_height > 42
        pin.close()
        pump(app)
        app.theme = Theme(True)
        config = Settings(app)
        config.action("select", 3)
        config.show()
        pump(app)
        config.window.grabWindow().save(str(output / "settings-dark.png"))
        config.close()
        callback = Mock()
        region = Region(app, callback)
        region.show()
        pump(app)
        display_report = []
        native = __import__("desktop")
        for surface in region.surfaces:
            bounds = native.Rect()
            assert native.api("user32", "GetWindowRect", w.BOOL, w.HWND, ctypes.POINTER(native.Rect))(int(surface.window.winId()), ctypes.byref(bounds))
            actual = (bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top)
            expected = surface.display["rect"]
            assert actual[:2] == expected[:2], (actual, expected)
            tolerance = math.ceil(surface.window.devicePixelRatio() * 2) + 1
            assert all(0 <= actual[i] - expected[i] < tolerance for i in (2, 3)), (actual, expected)
            pixels = surface.window.grabWindow()
            assert (pixels.width(), pixels.height()) == actual[2:]
            # Synthetic private-desktop mask must shade the entire last corner.
            assert pixels.pixelColor(expected[2] - 1, expected[3] - 1).alpha() > 0
            # The visible native shape is exactly the physical monitor, even
            # when its backing store needs a spare raster column.
            region_handle = native.api("gdi32", "CreateRectRgn", w.HANDLE, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int)(0, 0, 0, 0)
            try:
                assert native.api("user32", "GetWindowRgn", ctypes.c_int, w.HWND, w.HANDLE)(int(surface.window.winId()), region_handle) > 0
                shape = native.Rect()
                native.api("gdi32", "GetRgnBox", ctypes.c_int, w.HANDLE, ctypes.POINTER(native.Rect))(region_handle, ctypes.byref(shape))
                assert (shape.left, shape.top, shape.right, shape.bottom) == (0, 0, expected[2], expected[3])
            finally:
                native.api("gdi32", "DeleteObject", w.BOOL, w.HANDLE)(region_handle)
            pixels.save(str(output / ("mask-" + str(len(display_report)) + ".png")))
            display_report.append({"physical": actual, "logical": surface.window.geometry().getRect(), "dpr": surface.window.devicePixelRatio()})
        first_display = region.surfaces[0].display["rect"]
        last_display = region.surfaces[-1].display["rect"]
        start_point = (first_display[0] + 20, first_display[1] + 30)
        end_point = (last_display[0] + min(300, last_display[2] - 20),
                     last_display[1] + min(500, last_display[3] - 20))
        selection = (min(start_point[0], end_point[0]), min(start_point[1], end_point[1]),
                     abs(end_point[0] - start_point[0]), abs(end_point[1] - start_point[1]))
        with patch("desktop.physical_cursor", side_effect=[start_point, end_point, end_point]):
            region.action("begin")
            region.action("motion")
            assert region.surfaces[-1].backend.values["selectionWidth"] > 0
            region.action("end")
        deadline = time.monotonic() + 2
        while not callback.called and time.monotonic() < deadline:
            pump(app, .01)
        assert region.closed
        callback.assert_called_once_with(selection)
        # A secondary-screen pin must be correctly sized before first showing,
        # throughout OCR, and after its translated image arrives.
        secondary = Image((last_display[0] + 100, last_display[1] + 100, 680, 154), 680, 154, rgb, sample.png)
        pin = Pin(app, secondary)
        expected_dpr = region.surfaces[-1].screen.devicePixelRatio()
        initial = pin.window.size()
        pin.show()
        pump(app)
        assert pin.window.devicePixelRatio() == expected_dpr
        assert pin.window.size() == initial
        pin.set_source(blocks, "en", "zh")
        pin.update_status("正在翻译…")
        pump(app)
        assert pin.window.size() == initial
        pin.set_result(blocks, {"source": "source", "translated": "译文", "segments": translated, "sourceLanguage": "en", "targetLanguage": "zh"})
        pump(app)
        assert pin.window.size() == initial
        # Clicking a synthetic window belonging to a different process/desktop
        # is represented by a known empty point, without sending any real input.
        pin.outside_click.observe((-30000, -30000))
        while not app.events.empty():
            app.events.get_nowait()()
        assert pin.closed and pin.outside_click is None
    gc.collect()
    pump(app)
    report = {"warnings": warnings, "memory": memory_stats(), "blocks": len(blocks),
              "dpr": app.root.qt.primaryScreen().devicePixelRatio(), "displays": display_report,
              "settingsLoadSeconds": settings_load}
    (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    app.root.destroy()
    assert not warnings, warnings
    print("PASS: Qt settings isolation, capture selection, source retry, copy/swap, readable long text, actual OCR, rendered light/dark/narrow/comparison")


if __name__ == "__main__":
    run()

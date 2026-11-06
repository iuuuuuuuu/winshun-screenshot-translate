"""Disposable Qt/QML UI using WinShun's shared controls and Phosphor outlines."""
import ctypes
from ctypes import wintypes as w
import math
import os
from pathlib import Path
import re
import threading

from PySide6.QtCore import QObject, Property, QTimer, QUrl, Qt, Signal, Slot, QRectF, QPointF, QRect
from PySide6.QtGui import QColor, QFont, QFontDatabase, QFontMetricsF, QGuiApplication, QImage, QPainter, QPen, QRawFont, QTransform
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide6.QtQuick import QQuickImageProvider, QQuickPaintedItem, QQuickWindow

import desktop
from services import ENDPOINTS, LANGUAGES, NAMES, PROVIDERS, detect, target_for

ROOT = Path(__file__).resolve().parent
ZH_LANGUAGES = {"auto": "自动", "zh": "中文", "en": "英文", "ja": "日文", "ko": "韩文",
                "fr": "法文", "de": "德文", "es": "西班牙文", "ru": "俄文"}
# Same regular-font mapping used by WinShun src/app/Phosphor.cpp.
ICONS = {"\ue70d": "\ue136", "\ue8bb": "\ue4f6", "\ue711": "\ue4f6",
         "\ue713": "\ue270", "\ue8ab": "\ue83c", "\ue8c8": "\ue1ca",
         "\ue890": "\ue220", "\ue72c": "\ue036", "\ue8c1": "\ue4a2",
         "\ue73e": "\ue182",
         "compare": "\ue1b2", "capture": "\ue10e", "text": "\ue23a"}
_registered = False
_font_family = ""
_outline_font = None
_outline_paths = {}


class OutlineGlyph(QQuickPaintedItem):
    """Draw font outlines in an em square, without text baseline distortion."""
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._glyph, self._em, self._ink = "", 16., QColor("#666666")
        self.setAntialiasing(True)

    def set_glyph(self, value):
        self._glyph = value
        self.update()
        self.changed.emit()

    def set_em(self, value):
        self._em = value
        self.update()
        self.changed.emit()

    def set_ink(self, value):
        self._ink = QColor(value)
        self.update()
        self.changed.emit()

    glyph = Property(str, lambda s: s._glyph, set_glyph, notify=changed)
    em = Property(float, lambda s: s._em, set_em, notify=changed)
    ink = Property(QColor, lambda s: s._ink, set_ink, notify=changed)

    def paint(self, painter):
        if self._glyph == "compare":
            painter.setRenderHint(QPainter.Antialiasing)
            painter.setPen(QPen(self._ink, 1.1))
            bounds = QRectF((self.width() - 13) / 2, (self.height() - 11) / 2, 13, 11)
            painter.drawRoundedRect(bounds, 1.5, 1.5)
            painter.drawLine(QPointF(bounds.left(), bounds.center().y()), QPointF(bounds.right(), bounds.center().y()))
            return
        raw = _outline_font
        text = ICONS.get(self._glyph, self._glyph)
        if text not in _outline_paths:
            _outline_paths[text] = [raw.pathForGlyph(i) for i in raw.glyphIndexesForString(text)]
        if not _outline_paths[text]:
            return
        em = self._em * 1.125  # WinShun phosphor::kScale
        scale = em / raw.pixelSize()
        place = QTransform()
        place.translate((self.width() - em) / 2,
                        (self.height() - em) / 2 + raw.ascent() * scale)
        place.scale(scale, scale)
        painter.setRenderHint(QPainter.Antialiasing)
        for path in _outline_paths[text]:
            painter.fillPath(place.map(path), self._ink)


class Theme:
    def __init__(self, dark=False):
        self.dark = dark
        self.page = "#202020" if dark else "#f3f3f3"
        self.surface = "#2b2b2b" if dark else "#fbfbfb"
        self.text = "#f3f3f3" if dark else "#1b1b1b"
        self.muted = "#b8b8b8" if dark else "#666666"
        self.border = "#444444" if dark else "#d5d5d5"
        self.accent = "#60cdff" if dark else "#0067c0"


class Root:
    """Small event-loop adapter; the protocol core stays independent of Qt."""
    def __init__(self):
        global _registered, _font_family, _outline_font
        self.qt = QGuiApplication.instance() or QGuiApplication([])
        self.qt.setQuitOnLastWindowClosed(False)
        QQuickWindow.setTextRenderType(QQuickWindow.NativeTextRendering)
        if not _registered:
            identifier = QFontDatabase.addApplicationFont(str(ROOT / "assets/Phosphor.ttf"))
            families = QFontDatabase.applicationFontFamilies(identifier)
            if not families:
                raise ValueError("The WinShun icon font could not be loaded")
            _font_family = families[0]
            font = QFont(_font_family)
            font.setPixelSize(1024)
            font.setHintingPreference(QFont.PreferNoHinting)
            font.setStyleStrategy(QFont.NoFontMerging)
            _outline_font = QRawFont.fromFont(font)
            qmlRegisterType(OutlineGlyph, "WinShunBridge", 1, 0, "OutlineGlyph")
            _registered = True
        self.timers = []

    def after(self, milliseconds, callback):
        timer = QTimer()
        timer.setSingleShot(True)
        self.timers.append(timer)
        def run():
            self.timers.remove(timer)
            callback()
            timer.deleteLater()
        timer.timeout.connect(run)
        timer.start(milliseconds)

    def update(self):
        self.qt.processEvents()

    update_idletasks = update

    def mainloop(self):
        self.qt.exec()

    def quit(self):
        # QGuiApplication.quit asks each QML window to close; our close handlers
        # route through App and may reject that event while deletion is pending.
        # App.shutdown has already hidden/cancelled its windows and jobs.
        self.qt.exit(0)

    def destroy(self):
        for timer in self.timers:
            timer.stop()
            timer.deleteLater()
        self.timers.clear()
        self.update()

    def clipboard_get(self):
        return self.qt.clipboard().text()

    def winfo_viewable(self):
        return False


def create_root():
    return Root()


def style(root, theme):
    # QML Theme binds to each window's backend; no global widget stylesheet.
    pass


class Variable:
    def __init__(self, value):
        self.value = value

    def get(self):
        return self.value

    def set(self, value):
        self.value = value


class ImageProvider(QQuickImageProvider):
    def __init__(self):
        super().__init__(QQuickImageProvider.Image)
        self.images = {}

    def requestImage(self, identifier, size, requestedSize):
        image = self.images.get(identifier.split("?")[0], QImage())
        if size is not None:
            size.setWidth(image.width())
            size.setHeight(image.height())
        return image


class Backend(QObject):
    changed = Signal()
    state = Property("QVariantMap", lambda s: s.values, notify=changed)

    def __init__(self, owner):
        super().__init__()
        self.owner = owner
        self.values = {"dark": owner.app.theme.dark, "chinese": owner.app.chinese}

    def set(self, **values):
        if all(self.values.get(key) == value for key, value in values.items()):
            return
        self.values.update(values)
        self.changed.emit()

    @Slot(str, "QVariant")
    def action(self, name, value=None):
        if not self.owner.closed:
            self.owner.action(name, value.toVariant() if hasattr(value, "toVariant") else value)


class View:
    def __init__(self, owner, name, backend, provider=None):
        self.owner, self.backend = owner, backend
        self.engine = QQmlApplicationEngine()
        self.engine.addImportPath(str(ROOT / "qml"))
        self.engine.rootContext().setContextProperty("backend", backend)
        if provider is not None:
            self.engine.addImageProvider("shot", provider)
        self.engine.load(QUrl.fromLocalFile(str(ROOT / "qml" / name)))
        objects = self.engine.rootObjects()
        if not objects:
            raise ValueError("The translation interface could not be loaded")
        self.window = objects[0]

    def show(self, activate=False, rounded=True):
        self.window.show()
        # Windows rounds the real window edge, not just the QML card.
        handle = int(self.window.winId())
        try:
            set_attribute = desktop.api("dwmapi", "DwmSetWindowAttribute", ctypes.c_long,
                                        w.HWND, w.DWORD, ctypes.c_void_p, w.DWORD)
            for attribute, value in ((20, int(self.owner.app.theme.dark)), (33, 2 if rounded else 1)):
                number = ctypes.c_int(value)
                set_attribute(handle, attribute, ctypes.byref(number), ctypes.sizeof(number))
        except (AttributeError, OSError):
            pass
        if activate:
            self.window.requestActivate()

    def close(self):
        self.window.hide()
        # Destroy engine and its image provider only after QML callbacks return.
        self.owner.app.root.after(0, self.engine.deleteLater)


def position(window, left, top):
    desktop.api("user32", "SetWindowPos", w.BOOL, w.HWND, w.HWND,
                ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.UINT)(
                    int(window.winId()), None, int(left), int(top), 0, 0, 0x15)


def display_for(rect):
    bounds = desktop.monitor_rect(rect)
    displays = desktop.monitors()
    display = next((d for d in displays if d["rect"] == bounds), displays[0])
    screen = screen_for(display)
    return display, screen


def screen_for(display):
    screens = QGuiApplication.screens()
    # Qt 6.12 exposes EDID friendly names ("DELL..."), rather than DISPLAY2.
    # Windows' per-screen scaling keeps screen origins in desktop coordinates.
    return next((s for s in screens if s.name() == display["name"]),
                next((s for s in screens if (s.geometry().x(), s.geometry().y()) == display["rect"][:2]),
                     QGuiApplication.primaryScreen()))


def logical_position(display, screen, x, y):
    geometry = screen.geometry()
    px, py, pw, ph = display["rect"]
    return (round(geometry.x() + (x - px) * geometry.width() / pw),
            round(geometry.y() + (y - py) * geometry.height() / ph))


class OutsideClick:
    """Queue asynchronous raw mouse input; never run a system input hook."""
    def __init__(self, pin):
        from native_ui import OutsideClick as Input
        self.pin, self.pending = pin, False
        self.menu_bounds = {}
        self.clock = desktop.api("kernel32", "GetTickCount", w.DWORD)
        self.window_at = desktop.api("user32", "WindowFromPhysicalPoint", w.HWND, desktop.Point)
        self.process_of = desktop.api("user32", "GetWindowThreadProcessId", w.DWORD, w.HWND, ctypes.POINTER(w.DWORD))
        self.input = Input(self.observe)
        self.input.start()
        def registered():
            if self.pin.closed or self.input.cancelled.is_set():
                return
            if not self.input.ready.is_set():
                self.pin.app.root.after(30, registered)
                return
            from main import log
            if self.input.error:
                log("outside-click: input registration failed")
                self.pin.backend.set(providerNote=self.pin.app.t("点击外部关闭暂不可用", "Outside-click dismissal is unavailable"))
            else:
                log("outside-click: ready method=raw-input")
        self.pin.app.root.after(30, registered)

    def remember_menu(self, name, window, visible):
        tick = self.clock()
        if visible:
            rect = desktop.Rect()
            if desktop.api("user32", "GetWindowRect", w.BOOL, w.HWND, ctypes.POINTER(desktop.Rect))(
                    int(window.winId()), ctypes.byref(rect)):
                self.menu_bounds[name] = ((rect.left, rect.top, rect.right, rect.bottom), tick, None)
        elif name in self.menu_bounds:
            rect, start, _ = self.menu_bounds[name]
            self.menu_bounds[name] = (rect, start, tick)

    def observe(self, point, timestamp=None):
        if self.pending or self.pin.closed or not self.pin.app.settings.get("closeOnOutsideClick", True):
            return
        tick = self.clock() if timestamp is None else timestamp
        # Raw input is asynchronous: a language item may hide its menu before
        # we hit-test. Use the menu's bounds at the original message timestamp.
        for rect, start, end in tuple(self.menu_bounds.values()):
            left, top, right, bottom = rect
            elapsed = (tick - start) & 0xffffffff
            present = elapsed < 0x80000000 if end is None else elapsed <= ((end - start) & 0xffffffff)
            if present and left <= point[0] < right and top <= point[1] < bottom:
                return
        handle = self.window_at(desktop.Point(*point))
        process = w.DWORD()
        self.process_of(handle, ctypes.byref(process))
        # Includes the toolbar, top-level language windows, and settings.
        if process.value != os.getpid():
            self.pending = True
            self.pin.app.events.put(lambda: self.pin.close() if not self.pin.closed else None)

    def close(self):
        self.input.close()


def qimage(image):
    return QImage(image.rgb, image.width, image.height, image.width * 3, QImage.Format_RGB888).copy()


def wrap_image(text, width, theme, pixel_size=16):
    """Readable full text fallback only when a single source block cannot fit."""
    width = max(240, width)
    font = QFont("Microsoft YaHei UI")
    font.setPixelSize(pixel_size)
    metrics = QFontMetricsF(font)
    flags = int(Qt.TextWordWrap | Qt.AlignLeft | Qt.AlignTop)
    bounds = metrics.boundingRect(QRectF(0, 0, width - 32, 100000), flags, text)
    image = QImage(width, int(math.ceil(bounds.height())) + 32, QImage.Format_RGB32)
    image.fill(QColor(theme.surface))
    painter = QPainter(image)
    painter.setFont(font)
    painter.setPen(QColor(theme.text))
    painter.drawText(QRectF(16, 16, width - 32, image.height() - 32), flags, text)
    painter.end()
    return image


class Pin:
    def __init__(self, app, image):
        self.app, self.image, self.blocks, self.data = app, image, [], {}
        self.closed, self.loading, self.status, self.opacity = False, True, app.t("识别中…", "Recognizing…"), 1.
        self.compare, self.source, self.target = Variable(False), Variable("auto"), Variable("zh")
        self.origin = image.rect[:2]
        self.display, self.screen = display_for(image.rect)
        self.area = self.display["work"]
        self.visible, self.version, self.details = False, 0, None
        self.outside_click = None
        self.backend = Backend(self)
        self.provider = ImageProvider()
        self.provider.images["source"] = qimage(image)
        self.provider.images["translated"] = QImage(self.provider.images["source"])
        self.rendered_key = None
        self.backend.set(source="auto", target="zh", comparison=False, loading=True, ready=False,
                         sourceText="", translatedText="", status=self.status, fullText=False,
                         providerNote=self.provider_label(),
                         translationFirst=app.settings.get("comparisonOrder", "translation-first") != "source-first",
                         version=0, viewWidth=320, viewHeight=120,
                         sourceWidth=1, sourceHeight=1, resultWidth=1, resultHeight=1,
                         contentWidth=1, contentHeight=1,
                         sourceLabels=list(ZH_LANGUAGES.values()) if app.chinese else list(LANGUAGES.values()),
                         codes=list(LANGUAGES), provider=self.provider_label())
        self.view = View(self, "Pin.qml", self.backend, self.provider)
        self.window = self.view.window
        self.bar = View(self, "Toolbar.qml", self.backend)
        self.toolbar_window = self.bar.window
        # Resolve the capture's screen before calculating any image size.
        # A hidden QWindow otherwise starts with the primary screen's DPR.
        for window in (self.window, self.toolbar_window):
            window.setScreen(self.screen)
            window.setPosition(*logical_position(self.display, self.screen, *self.origin))
        self.redraw()

    def provider_label(self):
        index = PROVIDERS.index(self.app.active_provider())
        return ("MyMemory", "Google", "Edge", "translate.js", "微软", "DeepL", "OpenAI", "DeepSeek", "豆包", "混元")[index] if self.app.chinese else NAMES[index]

    def render(self):
        image = QImage(self.provider.images["source"])
        if not self.data.get("segments") or not self.blocks:
            return image
        painter = QPainter(image)
        painter.setRenderHint(QPainter.TextAntialiasing)
        for block, text in zip(self.blocks, self.data["segments"]):
            if desktop.re_literal(block["text"]):
                continue
            x, y, width, height = block["box"]
            right, bottom = image.width(), image.height()
            for other in self.blocks:
                if other is block:
                    continue
                a, b, c, d = other["box"]
                if b < y + height and b + d > y and a >= x + width:
                    right = min(right, a - 5)
                if a < x + width and a + c > x and b >= y + height:
                    bottom = min(bottom, b - 3)
            cell_width = max(1, min(right - x, width + max(12, height)))
            cell_height = max(1, min(bottom - y, max(26, height * 2 + 6)))
            backdrop, foreground = self.image.backdrop(block["box"])
            # Only cover the OCR text region; adjacent icons and badges remain.
            painter.fillRect(QRectF(x - 1, y - 1, width + 2, height + 3), QColor(backdrop))
            size = min(28, max(14, int(block.get("glyphHeight", height) * .95)))
            flags = int(Qt.TextWordWrap | Qt.AlignLeft | Qt.AlignTop)
            font = QFont("Microsoft YaHei UI")
            bounds = QRectF(x, y - 1, cell_width, cell_height)
            while size >= 12:
                font.setPixelSize(size)
                metrics = QFontMetricsF(font)
                measured = metrics.boundingRect(QRectF(0, 0, cell_width, 100000), flags, text)
                if measured.height() <= cell_height and measured.width() <= cell_width + 1:
                    break
                size -= 1
            if size < 12 and len(self.blocks) == 1:
                painter.end()
                return wrap_image(text, min(640, max(280, image.width())), self.app.theme)
            painter.setFont(font)
            painter.setPen(QColor(foreground))
            # Cover expanded text only inside the block's safe local area.
            painter.fillRect(QRectF(x, y - 1, min(cell_width, measured.width()),
                                   min(cell_height, measured.height())), QColor(backdrop))
            painter.save()
            painter.setClipRect(bounds)
            painter.drawText(bounds, flags, text)
            painter.restore()
        painter.end()
        return image

    def redraw(self):
        if self.closed:
            return
        key = (id(self.blocks), id(self.data), self.app.theme.dark)
        if key != self.rendered_key:
            self.version += 1
            self.rendered_key = key
            self.provider.images["translated"] = self.render()
        rendered = self.provider.images["translated"]
        dpr = self.window.devicePixelRatio()
        ax, ay, aw, ah = self.area
        max_width, max_height = aw / dpr - 16, ah / dpr - 90
        natural_width = max(self.image.width, rendered.width())
        self.factor = min(1., max_width * dpr / natural_width)
        source_width = self.image.width * self.factor / dpr
        result_width = rendered.width() * self.factor / dpr
        source_height = self.image.height * self.factor / dpr
        result_height = rendered.height() * self.factor / dpr
        if self.compare.get():
            content_width = max(source_width, result_width)
            content_height = source_height + result_height + 60
        else:
            content_width, content_height = result_width, result_height
        self.content_height = content_height
        self.view_width, self.view_height = math.ceil(content_width + 2), math.ceil(min(max_height, content_height + 2))
        self.overflow = content_height + 2 > self.view_height
        self.backend.set(dark=self.app.theme.dark, comparison=self.compare.get(),
                         translationFirst=self.app.settings.get("comparisonOrder", "translation-first") != "source-first",
                         sourceWidth=source_width, sourceHeight=source_height,
                         resultWidth=result_width, resultHeight=result_height,
                         contentWidth=content_width, contentHeight=content_height,
                         viewWidth=self.view_width, viewHeight=self.view_height,
                         version=self.version)
        self.window.setWidth(self.view_width)
        self.window.setHeight(self.view_height)
        self.place()

    def place(self):
        dpr = self.window.devicePixelRatio()
        ax, ay, aw, ah = self.area
        bar_width = self.toolbar_window.width() * dpr
        bar_height = self.toolbar_window.height() * dpr
        height = self.view_height * dpr
        left = max(ax, min(self.origin[0], ax + aw - self.view_width * dpr))
        top = max(ay, min(self.origin[1], ay + ah - height - bar_height - 8))
        self.origin = left, top
        self.toolbar_origin = (max(ax, min(left + (self.view_width * dpr - bar_width) / 2, ax + aw - bar_width)),
                               top + height + 8)
        self.window.setPosition(*logical_position(self.display, self.screen, *self.origin))
        self.toolbar_window.setPosition(*logical_position(self.display, self.screen, *self.toolbar_origin))
        wx, wy = logical_position(self.display, self.screen, ax, ay)
        self.backend.set(menuLeft=wx, menuTop=wy, menuRight=wx + aw / dpr, menuBottom=wy + ah / dpr)

    def show(self):
        self.visible = True
        self.place()
        self.view.show()
        self.bar.show()
        self.apply_preferences()

    def apply_preferences(self):
        enabled = self.app.settings.get("closeOnOutsideClick", True)
        if enabled and self.visible and self.outside_click is None:
            try:
                self.outside_click = OutsideClick(self)
            except OSError:
                # The capture remains usable if Windows disallows the hook.
                self.backend.set(providerNote=self.app.t("点击外部关闭暂不可用", "Outside-click dismissal is unavailable"))
        elif not enabled and self.outside_click is not None:
            self.outside_click.close()
            self.outside_click = None

    def set_source(self, blocks, source, target):
        self.blocks = blocks
        self.source.set(source)
        self.target.set(target)
        self.data = {"source": "\n".join(b["text"] for b in blocks), "sourceLanguage": source, "targetLanguage": target}
        self.backend.set(source=source, target=target, sourceText=self.data["source"], ready=True)

    def set_result(self, blocks, data):
        self.blocks, self.data = blocks, data
        self.source.set(data["sourceLanguage"])
        self.target.set(data["targetLanguage"])
        self.loading, self.status = False, ""
        actual = data.get("provider", self.app.active_provider())
        label = NAMES[PROVIDERS.index(actual)]
        note = (self.app.t("临时使用 ", "Temporarily using ") + label +
                self.app.t("；间隔到后自动重试主渠道", "; the preferred service will be retried after recovery")) if data.get("fallback") else label
        self.backend.set(source=self.source.get(), target=self.target.get(), sourceText=data["source"],
                         translatedText=data["translated"], loading=False, ready=True, status="",
                         provider=label, providerNote=note)
        self.redraw()

    def update_status(self, text, loading=True):
        self.status, self.loading = text, loading
        self.backend.set(status=text, loading=loading, ready=bool(self.blocks))

    def action(self, name, value=None):
        if name == "close":
            self.close()
        elif name == "compare":
            self.compare.set(not self.compare.get())
            self.redraw()
        elif name in ("source", "target") and not self.loading and self.blocks:
            getattr(self, name).set(value)
            self.backend.set(**{name: value})
            self.app.retranslate(self)
        elif name == "swap":
            self.swap()
        elif name == "copy":
            self.copy(bool(value))
        elif name == "settings":
            self.app.show_settings()
        elif name == "menu" and self.outside_click and hasattr(self, "toolbar_window"):
            window = self.toolbar_window.findChild(QObject, value[0])
            if window is not None:
                self.outside_click.remember_menu(value[0], window, bool(value[1]))
        elif name == "retry":
            self.retry()
        elif name == "text":
            self.show_full_text()
        elif name == "move":
            self.origin = (self.origin[0] + value[0] * self.window.devicePixelRatio(),
                           self.origin[1] + value[1] * self.window.devicePixelRatio())
            self.place()
        elif name == "opacity":
            self.opacity = max(.25, min(1., self.opacity + value * .05))
            self.window.setOpacity(self.opacity)
            self.toolbar_window.setOpacity(self.opacity)

    def show_full_text(self):
        self.backend.set(fullText=not self.backend.values["fullText"])

    def swap(self):
        if not self.loading and self.data.get("segments"):
            source = self.data["sourceLanguage"]
            if source == "auto":
                source = detect(self.data["source"])
            self.app.retranslate(self, self.data["targetLanguage"], source, self.data["segments"])

    def copy(self, source=False):
        if source and self.blocks:
            self.app.copy("\n".join(block["text"] for block in self.blocks))
        elif self.data.get("translated"):
            self.app.copy(self.data["translated"])

    def retry(self):
        self.close()
        self.app.retry_shot()

    def close(self):
        if self.closed:
            return
        self.closed = True
        # Hide first; cancellation and QML engine disposal happen afterwards.
        self.toolbar_window.hide()
        self.window.hide()
        if self.outside_click is not None:
            self.outside_click.close()
            self.outside_click = None
        self.app.cancel_pin(self)
        self.bar.close()
        self.view.close()
        self.provider.images.clear()
        self.image, self.blocks, self.data = None, [], {}
        if self.app.pin is self:
            self.app.pin = None
        self.app.release_idle()


class Settings:
    def __init__(self, app):
        self.app, self.closed, self.testing = app, False, False
        self.drafts, self.selected = {}, app.active_provider()
        self.test_results = {}
        self.test_cancel = threading.Event()
        self.backend = Backend(self)
        self.backend.set(names=["MyMemory", "Google", "Edge", "translate.js", "微软", "DeepL", "OpenAI", "DeepSeek", "豆包", "混元"] if app.chinese else list(NAMES),
                         providers=list(PROVIDERS), current=PROVIDERS.index(self.selected), message="", testing=False,
                         testResults=[], batch=False, showTestResults=False)
        self.select(self.selected)
        self.view = View(self, "Settings.qml", self.backend)
        self.window = self.view.window

    def select(self, provider):
        self.backend.set(showTestResults=False)
        self.selected = provider
        if provider not in self.drafts:
            self.drafts[provider] = self.app.profiles.get(provider)
        draft = self.drafts[provider]
        self.backend.set(current=PROVIDERS.index(provider), provider=provider,
                         apiKey=draft.get("apiKey", ""), endpoint=draft.get("endpoint", ""),
                         model=draft.get("model", ""), region=draft.get("region", ""),
                         endpointHint=ENDPOINTS[provider],
                         modelHint={"deepseek": "deepseek-flash", "hunyuan": "hunyuan-turbos-latest",
                                    "doubao": "模型 / 接入点 ID"}.get(provider, "gpt-4o-mini"),
                         today=f"{self.app.usage.today():,} / 5,000",
                         message=self.test_results.get(provider, {}).get("detail", ""))

    def action(self, name, value=None):
        if name == "select":
            self.select(PROVIDERS[int(value)])
        elif name in ("apiKey", "endpoint", "model", "region"):
            self.drafts[self.selected][name] = value
        elif name == "save":
            self.save()
        elif name == "test":
            self.test_service()
        elif name == "testAll":
            self.test_service(all_services=True)
        elif name == "showTests":
            self.backend.set(showTestResults=True)
        elif name == "close":
            self.close()

    def test_service(self, all_services=False):
        if self.testing:
            return
        providers = list(PROVIDERS) if all_services else [self.selected]
        # Snapshot drafts before starting. Each request diagnoses its own route,
        # without fallback or changes to the user's preferred service.
        profiles = {}
        for provider in providers:
            if provider not in self.drafts:
                self.drafts[provider] = self.app.profiles.get(provider)
            profiles[provider] = {**self.drafts[provider], "provider": provider,
                                  "proxy": self.app.settings.get("proxy", ""), "fallbackEnabled": False}
        self.testing = True
        self.backend.set(showTestResults=all_services)
        self.test_cancel.clear()
        self.test_results = {p: {"status": "waiting", "detail": self.app.t("等待测试", "Waiting")} for p in providers}
        self.publish_tests(all_services)
        def work():
            import queue
            def test(provider):
                if self.test_cancel.is_set():
                    return None
                profile = profiles[provider]
                if provider in PROVIDERS[4:] and not profile.get("apiKey", "").strip():
                    return {"status": "skipped", "detail": self.app.t("未配置 API 密钥", "API key not configured")}
                if provider == "doubao" and not profile.get("model", "").strip():
                    return {"status": "skipped", "detail": self.app.t("未配置模型 / 接入点", "Model / endpoint not configured")}
                def started():
                    if not self.closed:
                        self.test_results[provider] = {"status": "running", "detail": self.app.t("测试中…", "Testing…")}
                        self.publish_tests(all_services)
                self.app.events.put(started)
                try:
                    data = self.app.translator.translate(profile, ["Hello world"], "en", "zh")
                    return {"status": "success", "detail": self.app.t("连接成功：", "Connected: ") + data["translated"]}
                except Exception as ex:
                    return {"status": "failed", "detail": str(ex) if isinstance(ex, (ValueError, TimeoutError)) else self.app.t("连接失败，请检查网络", "Connection failed; check network")}
            pending = queue.Queue()
            for provider in providers:
                pending.put(provider)
            def consume():
                while not self.test_cancel.is_set():
                    try:
                        provider = pending.get_nowait()
                    except queue.Empty:
                        return
                    result = test(provider)
                    def completed(provider=provider, result=result):
                        if not self.closed and result is not None:
                            self.test_results[provider] = result
                            self.publish_tests(all_services)
                    self.app.events.put(completed)
            # Daemon requests never keep the disposable UI process alive after
            # its final window closes. At most three routes run concurrently.
            threads = [threading.Thread(target=consume, daemon=True, name="service-test-route") for _ in range(min(3, len(providers)))]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
            def done():
                self.testing = False
                if not self.closed:
                    self.publish_tests(all_services)
            self.app.events.put(done)
        threading.Thread(target=work, daemon=True, name="service-test").start()

    def publish_tests(self, batch):
        rows = [{"provider": p, "name": self.backend.values["names"][PROVIDERS.index(p)], **r}
                for p, r in self.test_results.items()]
        completed = sum(r["status"] not in ("waiting", "running") for r in rows)
        message = self.app.t("已测试 ", "Tested ") + f"{completed}/{len(rows)}" if batch else self.test_results.get(self.selected, {}).get("detail", "")
        self.backend.set(testing=self.testing, batch=batch, testResults=rows, message=message)

    def show(self):
        self.view.show(activate=True)

    def apply_theme(self):
        self.backend.set(dark=self.app.theme.dark)

    def save(self):
        try:
            self.app.profiles.save(self.drafts)
            self.close()
        except (OSError, ValueError):
            self.backend.set(message=self.app.t("保存失败，请检查地址和数据目录", "Save failed; check the endpoint and data directory"))

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.test_cancel.set()
        self.view.close()
        self.drafts.clear()
        if self.app.config is self:
            self.app.config = None
        self.app.release_idle()


class RegionSurface:
    def __init__(self, region, display, screen):
        self.region, self.app = region, region.app
        self.display, self.screen = display, screen
        self.backend = Backend(self)
        self.backend.set(selecting=False, selectionX=0, selectionY=0, selectionWidth=0, selectionHeight=0)
        self.view = View(self, "Region.qml", self.backend)
        self.window = self.view.window
        self.window.setScreen(screen)
        geometry = screen.geometry()
        dpr = screen.devicePixelRatio()
        self.window.setGeometry(QRect(geometry.x(), geometry.y(),
                                      math.ceil(display["rect"][2] / dpr) + 1,
                                      math.ceil(display["rect"][3] / dpr) + 1))

    def show(self):
        # Qt's fractional-DPI backing store can round the last raster column
        # inward. Give it one logical pixel of spare, then clip the native shape
        # to the exact monitor. No seams, overlap shading or uncovered edges.
        width, height = self.display["rect"][2:]
        region = desktop.api("gdi32", "CreateRectRgn", w.HANDLE, ctypes.c_int,
                             ctypes.c_int, ctypes.c_int, ctypes.c_int)(0, 0, width, height)
        if not region:
            raise OSError("Windows could not create the selection mask")
        if not desktop.api("user32", "SetWindowRgn", ctypes.c_int, w.HWND, w.HANDLE, w.BOOL)(
                int(self.window.winId()), region, True):
            desktop.api("gdi32", "DeleteObject", w.BOOL, w.HANDLE)(region)
            raise OSError("Windows could not size the selection mask")
        self.view.show(rounded=False)

    @property
    def closed(self):
        return self.region.closed

    def action(self, name, value=None):
        self.region.action(name, value)

    def selection(self, rect):
        x, y, width, height = rect
        ax, ay, aw, ah = self.display["rect"]
        sx = sy = 1 / self.window.devicePixelRatio()
        self.backend.set(selecting=True, selectionX=(x - ax) * sx, selectionY=(y - ay) * sy,
                         selectionWidth=width * sx, selectionHeight=height * sy)


class Region:
    def __init__(self, app, callback):
        self.app, self.callback, self.closed = app, callback, False
        self.rect = desktop.virtual_screen()
        self.surfaces = [RegionSurface(self, display, screen_for(display))
                         for display in desktop.monitors()]
        if not self.surfaces:
            raise ValueError("No display is available for screenshot selection")
        self.view, self.window = self.surfaces[0].view, self.surfaces[0].window
        self.start = None

    def show(self):
        for surface in self.surfaces:
            surface.show()
        self.window.requestActivate()

    def action(self, name, value=None):
        if name in ("begin", "motion", "end"):
            point = desktop.physical_cursor()
            if name == "begin":
                self.start = point
            if self.start is None:
                return
            x, y = self.start
            rect = min(x, point[0]), min(y, point[1]), abs(x - point[0]), abs(y - point[1])
            for surface in self.surfaces:
                surface.selection(rect)
            if name == "end":
                self.close(rect if rect[2] >= 3 and rect[3] >= 3 else None)
        else:
            self.close(None)

    def close(self, rect):
        if self.closed:
            return
        self.closed = True
        for surface in self.surfaces:
            surface.view.close()
        self.app.region = None
        self.app.root.after(100, lambda: self.callback(rect))

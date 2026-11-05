"""Windows capture and short-lived system OCR; no desktop access on import."""
import base64
import ctypes
from ctypes import wintypes as w
from dataclasses import dataclass
import json
import os
from pathlib import Path
import struct
import subprocess
import threading
import zlib

from spatial import group, union


class Rect(ctypes.Structure):
    _fields_ = [("left", w.LONG), ("top", w.LONG), ("right", w.LONG), ("bottom", w.LONG)]


class Point(ctypes.Structure):
    _fields_ = [("x", w.LONG), ("y", w.LONG)]


def api(library, name, result, *args):
    fn = getattr(ctypes.WinDLL(library, use_last_error=True), name)
    fn.restype, fn.argtypes = result, list(args)
    return fn


def dpi_awareness():
    try:
        if api("user32", "SetProcessDpiAwarenessContext", w.BOOL, w.HANDLE)(ctypes.c_void_p(-4)):
            return
    except AttributeError:
        pass
    try:
        api("shcore", "SetProcessDpiAwareness", ctypes.c_long, ctypes.c_int)(2)
    except (AttributeError, OSError):
        api("user32", "SetProcessDPIAware", w.BOOL)()


def virtual_screen():
    metric = api("user32", "GetSystemMetrics", ctypes.c_int, ctypes.c_int)
    return tuple(metric(i) for i in (76, 77, 78, 79))


def foreground():
    return api("user32", "GetForegroundWindow", w.HWND)()


def window_rect(handle):
    rect = Rect()
    if not api("user32", "GetWindowRect", w.BOOL, w.HWND, ctypes.POINTER(Rect))(handle, ctypes.byref(rect)):
        raise OSError("The active window could not be captured")
    vx, vy, vw, vh = virtual_screen()
    x, y = max(vx, rect.left), max(vy, rect.top)
    return x, y, min(vx + vw, rect.right) - x, min(vy + vh, rect.bottom) - y


def monitor_rect():
    point = Point()
    api("user32", "GetCursorPos", w.BOOL, ctypes.POINTER(Point))(ctypes.byref(point))
    handle = api("user32", "MonitorFromPoint", w.HANDLE, Point, w.DWORD)(point, 2)

    class MonitorInfo(ctypes.Structure):
        _fields_ = [("size", w.DWORD), ("monitor", Rect), ("work", Rect), ("flags", w.DWORD)]

    info = MonitorInfo()
    info.size = ctypes.sizeof(info)
    api("user32", "GetMonitorInfoW", w.BOOL, w.HANDLE, ctypes.POINTER(MonitorInfo))(handle, ctypes.byref(info))
    r = info.monitor
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def position(window, x, y):
    handle = window.winfo_id()
    # Tk widgets are children of a native top-level wrapper.
    parent = api("user32", "GetParent", w.HWND, w.HWND)(handle) or handle
    api("user32", "SetWindowPos", w.BOOL, w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
        ctypes.c_int, ctypes.c_int, w.UINT)(parent, None, x, y, 0, 0, 0x15)


def png_rgb(width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    compressor = zlib.compressobj(3)
    compressed = bytearray()
    stride = width * 3
    for y in range(height):
        compressed.extend(compressor.compress(b"\0" + rgb[y * stride:(y + 1) * stride]))
    compressed.extend(compressor.flush())
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", bytes(compressed)) + chunk(b"IEND", b""))


@dataclass
class Image:
    rect: tuple
    width: int
    height: int
    rgb: bytes
    png: bytes

    def backdrop(self, box):
        x, y, width, height = [int(v) for v in box]
        points = [(x + width * i // 4, max(0, y - 2)) for i in range(5)]
        points += [(x + width * i // 4, min(self.height - 1, y + height + 2)) for i in range(5)]
        colors = []
        for px, py in points:
            offset = (max(0, min(self.height - 1, py)) * self.width + max(0, min(self.width - 1, px))) * 3
            colors.append(tuple(self.rgb[offset:offset + 3]))
        color = tuple(sorted(c[i] for c in colors)[len(colors) // 2] for i in range(3))
        background = "#%02x%02x%02x" % color
        text = "#171717" if color[0] * .299 + color[1] * .587 + color[2] * .114 > 140 else "#f5f5f5"
        return background, text


def capture(rect):
    x, y, width, height = [int(v) for v in rect]
    if width < 2 or height < 2:
        raise ValueError("The capture region is empty")
    if width * height > 16_000_000:
        raise ValueError("The capture exceeds 16 megapixels. Select a smaller region.")
    get_dc = api("user32", "GetDC", w.HDC, w.HWND)
    release_dc = api("user32", "ReleaseDC", ctypes.c_int, w.HWND, w.HDC)
    make_dc = api("gdi32", "CreateCompatibleDC", w.HDC, w.HDC)
    make_bitmap = api("gdi32", "CreateCompatibleBitmap", w.HBITMAP, w.HDC, ctypes.c_int, ctypes.c_int)
    select = api("gdi32", "SelectObject", w.HANDLE, w.HDC, w.HANDLE)
    delete = api("gdi32", "DeleteObject", w.BOOL, w.HANDLE)
    delete_dc = api("gdi32", "DeleteDC", w.BOOL, w.HDC)
    screen = get_dc(None)
    memory, bitmap, previous = None, None, None
    try:
        memory = make_dc(screen)
        bitmap = make_bitmap(screen, width, height)
        if not screen or not memory or not bitmap:
            raise OSError("Windows could not allocate the capture")
        previous = select(memory, bitmap)
        blit = api("gdi32", "BitBlt", w.BOOL, w.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                   ctypes.c_int, w.HDC, ctypes.c_int, ctypes.c_int, w.DWORD)
        if not blit(memory, 0, 0, width, height, screen, x, y, 0x00CC0020 | 0x40000000):
            raise OSError("Windows could not capture this region")
        select(memory, previous)
        previous = None
        header = struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
        info = ctypes.create_string_buffer(header + b"\0" * 16)
        pixels = ctypes.create_string_buffer(width * height * 4)
        get_bits = api("gdi32", "GetDIBits", ctypes.c_int, w.HDC, w.HBITMAP, w.UINT, w.UINT,
                       ctypes.c_void_p, ctypes.c_void_p, w.UINT)
        if get_bits(screen, bitmap, 0, height, pixels, info, 0) != height:
            raise OSError("Windows could not read capture pixels")
        bgra = pixels.raw
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = bgra[2::4], bgra[1::4], bgra[0::4]
        rgb = bytes(rgb)
    finally:
        if previous:
            select(memory, previous)
        if bitmap:
            delete(bitmap)
        if memory:
            delete_dc(memory)
        if screen:
            release_dc(None, screen)
    return Image(tuple(rect), width, height, rgb, png_rgb(width, height, rgb))


class Worker:
    def __init__(self):
        self.lock = threading.Lock()
        self.process = None
        self.cancelled = threading.Event()

    def cancel(self):
        self.cancelled.set()
        with self.lock:
            if self.process and self.process.poll() is None:
                self.process.kill()

    def run(self, request):
        executable = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        with self.lock:
            if self.cancelled.is_set():
                raise InterruptedError("Cancelled")
            self.process = subprocess.Popen(
                [str(executable), "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                 "-File", str(Path(__file__).with_name("ocr.ps1"))],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
            process = self.process
        try:
            output, _ = process.communicate((json.dumps(request, ensure_ascii=False) + "\n").encode("utf-8"), timeout=20)
            if self.cancelled.is_set():
                raise InterruptedError("Cancelled")
            if len(output) > 8 * 1024 * 1024:
                raise ValueError("OCR response is too large")
            data = json.loads(output.decode("utf-8-sig"))
            if "error" in data:
                raise ValueError(data["error"])
            if process.returncode:
                raise ValueError("Windows OCR failed")
            return data
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise TimeoutError("Windows OCR timed out") from None
        except (json.JSONDecodeError, UnicodeError):
            raise ValueError("Windows OCR worker failed") from None
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            for stream in (process.stdin, process.stdout, process.stderr):
                stream.close()
            with self.lock:
                self.process = None

    def recognize(self, image):
        data = self.run({"mode": "ocr", "png": base64.b64encode(image.png).decode("ascii")})
        blocks = group(data["words"])
        candidates = group(data.get("enlarged", []))
        # Refine only overlapping labels. Never merge standalone numeric badges.
        for i, block in enumerate(blocks):
            if re_literal(block["text"]):
                continue
            for other in candidates:
                x, y, width, height = block["box"]
                a, b, c, d = other["box"]
                overlap = max(0, min(x + width, a + c) - max(x, a)) * max(0, min(y + height, b + d) - max(y, b))
                cleaner = (block["text"].endswith(other["text"]) and len(block["text"]) - len(other["text"]) <= 4
                           and other["box"][0] - block["box"][0] >= other["glyphHeight"] * .4)
                if overlap > width * height * .5 and overlap > c * d * .5 and (cleaner or sum(ch.isalpha() for ch in other["text"]) > sum(ch.isalpha() for ch in block["text"])):
                    blocks[i] = other
                    break
        if not blocks:
            blocks = candidates
        return blocks


def re_literal(text):
    import re
    return bool(re.fullmatch(r"\d+(?:[.,]\d+)*(?:[%+])?", text.strip()))

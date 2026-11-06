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

from spatial import group, refine


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


def disposable_process_job():
    """Make Windows release this disposable process and all OCR children."""
    class Limits(ctypes.Structure):
        _fields_ = [("processTime", ctypes.c_int64), ("jobTime", ctypes.c_int64),
                    ("flags", w.DWORD), ("minimum", ctypes.c_size_t), ("maximum", ctypes.c_size_t),
                    ("active", w.DWORD), ("affinity", ctypes.c_size_t),
                    ("priority", w.DWORD), ("scheduling", w.DWORD)]
    class Extended(ctypes.Structure):
        _fields_ = [("limits", Limits), ("io", ctypes.c_uint64 * 6),
                    ("processMemory", ctypes.c_size_t), ("jobMemory", ctypes.c_size_t),
                    ("peakProcessMemory", ctypes.c_size_t), ("peakJobMemory", ctypes.c_size_t)]
    handle = api("kernel32", "CreateJobObjectW", w.HANDLE, ctypes.c_void_p, w.LPCWSTR)(None, None)
    if not handle:
        raise OSError("Windows could not create the disposable process job")
    limits = Extended()
    limits.limits.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    process = api("kernel32", "GetCurrentProcess", w.HANDLE)()
    if not api("kernel32", "SetInformationJobObject", w.BOOL, w.HANDLE, ctypes.c_int,
               ctypes.c_void_p, w.DWORD)(handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)) or not api(
                   "kernel32", "AssignProcessToJobObject", w.BOOL, w.HANDLE, w.HANDLE)(handle, process):
        api("kernel32", "CloseHandle", w.BOOL, w.HANDLE)(handle)
        raise OSError("Windows could not isolate OCR process cleanup")
    # Never close while this process is running; Windows closes it at exit.
    return handle


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


class MonitorInfo(ctypes.Structure):
    _fields_ = [("size", w.DWORD), ("monitor", Rect), ("work", Rect), ("flags", w.DWORD)]


class MonitorInfoEx(ctypes.Structure):
    _fields_ = MonitorInfo._fields_ + [("device", w.WCHAR * 32)]


def monitors():
    """Physical monitor bounds paired with Qt's Windows display names."""
    result = []
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, ctypes.POINTER(Rect), w.LPARAM)
    def collect(handle, dc, bounds, data):
        info = MonitorInfoEx()
        info.size = ctypes.sizeof(info)
        if api("user32", "GetMonitorInfoW", w.BOOL, w.HANDLE, ctypes.POINTER(MonitorInfoEx))(handle, ctypes.byref(info)):
            def rect(r):
                return r.left, r.top, r.right - r.left, r.bottom - r.top
            result.append({"name": info.device, "rect": rect(info.monitor), "work": rect(info.work)})
        return True
    callback = callback_type(collect)
    if not api("user32", "EnumDisplayMonitors", w.BOOL, w.HDC, ctypes.POINTER(Rect),
               callback_type, w.LPARAM)(None, None, callback, 0) or not result:
        raise OSError("Windows could not enumerate displays")
    return result


def physical_cursor():
    point = Point()
    if not api("user32", "GetPhysicalCursorPos", w.BOOL, ctypes.POINTER(Point))(ctypes.byref(point)):
        raise OSError("Windows could not read the selection position")
    return point.x, point.y


def monitor_rect(rect=None, work=False):
    point = Point()
    if rect is None:
        api("user32", "GetCursorPos", w.BOOL, ctypes.POINTER(Point))(ctypes.byref(point))
        handle = api("user32", "MonitorFromPoint", w.HANDLE, Point, w.DWORD)(point, 2)
    else:
        x, y, width, height = rect
        bounds = Rect(int(x), int(y), int(x + width), int(y + height))
        handle = api("user32", "MonitorFromRect", w.HANDLE, ctypes.POINTER(Rect), w.DWORD)(ctypes.byref(bounds), 2)

    info = MonitorInfo()
    info.size = ctypes.sizeof(info)
    api("user32", "GetMonitorInfoW", w.BOOL, w.HANDLE, ctypes.POINTER(MonitorInfo))(handle, ctypes.byref(info))
    r = info.work if work else info.monitor
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def native_handle(window):
    handle = window.winfo_id()
    return api("user32", "GetParent", w.HWND, w.HWND)(handle) or handle


def decorate(window, dark=False, owner=None):
    handle = native_handle(window)
    if owner:
        name = "SetWindowLongPtrW" if ctypes.sizeof(ctypes.c_void_p) == 8 else "SetWindowLongW"
        api("user32", name, ctypes.c_ssize_t, w.HWND, ctypes.c_int, ctypes.c_ssize_t)(handle, -8, owner)
    try:
        set_attribute = api("dwmapi", "DwmSetWindowAttribute", ctypes.c_long,
                            w.HWND, w.DWORD, ctypes.c_void_p, w.DWORD)
        for attribute, value in ((20, int(dark)), (33, 2)):
            number = ctypes.c_int(value)
            set_attribute(handle, attribute, ctypes.byref(number), ctypes.sizeof(number))
    except (AttributeError, OSError):
        pass


def external_foreground():
    handle = foreground()
    process = w.DWORD()
    api("user32", "GetWindowThreadProcessId", w.DWORD, w.HWND, ctypes.POINTER(w.DWORD))(handle, ctypes.byref(process))
    current = api("kernel32", "GetCurrentProcessId", w.DWORD)()
    return handle if process.value != current else None


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

    def resized(self, width, height):
        width, height = max(1, int(width)), max(1, int(height))
        if (width, height) == (self.width, self.height):
            return self
        memory = api("gdi32", "CreateCompatibleDC", w.HDC, w.HDC)(None)
        header = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0))
        bits = ctypes.c_void_p()
        bitmap = api("gdi32", "CreateDIBSection", w.HBITMAP, w.HDC, ctypes.c_void_p, w.UINT,
                     ctypes.POINTER(ctypes.c_void_p), w.HANDLE, w.DWORD)(memory, header, 0, ctypes.byref(bits), None, 0)
        select = api("gdi32", "SelectObject", w.HANDLE, w.HDC, w.HANDLE)
        previous = select(memory, bitmap)
        try:
            if not memory or not bitmap:
                raise OSError("Windows could not resize the image")
            bgra = bytearray(self.width * self.height * 4)
            bgra[0::4], bgra[1::4], bgra[2::4] = self.rgb[2::3], self.rgb[1::3], self.rgb[0::3]
            source = ctypes.create_string_buffer(bytes(bgra))
            source_header = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, self.width, -self.height, 1, 32, 0, 0, 0, 0, 0, 0))
            api("gdi32", "SetStretchBltMode", ctypes.c_int, w.HDC, ctypes.c_int)(memory, 4)
            api("gdi32", "SetBrushOrgEx", w.BOOL, w.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_void_p)(memory, 0, 0, None)
            result = api("gdi32", "StretchDIBits", ctypes.c_int, w.HDC, ctypes.c_int, ctypes.c_int,
                         ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                         ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, w.UINT, w.DWORD)(
                             memory, 0, 0, width, height, 0, 0, self.width, self.height, source, source_header, 0, 0x00CC0020)
            if result <= 0:
                raise OSError("Windows could not resize the image")
            pixels = ctypes.string_at(bits, width * height * 4)
            rgb = bytearray(width * height * 3)
            rgb[0::3], rgb[1::3], rgb[2::3] = pixels[2::4], pixels[1::4], pixels[0::4]
            rgb = bytes(rgb)
            return Image(self.rect, width, height, rgb, png_rgb(width, height, rgb))
        finally:
            if previous:
                select(memory, previous)
            if bitmap:
                api("gdi32", "DeleteObject", w.BOOL, w.HANDLE)(bitmap)
            if memory:
                api("gdi32", "DeleteDC", w.BOOL, w.HDC)(memory)

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
        # Do not wait for the lock held while PowerShell is being created.
        # run() also checks cancellation immediately after Popen completes.
        process = self.process
        if process and process.poll() is None:
            try:
                process.kill()
            except OSError:
                pass

    def run(self, request):
        executable = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW | 0x80
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
            if self.cancelled.is_set():
                raise InterruptedError("Cancelled")
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
        return refine(blocks, candidates)


def re_literal(text):
    import re
    return bool(re.fullmatch(r"\d+(?:[.,]\d+)*(?:[%+])?", text.strip()))

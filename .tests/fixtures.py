"""Synthetic pixels only. No screen reads, foreground access or input events."""
import ctypes
from ctypes import wintypes as w
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from desktop import Image, Rect, api, png_rgb


def image_fixture(width=600, height=180):
    memory = api("gdi32", "CreateCompatibleDC", w.HDC, w.HDC)(None)
    header = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0))
    bits = ctypes.c_void_p()
    bitmap = api("gdi32", "CreateDIBSection", w.HBITMAP, w.HDC, ctypes.c_void_p, w.UINT,
                 ctypes.POINTER(ctypes.c_void_p), w.HANDLE, w.DWORD)(memory, header, 0, ctypes.byref(bits), None, 0)
    select = api("gdi32", "SelectObject", w.HANDLE, w.HDC, w.HANDLE)
    previous = select(memory, bitmap)
    font = None
    old_font = None
    try:
        api("gdi32", "PatBlt", w.BOOL, w.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int,
            ctypes.c_int, w.DWORD)(memory, 0, 0, width, height, 0x00FF0062)
        font = api("gdi32", "CreateFontW", w.HFONT, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                   ctypes.c_int, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD,
                   w.DWORD, w.LPCWSTR)(-28, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 4, 0, "Segoe UI")
        old_font = select(memory, font)
        api("gdi32", "SetBkMode", ctypes.c_int, w.HDC, ctypes.c_int)(memory, 1)
        box = Rect(24, 20, width - 20, height - 10)
        text = "Hello world\nScreenshot translation\nPull requests 499"
        api("user32", "DrawTextW", ctypes.c_int, w.HDC, w.LPCWSTR, ctypes.c_int,
            ctypes.POINTER(Rect), w.UINT)(memory, text, len(text), ctypes.byref(box), 0x810)
        bgra = ctypes.string_at(bits, width * height * 4)
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = bgra[2::4], bgra[1::4], bgra[0::4]
        rgb = bytes(rgb)
    finally:
        if old_font:
            select(memory, old_font)
        if font:
            api("gdi32", "DeleteObject", w.BOOL, w.HANDLE)(font)
        select(memory, previous)
        api("gdi32", "DeleteObject", w.BOOL, w.HANDLE)(bitmap)
        api("gdi32", "DeleteDC", w.BOOL, w.HDC)(memory)
    return Image((0, 0, width, height), width, height, rgb, png_rgb(width, height, rgb))


def memory_stats():
    class Counters(ctypes.Structure):
        _fields_ = [("cb", w.DWORD), ("PageFaultCount", w.DWORD)] + [
            (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                                               "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                                               "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
    handle = api("kernel32", "GetCurrentProcess", w.HANDLE)()
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    api("psapi", "GetProcessMemoryInfo", w.BOOL, w.HANDLE, ctypes.POINTER(Counters), w.DWORD)(
        handle, ctypes.byref(counters), counters.cb)
    resources = api("user32", "GetGuiResources", w.DWORD, w.HANDLE, w.DWORD)
    return {"workingMB": round(counters.WorkingSetSize / 1048576, 1),
            "privateMB": round(counters.PrivateUsage / 1048576, 1),
            "gdi": resources(handle, 0), "user": resources(handle, 1)}

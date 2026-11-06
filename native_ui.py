"""Lightweight Win32 selection and asynchronous input; no Qt or input hooks."""
import ctypes
from ctypes import wintypes as w
import threading
import time

import desktop

LRESULT = ctypes.c_ssize_t
WNDPROC = ctypes.WINFUNCTYPE(LRESULT, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
user = ctypes.WinDLL("user32", use_last_error=True)
gdi = ctypes.WinDLL("gdi32", use_last_error=True)
kernel = ctypes.WinDLL("kernel32", use_last_error=True)


def api(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype, function.argtypes = result, arguments
    return function


class WindowClass(ctypes.Structure):
    _fields_ = [("size", w.UINT), ("style", w.UINT), ("procedure", WNDPROC),
                ("classExtra", ctypes.c_int), ("windowExtra", ctypes.c_int),
                ("instance", w.HINSTANCE), ("icon", w.HICON), ("cursor", w.HANDLE),
                ("background", w.HBRUSH), ("menu", w.LPCWSTR), ("name", w.LPCWSTR),
                ("smallIcon", w.HICON)]


class Paint(ctypes.Structure):
    _fields_ = [("dc", w.HDC), ("erase", w.BOOL), ("rect", desktop.Rect),
                ("restore", w.BOOL), ("update", w.BOOL), ("reserved", ctypes.c_byte * 32)]


class RawDevice(ctypes.Structure):
    _fields_ = [("page", w.USHORT), ("usage", w.USHORT), ("flags", w.DWORD), ("target", w.HWND)]


class RawHeader(ctypes.Structure):
    _fields_ = [("kind", w.DWORD), ("size", w.DWORD), ("device", w.HANDLE), ("parameter", w.WPARAM)]


class MouseButtons(ctypes.Structure):
    _fields_ = [("flags", w.USHORT), ("data", w.USHORT)]


class MouseButtonUnion(ctypes.Union):
    _fields_ = [("buttons", w.ULONG), ("split", MouseButtons)]


class RawMouse(ctypes.Structure):
    _anonymous_ = ("buttonUnion",)
    _fields_ = [("flags", w.USHORT), ("buttonUnion", MouseButtonUnion),
                ("rawButtons", w.ULONG), ("lastX", w.LONG), ("lastY", w.LONG),
                ("extra", w.ULONG)]


class NativeThread:
    """Window messages stay on a separate thread on the caller's desktop."""
    def __init__(self, name):
        self.name = name + "-" + str(id(self))
        self.desktop = api(user, "GetThreadDesktop", w.HANDLE, w.DWORD)(
            api(kernel, "GetCurrentThreadId", w.DWORD)())
        self.ready = threading.Event()
        self.stopped = threading.Event()
        self.cancelled = threading.Event()
        self.error, self.thread_id = None, None
        self.windows = []
        self.callback = WNDPROC(self.dispatch)
        self.defproc = api(user, "DefWindowProcW", LRESULT, w.HWND, w.UINT, w.WPARAM, w.LPARAM)
        self.post = api(user, "PostThreadMessageW", w.BOOL, w.DWORD, w.UINT, w.WPARAM, w.LPARAM)
        self.thread = threading.Thread(target=self.run, daemon=True, name=name)

    def start(self):
        self.thread.start()

    def run(self):
        instance = None
        registered = False
        try:
            if self.cancelled.is_set():
                return
            if not api(user, "SetThreadDesktop", w.BOOL, w.HANDLE)(self.desktop):
                raise OSError("The UI desktop is unavailable")
            api(user, "SetThreadDpiAwarenessContext", w.HANDLE, w.HANDLE)(ctypes.c_void_p(-4))
            self.thread_id = api(kernel, "GetCurrentThreadId", w.DWORD)()
            instance = api(kernel, "GetModuleHandleW", w.HMODULE, w.LPCWSTR)(None)
            cursor = api(user, "LoadCursorW", w.HANDLE, w.HINSTANCE, ctypes.c_void_p)(None, ctypes.c_void_p(32515))
            window_class = WindowClass(ctypes.sizeof(WindowClass), 0, self.callback, 0, 0,
                                       instance, None, cursor, None, None, self.name, None)
            if not api(user, "RegisterClassExW", w.ATOM, ctypes.POINTER(WindowClass))(ctypes.byref(window_class)):
                raise OSError("The UI window class could not be registered")
            registered = True
            self.create(instance)
            self.ready.set()
            message = w.MSG()
            get = api(user, "GetMessageW", w.BOOL, ctypes.POINTER(w.MSG), w.HWND, w.UINT, w.UINT)
            dispatch = api(user, "DispatchMessageW", LRESULT, ctypes.POINTER(w.MSG))
            translate = api(user, "TranslateMessage", w.BOOL, ctypes.POINTER(w.MSG))
            while not self.cancelled.is_set() and get(ctypes.byref(message), None, 0, 0) > 0:
                if message.message == 0x8001:
                    break
                translate(ctypes.byref(message))
                dispatch(ctypes.byref(message))
        except Exception as error:
            self.error = type(error).__name__
            self.failed()
        finally:
            self.ready.set()
            destroy = api(user, "DestroyWindow", w.BOOL, w.HWND)
            for handle in reversed(self.windows):
                destroy(handle)
            self.cleanup()
            if registered:
                api(user, "UnregisterClassW", w.BOOL, w.LPCWSTR, w.HINSTANCE)(self.name, instance)
            self.stopped.set()

    def window(self, instance, extra, style, rect, parent=None):
        x, y, width, height = rect
        handle = api(user, "CreateWindowExW", w.HWND, w.DWORD, w.LPCWSTR, w.LPCWSTR,
                     w.DWORD, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                     w.HWND, w.HMENU, w.HINSTANCE, ctypes.c_void_p)(
                         extra, self.name, "", style, int(x), int(y), int(width), int(height),
                         parent, None, instance, None)
        if not handle:
            raise OSError("The UI window could not be created")
        self.windows.append(handle)
        return handle

    def dispatch(self, handle, message, parameter, data):
        try:
            result = self.message(handle, message, parameter, data)
            if result is not None:
                return result
        except Exception:
            self.error = "message failed"
            self.close()
        return self.defproc(handle, message, parameter, data)

    def close(self):
        self.cancelled.set()
        if self.thread_id is not None:
            self.post(self.thread_id, 0x8001, 0, 0)

    def create(self, instance):
        raise NotImplementedError

    def message(self, handle, message, parameter, data):
        return None

    def cleanup(self):
        pass

    def failed(self):
        pass


class Region(NativeThread):
    """Immediate multi-monitor selector owned by the lightweight broker."""
    def __init__(self, callback):
        super().__init__("screenshot-selector")
        self.callback_result = callback
        self.origin = self.selection = None
        self.completed = False
        self.masks, self.border = [], None
        self.started_at = time.monotonic()
        self.show_ms = None
        self.set_position = api(user, "SetWindowPos", w.BOOL, w.HWND, w.HWND,
                                ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.UINT)
        self.show_window = api(user, "ShowWindow", w.BOOL, w.HWND, ctypes.c_int)
        self.invalidate = api(user, "InvalidateRect", w.BOOL, w.HWND, ctypes.c_void_p, w.BOOL)
        self.set_region = api(user, "SetWindowRgn", ctypes.c_int, w.HWND, w.HANDLE, w.BOOL)
        self.create_region = api(gdi, "CreateRectRgn", w.HANDLE, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int)
        self.delete_object = api(gdi, "DeleteObject", w.BOOL, w.HANDLE)
        self.current_point = desktop.physical_cursor

    def show(self):
        self.start()

    def create(self, instance):
        for display in desktop.monitors():
            # Topmost tool windows, black at 20% opacity; no Qt/DPI conversion.
            handle = self.window(instance, 0x80088, 0x80000000, display["rect"])
            self.masks.append(handle)
            api(user, "SetLayeredWindowAttributes", w.BOOL, w.HWND, w.COLORREF, w.BYTE, w.DWORD)(handle, 0, 51, 2)
            self.show_window(handle, 8)  # Show without stealing activation.
            api(user, "UpdateWindow", w.BOOL, w.HWND)(handle)
        # A separately clipped, opaque border keeps the selection line crisp.
        self.border = self.window(instance, 0x80000 | 0x88 | 0x20 | 0x8000000,
                                  0x80000000, (0, 0, 1, 1))
        api(user, "SetLayeredWindowAttributes", w.BOOL, w.HWND, w.COLORREF, w.BYTE, w.DWORD)(self.border, 0, 255, 2)
        self.show_ms = (time.monotonic() - self.started_at) * 1000
        api(user, "SetForegroundWindow", w.BOOL, w.HWND)(self.masks[0])

    def paint(self, handle):
        paint = Paint()
        dc = api(user, "BeginPaint", w.HDC, w.HWND, ctypes.POINTER(Paint))(handle, ctypes.byref(paint))
        try:
            rect = desktop.Rect()
            api(user, "GetClientRect", w.BOOL, w.HWND, ctypes.POINTER(desktop.Rect))(handle, ctypes.byref(rect))
            brush = api(gdi, "CreateSolidBrush", w.HBRUSH, w.COLORREF)(0xFFCD60 if handle == self.border else 0)
            try:
                api(user, "FillRect", ctypes.c_int, w.HDC, ctypes.POINTER(desktop.Rect), w.HBRUSH)(dc, ctypes.byref(rect), brush)
            finally:
                self.delete_object(brush)
        finally:
            api(user, "EndPaint", w.BOOL, w.HWND, ctypes.POINTER(Paint))(handle, ctypes.byref(paint))

    def update_selection(self, point):
        x, y = self.origin
        left, top, width, height = min(x, point[0]), min(y, point[1]), abs(x - point[0]), abs(y - point[1])
        self.selection = (left, top, width, height)
        if width < 2 or height < 2:
            self.show_window(self.border, 0)
            return
        ring = self.create_region(0, 0, width, height)
        inside = self.create_region(2, 2, max(2, width - 2), max(2, height - 2))
        api(gdi, "CombineRgn", ctypes.c_int, w.HANDLE, w.HANDLE, w.HANDLE, ctypes.c_int)(ring, ring, inside, 4)
        self.delete_object(inside)
        if not self.set_region(self.border, ring, True):
            self.delete_object(ring)
        self.set_position(self.border, -1, left, top, width, height, 0x50)
        self.invalidate(self.border, None, False)

    def message(self, handle, message, parameter, data):
        if message == 0x0f:  # WM_PAINT
            self.paint(handle)
            return 0
        if message == 0x14:  # WM_ERASEBKGND
            return 1
        if message == 0x201:  # left button down
            self.origin = self.current_point()
            api(user, "SetCapture", w.HWND, w.HWND)(handle)
            return 0
        if message == 0x200 and self.origin is not None:
            self.update_selection(self.current_point())
            return 0
        if message == 0x202 and self.origin is not None:
            self.update_selection(self.current_point())
            api(user, "ReleaseCapture", w.BOOL)()
            rect = self.selection
            self.finish(rect if rect and rect[2] >= 3 and rect[3] >= 3 else None)
            return 0
        if message == 0x204 or message == 0x10 or (message == 0x100 and parameter == 27):
            self.finish(None)
            return 0

    def finish(self, rect):
        if not self.completed:
            self.completed = True
            for handle in self.windows:
                self.show_window(handle, 0)
            self.result = rect
            self.close()

    def cleanup(self):
        # Destroy selector HWNDs before requesting the actual capture.
        if not self.completed:
            self.result = None
        self.callback_result(self.result)


class OutsideClick(NativeThread):
    """Raw input sink: asynchronous notifications never delay system clicks."""
    DOWN = 0x1 | 0x4 | 0x10 | 0x40 | 0x100
    registration_lock = threading.Lock()
    registration_owner = None

    def __init__(self, callback):
        super().__init__("screenshot-outside-click")
        self.on_click = callback
        self.sink = None
        self.read_raw = api(user, "GetRawInputData", w.UINT, w.HANDLE, w.UINT,
                            ctypes.c_void_p, ctypes.POINTER(w.UINT), w.UINT)
        self.register_raw = api(user, "RegisterRawInputDevices", w.BOOL,
                                ctypes.POINTER(RawDevice), w.UINT, w.UINT)
        self.current_point = desktop.physical_cursor
        self.message_time = api(user, "GetMessageTime", w.DWORD)

    def create(self, instance):
        # Message-only window, no taskbar entry, activation or visible content.
        self.sink = self.window(instance, 0, 0, (0, 0, 0, 0), -3)
        device = RawDevice(1, 2, 0x100, self.sink)  # mouse, RIDEV_INPUTSINK
        with self.registration_lock:
            if not self.register_raw(ctypes.byref(device), 1, ctypes.sizeof(device)):
                raise OSError("Outside-click input could not be registered")
            OutsideClick.registration_owner = self.sink

    def raw_mouse(self, flags, point, timestamp=None):
        if flags & self.DOWN and not self.cancelled.is_set():
            self.on_click(point, timestamp)

    def message(self, handle, message, parameter, data):
        if message == 0xff:  # WM_INPUT; leave DefWindowProc to release the packet.
            size = w.UINT()
            header_size = ctypes.sizeof(RawHeader)
            if self.read_raw(data, 0x10000003, None, ctypes.byref(size), header_size) != 0:
                return None
            if size.value < header_size + ctypes.sizeof(RawMouse) or size.value > 1024:
                return None
            buffer = ctypes.create_string_buffer(size.value)
            if self.read_raw(data, 0x10000003, buffer, ctypes.byref(size), header_size) == 0xffffffff:
                return None
            header = RawHeader.from_buffer(buffer)
            if header.kind == 0:
                mouse = RawMouse.from_buffer(buffer, header_size)
                if mouse.split.flags & self.DOWN:
                    self.raw_mouse(mouse.split.flags, self.current_point(), self.message_time())
        return None

    def cleanup(self):
        if self.sink:
            with self.registration_lock:
                if OutsideClick.registration_owner == self.sink:
                    device = RawDevice(1, 2, 1, None)  # RIDEV_REMOVE
                    self.register_raw(ctypes.byref(device), 1, ctypes.sizeof(device))
                    OutsideClick.registration_owner = None

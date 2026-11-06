"""Tk UI. All windows and image references are destroyed on close."""
import tkinter as tk
from tkinter import ttk
import tkinter.font as fonts
import re

import desktop
from services import ENDPOINTS, LANGUAGES, NAMES, PROVIDERS, detect, target_for

ZH_LANGUAGES = {"auto": "\u81ea\u52a8", "zh": "\u4e2d\u6587", "en": "\u82f1\u6587",
                "ja": "\u65e5\u6587", "ko": "\u97e9\u6587", "fr": "\u6cd5\u6587",
                "de": "\u5fb7\u6587", "es": "\u897f\u73ed\u7259\u6587", "ru": "\u4fc4\u6587"}


class Theme:
    def __init__(self, dark=False):
        self.dark = dark
        self.page = "#202020" if dark else "#f3f3f3"
        self.surface = "#292929" if dark else "#ffffff"
        self.text = "#f1f1f1" if dark else "#181818"
        self.muted = "#b0b0b0" if dark else "#666666"
        self.border = "#414141" if dark else "#dedede"
        self.accent = "#60cdff" if dark else "#0067c0"
        self.track = "#333333" if dark else "#ececec"


def style(root, theme):
    s = ttk.Style(root)
    s.theme_use("clam")
    s.configure(".", font=("Microsoft YaHei UI", -14), background=theme.page, foreground=theme.text)
    s.configure("TFrame", background=theme.page)
    s.configure("TLabel", background=theme.page, foreground=theme.text)
    s.configure("TButton", padding=(8, 5), background=theme.surface, bordercolor=theme.border,
                lightcolor=theme.surface, darkcolor=theme.surface, focuscolor=theme.surface)
    s.map("TButton", background=[("active", theme.border), ("pressed", theme.border)])
    s.configure("Tool.TButton", padding=4, font=("Segoe Fluent Icons", -18), borderwidth=0,
                relief="flat", background=theme.surface)
    s.configure("Chrome.Tool.TButton", background=theme.page)
    s.configure("Surface.TFrame", background=theme.surface)
    s.configure("Surface.TLabel", background=theme.surface)
    s.configure("Track.TFrame", background=theme.track)
    s.configure("Primary.TButton", background=theme.accent, foreground="#ffffff", borderwidth=0)
    s.map("Primary.TButton", background=[("active", "#005a9e"), ("pressed", "#004578")])
    s.configure("TEntry", fieldbackground=theme.surface, foreground=theme.text, padding=8,
                bordercolor=theme.border, lightcolor=theme.surface, darkcolor=theme.surface,
                insertcolor=theme.text)
    s.configure("TSeparator", background=theme.border)
    s.configure("Language.TButton", padding=(10, 7), background=theme.surface, borderwidth=0, relief="flat", anchor="w")
    s.configure("TCombobox", fieldbackground=theme.surface, background=theme.surface, foreground=theme.text, padding=4)
    s.map("TCombobox", fieldbackground=[("readonly", theme.surface)], foreground=[("readonly", theme.text)])
    s.layout("Tab.TRadiobutton", [("Radiobutton.padding", {"sticky": "nswe", "children": [
        ("Radiobutton.label", {"sticky": "nswe"})]})])
    s.configure("Tab.TRadiobutton", padding=(12, 7), background=theme.track,
                borderwidth=0, relief="flat")
    s.map("Tab.TRadiobutton", background=[("selected", theme.surface), ("active", theme.border)],
          foreground=[("selected", theme.text)])
    s.configure("Compare.Toolbutton", padding=(8, 6), background=theme.surface, borderwidth=0, relief="flat")
    s.map("Compare.Toolbutton", background=[("selected", theme.track), ("active", theme.track)])
    s.configure("TCheckbutton", background=theme.page)
    root.option_add("*TCombobox*Listbox.background", theme.surface)
    root.option_add("*TCombobox*Listbox.foreground", theme.text)


class Tooltip:
    def __init__(self, widget, text):
        self.widget, self.text, self.tip = widget, text, None
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)
        widget.bind("<Destroy>", self.hide)

    def show(self, event=None):
        if self.tip:
            return
        self.tip = tk.Toplevel(self.widget)
        self.tip.withdraw()
        self.tip.overrideredirect(True)
        self.tip.attributes("-topmost", True)
        ttk.Label(self.tip, text=self.text, padding=(8, 4)).pack()
        self.tip.geometry(f"+{self.widget.winfo_rootx()}+{self.widget.winfo_rooty() + self.widget.winfo_height() + 5}")
        self.tip.deiconify()

    def hide(self, event=None):
        if self.tip:
            self.tip.destroy()
            self.tip = None


def tool(parent, glyph, label, command):
    button = ttk.Button(parent, text=glyph, width=3, style="Tool.TButton", command=command)
    if label:
        Tooltip(button, label)
    return button


class LanguagePicker(ttk.Frame):
    def __init__(self, parent, variable, values, command):
        super().__init__(parent, style="Surface.TFrame")
        self.variable, self.values, self.command = variable, values, command
        self.button = ttk.Button(self, textvariable=variable, width=9, style="Language.TButton", command=self.open)
        self.button.pack(side="left")
        self.arrow = tool(self, "\ue70d", "", self.open)
        self.arrow.configure(width=2)
        self.arrow.pack(side="left")
        self.button.bind("<Down>", lambda e: self.open())
        self.menu = tk.Menu(self, tearoff=False, font=("Microsoft YaHei UI", -14))
        for label in self.values:
            self.menu.add_radiobutton(label=label, variable=self.variable, value=label, command=self.command)

    def configure(self, **kwargs):
        if "state" in kwargs:
            state = "disabled" if kwargs.pop("state") == "disabled" else "normal"
            self.button.configure(state=state)
            self.arrow.configure(state=state)
        if kwargs:
            super().configure(**kwargs)

    def open(self):
        if self.button.instate(["disabled"]):
            return
        try:
            self.menu.tk_popup(self.winfo_rootx(), self.winfo_rooty() + self.winfo_height())
        finally:
            self.menu.grab_release()


class Region:
    def __init__(self, app, callback):
        self.app, self.callback, self.start, self.selection = app, callback, None, None
        self.rect = desktop.virtual_screen()
        self.window = tk.Toplevel(app.root)
        self.window.withdraw()
        self.window.overrideredirect(True)
        self.window.attributes("-topmost", True)
        self.window.attributes("-alpha", .22)
        self.window.configure(bg="#000000")
        self.window.geometry(f"{self.rect[2]}x{self.rect[3]}")
        self.canvas = tk.Canvas(self.window, bg="#000000", highlightthickness=0, cursor="crosshair")
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<ButtonPress-1>", self.down)
        self.canvas.bind("<B1-Motion>", self.move)
        self.canvas.bind("<ButtonRelease-1>", self.up)
        self.window.bind("<Escape>", lambda e: self.close(None))
        self.window.bind("<Button-3>", lambda e: self.close(None))

    def show(self):
        self.window.deiconify()
        self.window.update_idletasks()
        desktop.position(self.window, self.rect[0], self.rect[1])
        self.window.focus_force()
        self.window.grab_set()

    def down(self, event):
        self.start = (event.x, event.y)
        if self.selection:
            self.canvas.delete(self.selection)
        self.selection = self.canvas.create_rectangle(event.x, event.y, event.x, event.y, outline="#60cdff", width=3)

    def move(self, event):
        if self.start:
            self.canvas.coords(self.selection, *self.start, event.x, event.y)

    def up(self, event):
        if not self.start:
            return
        x, y = min(event.x, self.start[0]), min(event.y, self.start[1])
        width, height = abs(event.x - self.start[0]), abs(event.y - self.start[1])
        self.close((self.rect[0] + x, self.rect[1] + y, width, height) if width >= 3 and height >= 3 else None)

    def close(self, rect):
        if not self.window.winfo_exists():
            return
        self.window.grab_release()
        self.window.destroy()
        self.app.region = None
        # Allow the compositor to remove the selection mask before capture.
        self.app.root.after(100, lambda: self.callback(rect))


class Pin:
    def __init__(self, app, image):
        self.app, self.image, self.blocks, self.data = app, image, [], {}
        self.closed, self.drag, self.loading, self.status = False, None, True, ""
        self.area = desktop.monitor_rect(image.rect, work=True)
        self.origin = image.rect[:2]
        self.visible = False
        self.opacity = 1.0
        self.compare, self.source, self.target = tk.BooleanVar(value=False), tk.StringVar(value="auto"), tk.StringVar(value="zh")
        self.window = tk.Toplevel(app.root)
        self.window.withdraw()
        self.window.overrideredirect(True)
        self.window.attributes("-topmost", True)
        self.window.configure(bg=app.theme.border)
        self.body = ttk.Frame(self.window, padding=1, style="Surface.TFrame")
        self.body.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(self.body, highlightthickness=0, bg=app.theme.surface)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.scrollbar = ttk.Scrollbar(self.body, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.body.rowconfigure(0, weight=1)
        self.body.columnconfigure(0, weight=1)
        self.photo = None
        self.display_image = None
        self.factor = 1.0
        self.toolbar_window = tk.Toplevel(self.window)
        self.toolbar_window.withdraw()
        self.toolbar_window.overrideredirect(True)
        self.toolbar_window.attributes("-topmost", True)
        self.toolbar_window.configure(bg=app.theme.border)
        self.toolbar = ttk.Frame(self.toolbar_window, padding=(7, 5), style="Surface.TFrame")
        self.toolbar.pack(fill="both", expand=True, padx=1, pady=1)
        self.labels = list(ZH_LANGUAGES.values()) if app.chinese else list(LANGUAGES.values())
        self.codes = list(LANGUAGES)
        self.source_display, self.target_display = tk.StringVar(value=self.labels[0]), tk.StringVar(value=self.labels[1])
        self.choices = []
        for var, codevar in ((self.source_display, self.source), (self.target_display, self.target)):
            values = self.labels if codevar is self.source else self.labels[1:]
            choice = LanguagePicker(self.toolbar, var, values, self.change_language)
            choice.configure(state="disabled")
            choice.pack(side="left", padx=2)
            choice.bind("<Escape>", lambda e: self.close())
            self.choices.append(choice)
            if len(self.choices) == 1:
                self.swap_button = tool(self.toolbar, "\ue8ab", app.t("\u4ea4\u6362\u8bed\u8a00", "Swap languages"), self.swap)
                self.swap_button.pack(side="left", padx=2)
        self.compare_button = ttk.Checkbutton(self.toolbar, text="\u25eb  " + app.t("\u5bf9\u7167", "Compare"),
                                             variable=self.compare, command=self.redraw, style="Compare.Toolbutton")
        self.compare_button.pack(side="left", padx=(8, 6))
        tool(self.toolbar, "\ue8bb", app.t("\u5173\u95ed", "Close"), self.close).pack(side="right")
        tool(self.toolbar, "\ue713", app.t("\u7ffb\u8bd1\u670d\u52a1\u8bbe\u7f6e", "Translation services"), app.show_settings).pack(side="right", padx=2)
        self.menu = tk.Menu(self.window, tearoff=False)
        self.menu.add_command(label=app.t("\u590d\u5236\u8bd1\u6587", "Copy translation"), command=self.copy)
        self.menu.add_command(label=app.t("\u590d\u5236\u539f\u6587", "Copy source"), command=lambda: self.copy(True))
        self.menu.add_command(label=app.t("\u91cd\u65b0\u622a\u56fe", "Capture again"), command=self.retry)
        self.menu.add_command(label=app.t("\u5b8c\u6574\u8bd1\u6587", "Full translation"), command=self.show_full_text)
        self.menu.add_separator()
        self.menu.add_command(label=app.t("\u5173\u95ed", "Close"), command=self.close)
        self.canvas.bind("<Button-3>", lambda e: self.menu.tk_popup(e.x_root, e.y_root))
        self.canvas.bind("<Double-Button-1>", lambda e: self.close())
        self.canvas.bind("<ButtonPress-1>", self.down)
        self.canvas.bind("<B1-Motion>", self.move)
        self.canvas.bind("<ButtonRelease-1>", lambda e: setattr(self, "drag", None))
        self.canvas.bind("<MouseWheel>", self.scroll_or_opacity)
        self.toolbar.bind("<ButtonPress-1>", self.down)
        self.toolbar.bind("<B1-Motion>", self.move)
        self.toolbar.bind("<ButtonRelease-1>", lambda e: setattr(self, "drag", None))
        self.toolbar.bind("<Double-Button-1>", lambda e: self.close())
        self.toolbar_window.bind("<Escape>", lambda e: self.close())
        self.toolbar_window.bind("<Control-c>", lambda e: self.copy())
        self.window.bind("<Escape>", lambda e: self.close())
        self.canvas.bind("<Tab>", self.toggle_compare)
        self.window.bind("<Control-c>", lambda e: self.copy())
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self.font = fonts.Font(root=self.window, family="Microsoft YaHei UI", size=-14)
        self.details = None
        self.text_items = []
        self.prepare_image()
        self.update_status(app.t("\u6b63\u5728\u8bc6\u522b\u2026", "Recognizing\u2026"))

    def show(self):
        self.visible = True
        self.layout_image()
        self.window.deiconify()
        self.toolbar_window.deiconify()
        self.window.update_idletasks()
        self.position_windows()
        desktop.decorate(self.window, self.app.theme.dark)
        desktop.decorate(self.toolbar_window, self.app.theme.dark, desktop.native_handle(self.window))
        self.window.focus_force()

    def prepare_image(self):
        self.toolbar_window.update_idletasks()
        available = max(48, self.area[3] - self.toolbar_window.winfo_reqheight() - 18)
        factor = min(1, (self.area[2] - 18) / self.image.width, available / self.image.height)
        width, height = max(1, int(self.image.width * factor)), max(1, int(self.image.height * factor))
        if self.display_image and (width, height) == (self.display_image.width, self.display_image.height):
            return
        self.display_image = self.image.resized(width, height)
        self.factor = width / self.image.width
        self.photo = tk.PhotoImage(master=self.window, data=self.display_image.png)

    def layout_image(self):
        self.window.update_idletasks()
        toolbar_width = min(self.area[2], self.toolbar_window.winfo_reqwidth())
        toolbar_height = self.toolbar_window.winfo_reqheight()
        available = max(48, self.area[3] - toolbar_height - 18)
        self.overflow = self.content_height > available
        self.view_width = min(self.area[2], self.content_width + 2 + (14 if self.overflow else 0))
        self.view_height = min(available, self.content_height + 2)
        if self.overflow:
            self.scrollbar.grid(row=0, column=1, sticky="ns")
        else:
            self.scrollbar.grid_remove()
            self.canvas.yview_moveto(0)
        self.window.geometry(f"{self.view_width}x{self.view_height}")
        self.toolbar_window.geometry(f"{toolbar_width}x{toolbar_height}")
        self.position_windows()

    def position_windows(self):
        ax, ay, aw, ah = self.area
        th = self.toolbar_window.winfo_reqheight()
        tw = min(aw, self.toolbar_window.winfo_reqwidth())
        x = max(ax, min(self.origin[0], ax + aw - self.view_width))
        y = max(ay, min(self.origin[1], ay + ah - self.view_height - th - 8))
        self.origin = (x, y)
        self.toolbar_origin = (max(ax, min(x + (self.view_width - tw) // 2, ax + aw - tw)), y + self.view_height + 8)
        desktop.position(self.window, int(x), int(y))
        desktop.position(self.toolbar_window, *map(int, self.toolbar_origin))

    def toggle_compare(self, event=None):
        self.compare.set(not self.compare.get())
        self.redraw()
        return "break"

    def scroll_or_opacity(self, event):
        if self.overflow:
            self.canvas.yview_scroll(-int(event.delta / 120) * 2, "units")
        else:
            self.opacity = max(.3, min(1.0, self.opacity + (.05 if event.delta > 0 else -.05)))
            self.window.attributes("-alpha", self.opacity)
        return "break"

    def down(self, event):
        self.drag = (event.x_root, event.y_root, self.window.winfo_rootx(), self.window.winfo_rooty())

    def move(self, event):
        if self.drag:
            x, y, left, top = self.drag
            self.origin = (left + event.x_root - x, top + event.y_root - y)
            area = desktop.monitor_rect((*self.origin, self.view_width, self.view_height), work=True)
            if area != self.area:
                self.area = area
                self.prepare_image()
                self.redraw()
            else:
                self.position_windows()

    def update_status(self, text, busy=True):
        if self.closed:
            return
        self.status, self.loading = text, busy
        for choice in self.choices:
            choice.configure(state="disabled" if busy or not self.blocks else "readonly")
        self.swap_button.configure(state="disabled" if busy or not self.data else "normal")
        self.redraw()

    def set_source(self, blocks, source, target):
        if self.closed:
            return
        self.blocks = blocks
        self.source.set(source)
        self.target.set(target)
        self.source_display.set(self.labels[self.codes.index(source)] if source in self.codes else source)
        self.target_display.set(self.labels[self.codes.index(target)])

    def set_result(self, blocks, data):
        if self.closed:
            return
        self.blocks, self.data = blocks, data
        self.source.set(data["sourceLanguage"])
        self.target.set(data["targetLanguage"])
        self.source_display.set(self.labels[self.codes.index(self.source.get())] if self.source.get() in self.codes else self.source.get())
        self.target_display.set(self.labels[self.codes.index(self.target.get())])
        self.update_status("", False)

    def redraw(self):
        if self.closed:
            return
        self.canvas.delete("all")
        self.canvas.configure(bg=self.app.theme.surface)
        self.window.configure(bg=self.app.theme.border)
        self.toolbar_window.configure(bg=self.app.theme.border)
        self.text_items = []
        caption = 24 if self.compare.get() else 0
        self.content_width, self.content_height = self.display_image.width, self.display_image.height
        self.canvas.create_image(0, caption, anchor="nw", image=self.photo)
        if self.data:
            self.paint_blocks(caption)
        self.content_height += caption
        if caption:
            self.canvas.create_text(10, 12, anchor="w", text=self.app.t("\u8bd1\u6587", "Translation"),
                                    fill=self.app.theme.muted, font=("Microsoft YaHei UI", -12))
            original_top = self.content_height
            self.canvas.create_line(0, original_top, self.content_width, original_top, fill=self.app.theme.border)
            self.canvas.create_text(10, original_top + 12, anchor="w", text=self.app.t("\u539f\u6587", "Original"),
                                    fill=self.app.theme.muted, font=("Microsoft YaHei UI", -12))
            self.canvas.create_image(0, original_top + 24, anchor="nw", image=self.photo)
            self.content_height += self.display_image.height + 24
        self.canvas.configure(scrollregion=(0, 0, self.content_width, self.content_height))
        if self.status:
            if not self.loading:
                self.content_width = max(260, self.content_width)
                self.content_height = max(100, self.content_height)
            x, y = self.content_width / 2, min(self.content_height / 2, 100)
            label = self.canvas.create_text(x, y, text=self.status, fill=self.app.theme.text,
                                            font=("Microsoft YaHei UI", 10), width=max(1, min(self.content_width - 16, 420)), justify="center")
            box = self.canvas.bbox(label)
            background = self.canvas.create_rectangle(box[0] - 9, box[1] - 6, box[2] + 9, box[3] + 6,
                                                       fill=self.app.theme.surface, outline=self.app.theme.border)
            self.canvas.tag_lower(background, label)
        self.layout_image()

    def paint_blocks(self, offset):
        scaled = [{**block, "box": [v * self.factor for v in block["box"]]} for block in self.blocks]
        for block, text in zip(scaled, self.data["segments"]):
            if desktop.re_literal(block["text"]):
                continue
            x, y, width, height = block["box"]
            right, bottom = self.display_image.width, self.display_image.height
            for other in scaled:
                if other is block:
                    continue
                a, b, c, d = other["box"]
                if b < y + height and b + d > y and a >= x + width:
                    right = min(right, a - 5)
                if a < x + width and a + c > x and b >= y + height:
                    bottom = min(bottom, b - 3)
            # Expansion is local to a text block, never a whole-image reflow.
            y = max(0, y - 2)
            cell_width = max(1, min(right - x, width + max(12, height)))
            cell_height = max(1, min(bottom - y, max(26, height * 2 + 6)))
            background, foreground = self.display_image.backdrop(block["box"])
            self.canvas.create_rectangle(max(0, x - 1), y + offset, x + width + 1, y + height + 4 + offset, fill=background, outline="")
            size = min(26, max(14, int(block.get("glyphHeight", height) * self.factor * .95)))
            item = self.canvas.create_text(x, y + offset, text=text, anchor="nw", fill=foreground,
                                           width=cell_width, font=self.font)
            def fits():
                box = self.canvas.bbox(item)
                return box and box[2] <= x + cell_width + 1 and box[3] <= y + offset + cell_height + 1
            while size >= 12:
                self.canvas.itemconfigure(item, font=("Microsoft YaHei UI", -size))
                if fits():
                    break
                size -= 1
            if not fits():
                if len(self.blocks) == 1:
                    self.paint_readable(offset)
                    return
                # Keep neighbouring labels fixed; full text remains available from the context menu.
                cell_height = max(1, bottom - y)
                lo, hi = 0, len(text)
                while lo < hi:
                    middle = (lo + hi + 1) // 2
                    self.canvas.itemconfigure(item, text=text[:middle].rstrip() + "\u2026")
                    if fits():
                        lo = middle
                    else:
                        hi = middle - 1
                self.canvas.itemconfigure(item, text=text[:lo].rstrip() + "\u2026" if lo else "")
            box = self.canvas.bbox(item)
            if box:
                cover = self.canvas.create_rectangle(x, y + offset, min(x + cell_width, box[2]),
                                                       min(y + offset + cell_height, box[3]), fill=background, outline="")
                self.canvas.tag_lower(cover, item)
            self.text_items.append(item)

    def paint_readable(self, offset):
        self.canvas.delete("all")
        self.content_width = min(self.area[2] - 18, max(240, min(640, self.display_image.width)))
        text = self.data["translated"]
        if re.search("[\u2e80-\u9fff]", text):
            font = fonts.Font(root=self.window, family="Microsoft YaHei UI", size=-16)
            lines = []
            for paragraph in text.split("\n"):
                while paragraph:
                    end = 1
                    while end < len(paragraph) and font.measure(paragraph[:end + 1]) <= self.content_width - 34:
                        end += 1
                    if end < len(paragraph) and paragraph[end] in "\u3002\uff0c\uff01\uff1f\uff1b\uff1a\u3001\u201d\u2019\uff09\u3011":
                        end = max(1, end - 1)
                    lines.append(paragraph[:end])
                    paragraph = paragraph[end:]
            text = "\n".join(lines)
        item = self.canvas.create_text(16, offset + 16, anchor="nw", text=text,
                                       width=self.content_width - 32, font=("Microsoft YaHei UI", -16), fill=self.app.theme.text)
        self.content_height = max(56, self.canvas.bbox(item)[3] - offset + 16)
        self.text_items = [item]

    def show_full_text(self):
        if not self.data:
            return
        if self.details and self.details.winfo_exists():
            self.details.lift()
            return
        self.details = tk.Toplevel(self.window)
        self.details.title(self.app.t("\u5b8c\u6574\u8bd1\u6587", "Full translation"))
        self.details.attributes("-topmost", True)
        text = tk.Text(self.details, wrap="word", width=55, height=16, padx=16, pady=16,
                       font=("Microsoft YaHei UI", 11), bg=self.app.theme.surface, fg=self.app.theme.text,
                       relief="flat", selectbackground=self.app.theme.accent)
        text.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(self.details, command=text.yview)
        scroll.pack(side="right", fill="y")
        text.configure(yscrollcommand=scroll.set)
        text.insert("1.0", self.data["translated"])
        text.configure(state="disabled")
        self.details.bind("<Escape>", lambda e: self.details.destroy())

    def change_language(self, event=None):
        if self.loading or not self.blocks:
            return
        self.source.set(self.codes[self.labels.index(self.source_display.get())])
        self.target.set(self.codes[self.labels.index(self.target_display.get())])
        if self.target.get() == "auto":
            self.target.set(target_for(self.data["source"]))
        self.app.retranslate(self)

    def swap(self):
        if not self.loading and self.data:
            source, target = self.data["sourceLanguage"], self.data["targetLanguage"]
            if source == "auto":
                source = detect(self.data["source"])
            # Reverse translates the currently displayed translation, preserving boxes.
            self.app.retranslate(self, target, source, self.data["segments"])

    def copy(self, source=False):
        if source and self.blocks:
            self.app.copy("\n".join(block["text"] for block in self.blocks))
            return
        if self.data:
            self.app.copy(self.data["source" if source else "translated"])

    def retry(self):
        self.close()
        self.app.retry_shot()

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.app.cancel_pin(self)
        self.canvas.delete("all")
        self.window.destroy()
        self.photo = self.image = self.display_image = self.font = None
        self.blocks, self.data = [], {}
        if self.app.pin is self:
            self.app.pin = None
        self.app.release_idle()


class Settings:
    def __init__(self, app):
        self.app, self.drafts, self.selected, self.entries = app, {}, None, {}
        self.closed = False
        self.owner = None
        self.window = tk.Toplevel(app.root)
        self.window.withdraw()
        self.window.overrideredirect(True)
        self.window.title(app.t("\u622a\u56fe\u7ffb\u8bd1\u8bbe\u7f6e - Win\u987a", "Screenshot Translate Settings - WinShun"))
        self.window.attributes("-topmost", True)
        self.window.configure(bg=app.theme.page)
        self.window.geometry("900x540")
        self.window.minsize(640, 420)
        self.window.bind("<Escape>", lambda e: self.close())
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self.chrome = ttk.Frame(self.window, padding=(16, 5))
        self.chrome.pack(fill="x")
        title = ttk.Label(self.chrome, text=app.t("\u622a\u56fe\u7ffb\u8bd1\u8bbe\u7f6e - Win\u987a", "Screenshot Translate Settings - WinShun"), font=("Microsoft YaHei UI", -12))
        title.pack(side="left")
        close = tool(self.chrome, "\ue8bb", app.t("\u5173\u95ed", "Close"), self.close)
        close.configure(style="Chrome.Tool.TButton")
        close.pack(side="right")
        self.drag = None
        for widget in (self.chrome, title):
            widget.bind("<ButtonPress-1>", self.drag_start)
            widget.bind("<B1-Motion>", self.drag_move)
        grip = ttk.Sizegrip(self.window)
        grip.place(relx=1, rely=1, anchor="se")
        grip.bind("<ButtonPress-1>", self.resize_start)
        grip.bind("<B1-Motion>", self.resize_move)
        ttk.Label(self.window, text=app.t("\u622a\u56fe\u7ffb\u8bd1", "Screenshot Translate"),
                  font=("Microsoft YaHei UI", 18, "bold"), padding=(24, 20)).pack(anchor="w")
        self.channel = tk.StringVar(value=app.active_provider())
        self.tabs = ttk.Frame(self.window, padding=4, style="Track.TFrame")
        self.tabs.pack(fill="x", padx=24)
        self.tab_buttons = []
        names = ["MyMemory", "Google", "Edge", "Zvo", "\u5fae\u8f6f", "DeepL", "OpenAI", "DeepSeek", "\u8c46\u5305", "\u6df7\u5143"] if app.chinese else NAMES
        for provider, name in zip(PROVIDERS, names):
            self.tab_buttons.append(ttk.Radiobutton(
                self.tabs, text=name, value=provider, variable=self.channel,
                style="Tab.TRadiobutton", command=self.select))
        self.tab_columns = 0
        self.tabs.bind("<Configure>", lambda event: self.arrange_tabs(event.width))
        self.arrange_tabs(920)
        footer = ttk.Frame(self.window, padding=(24, 16))
        footer.pack(fill="x", side="bottom")
        self.save_button = ttk.Button(footer, text=app.t("\u4fdd\u5b58", "Save"), style="Primary.TButton", command=self.save, width=9)
        self.save_button.pack(side="right")
        ttk.Button(footer, text=app.t("\u53d6\u6d88", "Cancel"), command=self.close, width=9).pack(side="right", padx=12)
        self.test_button = ttk.Button(footer, text=app.t("测试翻译", "Test translation"), command=self.test_service)
        self.test_button.pack(side="right", padx=(0, 8))
        self.testing = False
        self.message = ttk.Label(footer, text="", wraplength=330)
        self.message.pack(side="left", fill="x", expand=True)
        viewport = ttk.Frame(self.window)
        viewport.pack(fill="both", expand=True, padx=24, pady=(16, 0))
        self.scroll_canvas = tk.Canvas(viewport, highlightthickness=0, bg=app.theme.page)
        self.scroll_canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar = ttk.Scrollbar(viewport, command=self.scroll_canvas.yview)
        self.scroll_canvas.configure(yscrollcommand=self.scrollbar.set)
        self.content = ttk.Frame(self.scroll_canvas, style="Surface.TFrame")
        self.content_item = self.scroll_canvas.create_window(0, 0, window=self.content, anchor="nw")
        self.content.bind("<Configure>", self.layout_content)
        self.scroll_canvas.bind("<Configure>", self.layout_content)
        self.window.bind("<MouseWheel>", lambda e: self.scroll_canvas.yview_scroll(-int(e.delta / 120) * 2, "units"))
        self.rows = []
        self.select()

    def drag_start(self, event):
        self.drag = (event.x_root, event.y_root, self.window.winfo_rootx(), self.window.winfo_rooty())

    def drag_move(self, event):
        if self.drag:
            x, y, left, top = self.drag
            desktop.position(self.window, left + event.x_root - x, top + event.y_root - y)

    def resize_start(self, event):
        self.resize_origin = (event.x_root, event.y_root, self.window.winfo_width(), self.window.winfo_height())
        return "break"

    def resize_move(self, event):
        x, y, width, height = self.resize_origin
        self.window.geometry(f"{max(640, width + event.x_root - x)}x{max(420, height + event.y_root - y)}")
        return "break"

    def layout_content(self, event=None):
        width = max(1, self.scroll_canvas.winfo_width())
        self.scroll_canvas.itemconfigure(self.content_item, width=width)
        height = self.content.winfo_reqheight()
        self.scroll_canvas.configure(scrollregion=(0, 0, width, height))
        if height > self.scroll_canvas.winfo_height():
            self.scrollbar.pack(side="right", fill="y")
        else:
            self.scrollbar.pack_forget()
            self.scroll_canvas.yview_moveto(0)

    def arrange_tabs(self, width):
        cell = max(button.winfo_reqwidth() for button in self.tab_buttons) + 4
        columns = max(1, min(len(self.tab_buttons), int((width - 8) // cell)))
        if columns == self.tab_columns:
            return
        self.tab_columns = columns
        for column in range(len(self.tab_buttons)):
            self.tabs.columnconfigure(column, weight=1 if column < columns else 0,
                                      uniform="providers" if column < columns else "")
        for index, button in enumerate(self.tab_buttons):
            button.grid(row=index // columns, column=index % columns, sticky="ew", padx=2, pady=2)

    def show(self):
        self.owner = self.owner or desktop.external_foreground()
        self.window.deiconify()
        self.window.update_idletasks()
        desktop.decorate(self.window, self.app.theme.dark, self.owner)
        self.window.lift()
        self.window.focus_force()

    def remember(self):
        if self.selected and self.entries:
            self.drafts[self.selected].update({key: var.get() for key, var in self.entries.items()})

    def test_service(self):
        if self.testing:
            return
        self.remember()
        provider = self.selected
        profile = {**self.drafts[provider], "provider": provider, "proxy": self.app.settings.get("proxy", "")}
        self.testing = True
        self.test_button.configure(state="disabled")
        self.message.configure(text=self.app.t("测试中…", "Testing…"))
        import threading
        def work():
            try:
                result = self.app.translator.translate(profile, ["Hello world"], "en", "zh")
                message = self.app.t("连接成功：", "Connected: ") + result["translated"]
            except Exception as ex:
                message = str(ex) if isinstance(ex, (ValueError, TimeoutError)) else self.app.t("连接失败，请检查网络", "Connection failed; check network")
                if message == "HTTP 429":
                    message = self.app.t("服务限流（HTTP 429），可测试其他渠道", "Rate limited (HTTP 429); test another service")
            def done():
                self.testing = False
                if not self.closed:
                    self.test_button.configure(state="normal")
                    if self.selected == provider:
                        self.message.configure(text=message)
            self.app.events.put(done)
        threading.Thread(target=work, daemon=True, name="service-test").start()

    def apply_theme(self):
        self.window.configure(bg=self.app.theme.page)
        self.scroll_canvas.configure(bg=self.app.theme.page)
        self.select()
        if self.window.winfo_viewable():
            desktop.decorate(self.window, self.app.theme.dark, self.owner)

    def select(self):
        self.remember()
        provider = self.channel.get()
        self.selected, self.entries = provider, {}
        for child in self.content.winfo_children():
            child.destroy()
        self.rows = []
        self.message.configure(text="")
        self.scroll_canvas.yview_moveto(0)
        if provider not in self.drafts:
            self.drafts[provider] = self.app.profiles.get(provider)
        draft = self.drafts[provider]
        if provider == "mymemory":
            self.add_row(self.app.t("API \u5bc6\u94a5", "API key"), value=self.app.t("\u65e0\u9700\u5bc6\u94a5", "Not required"))
            text = (f"{self.app.usage.today():,} / 5,000 " +
                    self.app.t("\u5b57\u7b26\uff08\u672c\u63d2\u4ef6\u4eca\u65e5\u63d0\u4ea4\uff09", "characters submitted today by this plugin"))
            if not self.app.usage.available:
                text = self.app.t("\u7edf\u8ba1\u6682\u4e0d\u53ef\u7528", "Usage unavailable")
            text = text.split(" characters")[0].split(" \u5b57\u7b26")[0]
            self.add_row(self.app.t("\u672c\u63d2\u4ef6\u4eca\u65e5\u63d0\u4ea4", "Submitted today"),
                         self.app.t("\u6309\u672c\u673a\u65e5\u671f\u7edf\u8ba1\uff0c\u975e\u670d\u52a1\u4f59\u989d", "Local date, not server balance"), value=text)
            return
        fields = []
        if provider not in ("google", "edge"):
            fields.append(("apiKey", self.app.t("API \u5bc6\u94a5\uff08\u53ef\u9009\uff09", "API key (optional)") if provider == "zvo" else self.app.t("API \u5bc6\u94a5", "API key")))
        else:
            self.add_row(self.app.t("API \u5bc6\u94a5", "API key"), value=self.app.t("\u65e0\u9700\u5bc6\u94a5", "Not required"))
        fields.append(("endpoint", self.app.t("\u670d\u52a1\u5730\u5740", "Endpoint")))
        if provider in PROVIDERS[6:]:
            fields.append(("model", self.app.t("\u6a21\u578b / \u63a5\u5165\u70b9 ID", "Model / Endpoint ID")))
        if provider == "microsoft":
            fields.append(("region", self.app.t("\u670d\u52a1\u533a\u57df", "Region")))
        for key, label in fields:
            var = tk.StringVar(value=draft.get(key, ""))
            self.entries[key] = var
            hint = self.app.t("Windows \u5f53\u524d\u7528\u6237\u52a0\u5bc6", "Encrypted for this Windows user") if key == "apiKey" else ""
            if key == "endpoint":
                hint = ENDPOINTS[provider]
            row = self.add_row(label, hint)
            field = ttk.Frame(row, style="Surface.TFrame")
            field.grid(row=0, column=1, rowspan=2, sticky="ew", padx=(20, 0))
            entry = ttk.Entry(field, textvariable=var, show="\u2022" if key == "apiKey" else "")
            if key == "apiKey":
                tool(field, "\ue890", self.app.t("\u663e\u793a / \u9690\u85cf\u5bc6\u94a5", "Show / hide key"),
                     lambda e=entry: e.configure(show="" if e.cget("show") else "\u2022")).pack(side="right")
            entry.pack(side="left", fill="x", expand=True)

    def add_row(self, title, hint="", value=""):
        if self.rows:
            ttk.Separator(self.content).pack(fill="x")
        row = ttk.Frame(self.content, padding=(20, 16), style="Surface.TFrame")
        row.pack(fill="x")
        row.columnconfigure(0, weight=1, uniform="settings")
        row.columnconfigure(1, weight=1, uniform="settings")
        title_label = ttk.Label(row, text=title, style="Surface.TLabel")
        title_label.grid(row=0, column=0, sticky="w")
        hint_label = ttk.Label(row, text=hint, foreground=self.app.theme.muted, style="Surface.TLabel", font=("Microsoft YaHei UI", -12))
        if hint:
            hint_label.grid(row=1, column=0, sticky="w", pady=(8, 0))
        if value:
            ttk.Label(row, text=value, style="Surface.TLabel", anchor="e").grid(row=0, column=1, rowspan=2, sticky="ew")
        def wrap(event):
            hint_label.configure(wraplength=max(80, (event.width - 60) // 2))
        row.bind("<Configure>", wrap)
        self.rows.append(row)
        return row

    def save(self):
        self.remember()
        try:
            self.app.profiles.save(self.drafts)
            self.close()
        except (OSError, ValueError):
            self.message.configure(text=self.app.t("\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u5730\u5740\u548c\u6570\u636e\u76ee\u5f55", "Save failed. Check the endpoint and data directory."))

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.window.destroy()
        self.drafts.clear()
        self.entries.clear()
        if self.app.config is self:
            self.app.config = None
        self.app.release_idle()

"""Tk UI. All windows and image references are destroyed on close."""
import tkinter as tk
from tkinter import ttk
import tkinter.font as fonts

import desktop
from services import ENDPOINTS, LANGUAGES, NAMES, PROVIDERS, detect, target_for

ZH_LANGUAGES = {"auto": "\u81ea\u52a8", "zh": "\u4e2d\u6587", "en": "\u82f1\u6587",
                "ja": "\u65e5\u6587", "ko": "\u97e9\u6587", "fr": "\u6cd5\u6587",
                "de": "\u5fb7\u6587", "es": "\u897f\u73ed\u7259\u6587", "ru": "\u4fc4\u6587"}


class Theme:
    def __init__(self, dark=False):
        self.page = "#202020" if dark else "#f3f3f3"
        self.surface = "#292929" if dark else "#ffffff"
        self.text = "#f1f1f1" if dark else "#181818"
        self.muted = "#b0b0b0" if dark else "#666666"
        self.border = "#414141" if dark else "#dedede"
        self.accent = "#60cdff" if dark else "#0067c0"


def style(root, theme):
    s = ttk.Style(root)
    s.theme_use("clam")
    s.configure(".", font=("Microsoft YaHei UI", 10), background=theme.page, foreground=theme.text)
    s.configure("TFrame", background=theme.page)
    s.configure("TLabel", background=theme.page, foreground=theme.text)
    s.configure("TButton", padding=(8, 5), background=theme.surface, bordercolor=theme.border,
                lightcolor=theme.surface, darkcolor=theme.surface, focuscolor=theme.surface)
    s.map("TButton", background=[("active", theme.border), ("pressed", theme.border)])
    s.configure("Tool.TButton", padding=4, font=("Segoe Fluent Icons", 12))
    s.configure("TEntry", fieldbackground=theme.surface, foreground=theme.text, padding=8)
    s.configure("TCombobox", fieldbackground=theme.surface, background=theme.surface, foreground=theme.text, padding=4)
    s.map("TCombobox", fieldbackground=[("readonly", theme.surface)], foreground=[("readonly", theme.text)])
    s.configure("Tab.TRadiobutton", indicatoron=False, padding=(9, 8), background=theme.page,
                borderwidth=0, relief="flat")
    s.map("Tab.TRadiobutton", background=[("selected", theme.surface)], foreground=[("selected", theme.accent)])
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
    Tooltip(button, label)
    return button


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
        self.compare, self.source, self.target = tk.BooleanVar(value=False), tk.StringVar(value="auto"), tk.StringVar(value="zh")
        self.window = tk.Toplevel(app.root)
        self.window.withdraw()
        self.window.overrideredirect(True)
        self.window.attributes("-topmost", True)
        self.window.configure(bg=app.theme.border)
        self.body = ttk.Frame(self.window, padding=1)
        self.body.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(self.body, highlightthickness=0, bg=app.theme.surface)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.scrollbar = ttk.Scrollbar(self.body, orient="vertical", command=self.canvas.yview)
        self.scrollbar.grid(row=0, column=1, sticky="ns")
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.horizontal = ttk.Scrollbar(self.body, orient="horizontal", command=self.canvas.xview)
        self.horizontal.grid(row=1, column=0, sticky="ew")
        self.canvas.configure(xscrollcommand=self.horizontal.set)
        self.body.rowconfigure(0, weight=1)
        self.body.columnconfigure(0, weight=1)
        self.photo = tk.PhotoImage(master=self.window, data=image.png)
        self.toolbar = ttk.Frame(self.body, padding=(7, 5))
        self.toolbar.grid(row=2, column=0, columnspan=2, sticky="ew")
        self.compare_button = ttk.Checkbutton(self.toolbar, text=app.t("\u5bf9\u7167", "Compare"),
                                             variable=self.compare, command=self.redraw)
        self.compare_button.pack(side="left", padx=(0, 6))
        self.labels = list(ZH_LANGUAGES.values()) if app.chinese else list(LANGUAGES.values())
        self.codes = list(LANGUAGES)
        self.source_display, self.target_display = tk.StringVar(), tk.StringVar()
        self.choices = []
        for var, codevar in ((self.source_display, self.source), (self.target_display, self.target)):
            choice = ttk.Combobox(self.toolbar, textvariable=var, values=self.labels, width=8, state="disabled")
            choice.pack(side="left", padx=2)
            choice.bind("<<ComboboxSelected>>", self.change_language)
            choice.bind("<Escape>", lambda e: self.close())
            self.choices.append(choice)
            if len(self.choices) == 1:
                tool(self.toolbar, "\ue8ab", app.t("\u4ea4\u6362\u8bed\u8a00", "Swap languages"), self.swap).pack(side="left", padx=2)
        tool(self.toolbar, "\ue8bb", app.t("\u5173\u95ed", "Close"), self.close).pack(side="right")
        tool(self.toolbar, "\ue8c8", app.t("\u590d\u5236\u8bd1\u6587", "Copy translation"), self.copy).pack(side="right", padx=2)
        tool(self.toolbar, "\ue72c", app.t("\u91cd\u65b0\u622a\u56fe", "Capture again"), self.retry).pack(side="right", padx=2)
        self.menu = tk.Menu(self.window, tearoff=False)
        self.menu.add_command(label=app.t("\u590d\u5236\u8bd1\u6587", "Copy translation"), command=self.copy)
        self.menu.add_command(label=app.t("\u590d\u5236\u539f\u6587", "Copy source"), command=lambda: self.copy(True))
        self.menu.add_separator()
        self.menu.add_command(label=app.t("\u5173\u95ed", "Close"), command=self.close)
        self.canvas.bind("<Button-3>", lambda e: self.menu.tk_popup(e.x_root, e.y_root))
        self.canvas.bind("<Double-Button-1>", lambda e: self.close())
        self.canvas.bind("<ButtonPress-1>", self.down)
        self.canvas.bind("<B1-Motion>", self.move)
        self.canvas.bind("<ButtonRelease-1>", lambda e: setattr(self, "drag", None))
        self.canvas.bind("<MouseWheel>", lambda e: self.canvas.yview_scroll(-int(e.delta / 120), "units"))
        self.window.bind("<Escape>", lambda e: self.close())
        self.window.bind("<Control-c>", lambda e: self.copy())
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self.font = fonts.Font(root=self.window, family="Microsoft YaHei UI", size=-14)
        self.status_id = None
        self.update_status(app.t("\u6b63\u5728\u8bc6\u522b\u2026", "Recognizing\u2026"))
        self.redraw()

    def show(self):
        vx, vy, vw, vh = desktop.virtual_screen()
        width = min(max(self.image.width + 18, 450), vw - 40)
        height = min(self.image.height + 66, vh - 80)
        self.window.geometry(f"{width}x{height}")
        self.window.deiconify()
        self.window.update_idletasks()
        desktop.position(self.window, max(vx, min(self.image.rect[0], vx + vw - width)),
                         max(vy, min(self.image.rect[1], vy + vh - height)))
        self.window.focus_force()

    def down(self, event):
        self.drag = (event.x_root, event.y_root, self.window.winfo_rootx(), self.window.winfo_rooty())

    def move(self, event):
        if self.drag:
            x, y, left, top = self.drag
            desktop.position(self.window, left + event.x_root - x, top + event.y_root - y)

    def update_status(self, text, busy=True):
        if self.closed:
            return
        self.status, self.loading = text, busy
        for choice in self.choices:
            choice.configure(state="disabled" if busy else "readonly")
        self.redraw()

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
        self.canvas.create_image(0, 0, anchor="nw", image=self.photo)
        offset = self.image.height + 8 if self.compare.get() else 0
        if offset:
            self.canvas.create_image(0, offset, anchor="nw", image=self.photo)
        if not self.loading and self.data:
            self.paint_blocks(offset)
        height = self.image.height + offset
        self.canvas.configure(scrollregion=(0, 0, self.image.width, height))
        if self.status:
            x = min(self.image.width / 2, 230)
            y = min(self.image.height / 2, 100)
            label = self.canvas.create_text(x, y, text=self.status, fill=self.app.theme.text,
                                            font=("Microsoft YaHei UI", 11), width=max(80, min(self.image.width - 16, 420)), justify="center")
            box = self.canvas.bbox(label)
            background = self.canvas.create_rectangle(box[0] - 9, box[1] - 6, box[2] + 9, box[3] + 6,
                                                       fill=self.app.theme.surface, outline=self.app.theme.border)
            self.canvas.tag_lower(background, label)

    def paint_blocks(self, offset):
        for block, text in zip(self.blocks, self.data["segments"]):
            if desktop.re_literal(block["text"]):
                continue
            x, y, width, height = block["box"]
            right, bottom = self.image.width - 2, self.image.height - 2
            for other in self.blocks:
                if other is block:
                    continue
                a, b, c, d = other["box"]
                if b < y + height and b + d > y and a >= x + width:
                    right = min(right, a - 5)
                if a < x + width and a + c > x and b >= y + height:
                    bottom = min(bottom, b - 3)
            # Expansion is local to a text block, never a whole-image reflow.
            cell_width = max(1, min(right - x, max(width, width * 1.6)))
            cell_height = max(1, min(bottom - y, max(height, height * 1.5)))
            background, foreground = self.image.backdrop(block["box"])
            self.canvas.create_rectangle(x, y + offset, x + width, y + height + offset, fill=background, outline="")
            size = min(28, max(9, int(block.get("glyphHeight", height) * .85)))
            item = self.canvas.create_text(x, y + offset, text=text, anchor="nw", fill=foreground,
                                           width=cell_width, font=self.font)
            def fits():
                box = self.canvas.bbox(item)
                return box and box[2] <= x + cell_width + 1 and box[3] <= y + offset + cell_height + 1
            while size > 8:
                self.font.configure(size=-size)
                self.canvas.itemconfigure(item, font=("Microsoft YaHei UI", -size))
                if fits():
                    break
                size -= 1
            if not fits():
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
        if self.data:
            self.app.copy(self.data["source" if source else "translated"])

    def retry(self):
        self.close()
        self.app.start_shot("region")

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.app.cancel_pin(self)
        self.canvas.delete("all")
        self.window.destroy()
        self.photo = self.image = self.font = None
        self.blocks, self.data = [], {}
        if self.app.pin is self:
            self.app.pin = None
        self.app.release_idle()


class Settings:
    def __init__(self, app):
        self.app, self.drafts, self.selected, self.entries = app, {}, None, {}
        self.window = tk.Toplevel(app.root)
        self.window.withdraw()
        self.window.title(app.t("\u622a\u56fe\u7ffb\u8bd1\u8bbe\u7f6e - Win\u987a", "Screenshot Translate Settings - WinShun"))
        self.window.attributes("-topmost", True)
        self.window.configure(bg=app.theme.page)
        self.window.geometry("920x520")
        self.window.minsize(740, 420)
        self.window.bind("<Escape>", lambda e: self.close())
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        ttk.Label(self.window, text=app.t("\u622a\u56fe\u7ffb\u8bd1", "Screenshot Translate"),
                  font=("Microsoft YaHei UI", 19), padding=(24, 20)).pack(anchor="w")
        self.channel = tk.StringVar(value=app.active_provider())
        self.tabs = ttk.Frame(self.window, padding=(24, 0))
        self.tabs.pack(fill="x")
        self.tab_buttons = []
        for provider, name in zip(PROVIDERS, NAMES):
            self.tab_buttons.append(ttk.Radiobutton(
                self.tabs, text=name, value=provider, variable=self.channel,
                style="Tab.TRadiobutton", command=self.select))
        self.tab_columns = 0
        self.tabs.bind("<Configure>", lambda event: self.arrange_tabs(event.width))
        self.arrange_tabs(920)
        self.content = ttk.Frame(self.window, padding=(24, 20))
        self.content.pack(fill="both", expand=True)
        self.content.columnconfigure(1, weight=1)
        footer = ttk.Frame(self.window, padding=(24, 16))
        footer.pack(fill="x", side="bottom")
        self.message = ttk.Label(footer, text="", wraplength=480)
        self.message.pack(side="left", fill="x")
        ttk.Button(footer, text=app.t("\u4fdd\u5b58", "Save"), command=self.save).pack(side="right")
        ttk.Button(footer, text=app.t("\u53d6\u6d88", "Cancel"), command=self.close).pack(side="right", padx=8)
        self.select()

    def arrange_tabs(self, width):
        cell = max(button.winfo_reqwidth() for button in self.tab_buttons) + 4
        columns = max(1, min(len(self.tab_buttons), int((width - 48) // cell)))
        if columns == self.tab_columns:
            return
        self.tab_columns = columns
        for column in range(len(self.tab_buttons)):
            self.tabs.columnconfigure(column, weight=1 if column < columns else 0,
                                      uniform="providers" if column < columns else "")
        for index, button in enumerate(self.tab_buttons):
            button.grid(row=index // columns, column=index % columns, sticky="ew", padx=2, pady=2)

    def show(self):
        self.window.deiconify()
        self.window.lift()
        self.window.focus_force()

    def remember(self):
        if self.selected and self.entries:
            self.drafts[self.selected].update({key: var.get() for key, var in self.entries.items()})

    def select(self):
        self.remember()
        provider = self.channel.get()
        self.selected, self.entries = provider, {}
        for child in self.content.winfo_children():
            child.destroy()
        if provider not in self.drafts:
            self.drafts[provider] = self.app.profiles.get(provider)
        draft = self.drafts[provider]
        if provider == "mymemory":
            ttk.Label(self.content, text=self.app.t("\u65e0\u9700\u5bc6\u94a5", "No API key required"), padding=(0, 10)).grid(row=0, column=0, sticky="w")
            text = (f"{self.app.usage.today():,} / 5,000 " +
                    self.app.t("\u5b57\u7b26\uff08\u672c\u63d2\u4ef6\u4eca\u65e5\u63d0\u4ea4\uff09", "characters submitted today by this plugin"))
            if not self.app.usage.available:
                text = self.app.t("\u7edf\u8ba1\u6682\u4e0d\u53ef\u7528", "Usage unavailable")
            ttk.Label(self.content, text=text, padding=(0, 10)).grid(row=1, column=0, columnspan=2, sticky="w")
            ttk.Label(self.content, text=self.app.t("\u672c\u673a\u65e5\u671f\u4f30\u7b97\uff0c\u975e\u670d\u52a1\u7aef\u4f59\u989d", "Local-date estimate, not the server balance"),
                      foreground=self.app.theme.muted).grid(row=2, column=0, columnspan=2, sticky="w")
            return
        fields = []
        if provider not in ("google", "edge"):
            fields.append(("apiKey", self.app.t("API \u5bc6\u94a5", "API key")))
        fields.append(("endpoint", self.app.t("\u670d\u52a1\u5730\u5740", "Endpoint")))
        if provider in PROVIDERS[6:]:
            fields.append(("model", self.app.t("\u6a21\u578b / \u63a5\u5165\u70b9 ID", "Model / Endpoint ID")))
        if provider == "microsoft":
            fields.append(("region", self.app.t("\u670d\u52a1\u533a\u57df", "Region")))
        for row, (key, label) in enumerate(fields):
            ttk.Label(self.content, text=label, padding=(0, 10)).grid(row=row, column=0, sticky="w", padx=(0, 24))
            var = tk.StringVar(value=draft.get(key, ""))
            self.entries[key] = var
            entry = ttk.Entry(self.content, textvariable=var, show="\u2022" if key == "apiKey" else "")
            entry.grid(row=row, column=1, sticky="ew", pady=6)
        ttk.Label(self.content, text=ENDPOINTS[provider], foreground=self.app.theme.muted, wraplength=550).grid(
            row=len(fields), column=1, sticky="w", pady=8)
        self.message.configure(text="")

    def save(self):
        self.remember()
        try:
            self.app.profiles.save(self.drafts)
            self.close()
        except (OSError, ValueError):
            self.message.configure(text=self.app.t("\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u5730\u5740\u548c\u6570\u636e\u76ee\u5f55", "Save failed. Check the endpoint and data directory."))

    def close(self):
        self.window.destroy()
        self.drafts.clear()
        self.entries.clear()
        if self.app.config is self:
            self.app.config = None
        self.app.release_idle()

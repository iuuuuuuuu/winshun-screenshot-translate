using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal static class Ui
{
    internal static bool Dark;
    private static readonly PrivateFontCollection fonts = new PrivateFontCollection();
    private static readonly FontFamily family = LoadFont();
    internal static readonly Font Body = new Font(family, 14, GraphicsUnit.Pixel);
    internal static readonly Font Setting = new Font(family, 16, GraphicsUnit.Pixel);
    internal static readonly Font Strong = new Font(family, 16, FontStyle.Bold, GraphicsUnit.Pixel);
    internal static readonly Font Caption = new Font(family, 14, GraphicsUnit.Pixel);
    internal static readonly Font Heading = new Font(family, 28, FontStyle.Bold, GraphicsUnit.Pixel);
    internal static readonly Font Icons = new Font("Segoe MDL2 Assets", 15, GraphicsUnit.Pixel);
    internal static readonly Font CaptionIcons = new Font("Segoe MDL2 Assets", 10, GraphicsUnit.Pixel);
    internal static Color Page { get { return Dark ? Color.FromArgb(32, 32, 32) : Color.FromArgb(243, 243, 243); } }
    internal static Color Surface { get { return Dark ? Color.FromArgb(43, 43, 43) : Color.FromArgb(251, 251, 251); } }
    internal static Color Input { get { return Dark ? Color.FromArgb(55, 55, 55) : Color.White; } }
    internal static Color Text { get { return Dark ? Color.FromArgb(243, 243, 243) : Color.FromArgb(27, 27, 27); } }
    internal static Color Muted { get { return Dark ? Color.FromArgb(184, 184, 184) : Color.FromArgb(102, 102, 102); } }
    internal static Color Line { get { return Dark ? Color.FromArgb(65, 65, 65) : Color.FromArgb(229, 229, 229); } }
    internal static Color InputLine { get { return Dark ? Color.FromArgb(80, 80, 80) : Color.FromArgb(213, 213, 213); } }
    internal static Color Hover { get { return Dark ? Color.FromArgb(57, 57, 57) : Color.FromArgb(237, 237, 237); } }
    internal static Color Accent { get { return Dark ? Color.FromArgb(96, 180, 245) : Color.FromArgb(0, 103, 192); } }
    internal static Color AccentFill { get { return Color.FromArgb(0, 103, 192); } }
    internal static Color Selected { get { return Dark ? Color.FromArgb(39, 62, 81) : Color.FromArgb(226, 238, 249); } }
    private static FontFamily LoadFont()
    {
        string path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            @"Programs\WinShun\fonts\AlibabaPuHuiTi-3-55-Regular.ttf");
        try
        {
            if (File.Exists(path))
            {
                fonts.AddFontFile(path);
                NativeUi.AddFontResourceEx(path, 0x10, IntPtr.Zero);
                return fonts.Families[0];
            }
        }
        catch (ArgumentException) { }
        catch (IOException) { }
        return new FontFamily("Microsoft YaHei UI");
    }
    internal static GraphicsPath Round(RectangleF r, float radius)
    {
        var path = new GraphicsPath();
        float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
        if (d <= 0) { path.AddRectangle(r); return path; }
        path.AddArc(r.Left, r.Top, d, d, 180, 90);
        path.AddArc(r.Right - d, r.Top, d, d, 270, 90);
        path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        path.AddArc(r.Left, r.Bottom - d, d, d, 90, 90);
        path.CloseFigure(); return path;
    }
    internal static void Box(Graphics g, RectangleF r, Color fill, Color border, float radius)
    {
        g.SmoothingMode = SmoothingMode.AntiAlias;
        using (var path = Round(r, radius))
        {
            using (var brush = new SolidBrush(fill)) g.FillPath(brush, path);
            if (border != Color.Transparent) using (var pen = new Pen(border)) g.DrawPath(pen, path);
        }
    }
    internal static void Label(Graphics g, string text, Font font, RectangleF box, Color color, bool center = false, bool wrap = false)
    {
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
        using (var brush = new SolidBrush(color))
        using (var format = new StringFormat { Alignment = center ? StringAlignment.Center : StringAlignment.Near,
            LineAlignment = StringAlignment.Center, Trimming = StringTrimming.EllipsisCharacter,
            FormatFlags = wrap ? 0 : StringFormatFlags.NoWrap })
            g.DrawString(text, font, brush, box, format);
    }
    internal static void Glyph(Graphics g, string glyph, RectangleF box, Color color) { Label(g, glyph, Icons, box, color, true); }
    internal static void Snapshot(Control control, Graphics g, Point origin)
    {
        using (var image = new Bitmap(control.Width, control.Height))
        {
            control.DrawToBitmap(image, new Rectangle(Point.Empty, control.Size));
            if (control is ChromeBar)
                using (Graphics local = Graphics.FromImage(image))
                    foreach (Control child in control.Controls) Snapshot(child, local, child.Location);
            g.DrawImageUnscaled(image, origin);
        }
    }
    internal static void Refresh(Control control)
    {
        var field = control as FluentField;
        if (field != null) field.RefreshTheme();
        foreach (Control child in control.Controls) Refresh(child);
        control.Invalidate();
    }
}

internal sealed class FluentButton : Control, IButtonControl
{
    internal string GlyphText = "";
    internal bool Primary, Destructive, CaptionButton;
    private bool hover, pressed;
    public DialogResult DialogResult { get; set; }
    internal FluentButton()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Font = Ui.Body; TabStop = true; AccessibleRole = AccessibleRole.PushButton;
        Cursor = Cursors.Hand; Height = 36; Width = 96;
    }
    public void NotifyDefault(bool value) { }
    public void PerformClick() { if (Enabled) OnClick(EventArgs.Empty); }
    protected override void OnClick(EventArgs e)
    {
        base.OnClick(e);
        if (DialogResult != DialogResult.None)
        {
            Form form = FindForm();
            if (form != null) { form.DialogResult = DialogResult; form.Close(); }
        }
    }
    protected override bool IsInputKey(Keys keyData) { return keyData == Keys.Space || base.IsInputKey(keyData); }
    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter) { PerformClick(); e.Handled = true; }
        base.OnKeyDown(e);
    }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = pressed = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e) { if (e.Button == MouseButtons.Left) { pressed = true; if (TabStop) Focus(); } Invalidate(); base.OnMouseDown(e); }
    protected override void OnMouseUp(MouseEventArgs e) { pressed = false; Invalidate(); base.OnMouseUp(e); }
    protected override void OnPaint(PaintEventArgs e)
    {
        Color fill = Primary ? Ui.AccentFill : hover ? Ui.Hover : BackColor;
        Color text = Primary ? Color.White : Ui.Text;
        if (hover && Destructive) { fill = Color.FromArgb(196, 43, 28); text = Color.White; }
        if (pressed) fill = Primary ? Color.FromArgb(0, 83, 156) : Ui.Hover;
        if (!Enabled) text = Ui.Muted;
        bool iconOnly = Text.Length == 0;
        Ui.Box(e.Graphics, new RectangleF(0.5f, 0.5f, Width - 1, Height - 1), fill,
            Primary || iconOnly ? Color.Transparent : Ui.InputLine, CaptionButton ? 0 : 4);
        if (GlyphText.Length > 0)
            Ui.Label(e.Graphics, GlyphText, CaptionButton ? Ui.CaptionIcons : Ui.Icons, new RectangleF(iconOnly ? 0 : 8, 0, iconOnly ? Width : 28, Height), text, true);
        if (!iconOnly) Ui.Label(e.Graphics, Text, Font, new RectangleF(GlyphText.Length > 0 ? 34 : 6, 0, Width - (GlyphText.Length > 0 ? 40 : 12), Height), text, true);
        if (Focused && ShowFocusCues) using (var pen = new Pen(Ui.Text)) e.Graphics.DrawRectangle(pen, 3, 3, Width - 7, Height - 7);
    }
}

internal sealed class FluentToggle : Control
{
    private bool hover;
    private bool check;
    internal event EventHandler CheckedChanged;
    internal bool Checked
    {
        get { return check; }
        set { if (check == value) return; check = value; Invalidate(); if (CheckedChanged != null) CheckedChanged(this, EventArgs.Empty); }
    }
    internal FluentToggle()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Font = Ui.Body; Cursor = Cursors.Hand; Size = new Size(90, 34); TabStop = true; AccessibleRole = AccessibleRole.CheckButton;
    }
    protected override void OnClick(EventArgs e) { Checked = !Checked; base.OnClick(e); }
    protected override bool IsInputKey(Keys keyData) { return keyData == Keys.Space || base.IsInputKey(keyData); }
    protected override void OnKeyDown(KeyEventArgs e) { if (e.KeyCode == Keys.Space) { Checked = !Checked; e.Handled = true; } base.OnKeyDown(e); }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnPaint(PaintEventArgs e)
    {
        Ui.Box(e.Graphics, new RectangleF(0, 0, Width - 1, Height - 1), Checked ? Ui.Selected : hover ? Ui.Hover : BackColor, Color.Transparent, 4);
        Color color = Checked ? Ui.Accent : Ui.Text;
        Ui.Glyph(e.Graphics, "\uE8A9", new RectangleF(4, 0, 26, Height), color);
        Ui.Label(e.Graphics, Text, Font, new RectangleF(30, 0, Width - 34, Height), color, true);
        if (Focused && ShowFocusCues) using (var pen = new Pen(Ui.Accent)) e.Graphics.DrawRectangle(pen, 2, 2, Width - 5, Height - 5);
    }
}

internal sealed class PageTabs : Control
{
    internal string[] Labels;
    internal event EventHandler SelectedIndexChanged;
    private int current, hover = -1, offset;
    internal int SelectedIndex
    {
        get { return current; }
        set { if (current == value) return; current = value; RevealSelected(); Invalidate(); if (SelectedIndexChanged != null) SelectedIndexChanged(this, EventArgs.Empty); }
    }
    internal PageTabs(string[] labels)
    {
        Labels = labels; Height = 46; Font = Ui.Setting; Cursor = Cursors.Hand; TabStop = true;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    }
    internal Rectangle TabBounds(int index)
    {
        int x = -offset;
        using (Graphics g = CreateGraphics())
            for (int i = 0; i < Labels.Length; i++)
            {
                int width = (int)Math.Ceiling(g.MeasureString(Labels[i], Ui.Strong).Width) + 20;
                if (i == index) return new Rectangle(x, 0, width, Height);
                x += width;
            }
        return Rectangle.Empty;
    }
    private int TotalWidth { get { return Labels.Length == 0 ? 0 : TabBounds(Labels.Length - 1).Right + offset; } }
    private int ViewWidth { get { return TotalWidth > Width ? Math.Max(1, Width - 56) : Width; } }
    private void RevealSelected()
    {
        Rectangle box = TabBounds(current);
        if (box.Left < 0) offset = Math.Max(0, offset + box.Left);
        else if (box.Right > ViewWidth) offset += box.Right - ViewWidth;
        offset = Math.Max(0, Math.Min(offset, Math.Max(0, TotalWidth - ViewWidth)));
    }
    protected override void OnSizeChanged(EventArgs e) { base.OnSizeChanged(e); if (Labels != null) { RevealSelected(); Invalidate(); } }
    protected override void OnPaint(PaintEventArgs e)
    {
        int viewport = ViewWidth;
        var state = e.Graphics.Save();
        e.Graphics.SetClip(new Rectangle(0, 0, viewport, Height));
        for (int i = 0; i < Labels.Length; i++)
        {
            Rectangle box = TabBounds(i);
            Ui.Label(e.Graphics, Labels[i], i == current ? Ui.Strong : Ui.Setting,
                new RectangleF(box.X + 4, 0, box.Width - 8, Height - 9), i == current || i == hover ? Ui.Text : Ui.Muted, true);
            if (i == current) Ui.Box(e.Graphics, new RectangleF(box.X + (box.Width - 16) / 2, Height - 5, 16, 3), Ui.Accent, Color.Transparent, 1.5f);
        }
        e.Graphics.Restore(state);
        if (viewport < Width)
        {
            Ui.Glyph(e.Graphics, "\uE76B", new RectangleF(Width - 56, 0, 28, Height - 9), offset > 0 ? Ui.Text : Ui.Line);
            Ui.Glyph(e.Graphics, "\uE76C", new RectangleF(Width - 28, 0, 28, Height - 9), offset + viewport < TotalWidth ? Ui.Text : Ui.Line);
        }
    }
    private int Hit(Point point) { if (point.X >= ViewWidth) return -1; for (int i = 0; i < Labels.Length; i++) if (TabBounds(i).Contains(point)) return i; return -1; }
    protected override void OnMouseMove(MouseEventArgs e) { int next = Hit(e.Location); if (next != hover) { hover = next; Invalidate(); } base.OnMouseMove(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = -1; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left)
        {
            if (e.X >= ViewWidth && ViewWidth < Width)
            {
                offset = Math.Max(0, Math.Min(TotalWidth - ViewWidth, offset + (e.X < Width - 28 ? -160 : 160)));
                Invalidate();
            }
            else { int index = Hit(e.Location); if (index >= 0) { Focus(); SelectedIndex = index; } }
        }
        base.OnMouseDown(e);
    }
    protected override void OnMouseWheel(MouseEventArgs e)
    {
        offset = Math.Max(0, Math.Min(Math.Max(0, TotalWidth - ViewWidth), offset - Math.Sign(e.Delta) * 160));
        Invalidate(); base.OnMouseWheel(e);
    }
    protected override bool IsInputKey(Keys keyData) { return keyData == Keys.Left || keyData == Keys.Right || base.IsInputKey(keyData); }
    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Left) { SelectedIndex = Math.Max(0, current - 1); e.Handled = true; }
        if (e.KeyCode == Keys.Right) { SelectedIndex = Math.Min(Labels.Length - 1, current + 1); e.Handled = true; }
        base.OnKeyDown(e);
    }
}

internal sealed class ServiceTabs : Control
{
    private readonly string[] labels;
    private Rectangle[] boxes;
    private int current, hover = -1;
    internal event EventHandler SelectedIndexChanged;
    internal int SelectedIndex
    {
        get { return current; }
        set
        {
            if (value < 0 || value >= labels.Length) throw new ArgumentOutOfRangeException("value");
            if (current == value) return;
            current = value; Invalidate();
            if (SelectedIndexChanged != null) SelectedIndexChanged(this, EventArgs.Empty);
        }
    }
    internal ServiceTabs(string[] labels)
    {
        this.labels = labels;
        Font = Ui.Setting; Cursor = Cursors.Hand; TabStop = true; AccessibleRole = AccessibleRole.PageTabList;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Arrange();
    }
    private void Arrange()
    {
        if (labels == null) return;
        boxes = new Rectangle[labels.Length];
        int x = 3, y = 3;
        using (Graphics g = CreateGraphics())
            for (int i = 0; i < labels.Length; i++)
            {
                int width = (int)Math.Ceiling(g.MeasureString(labels[i], Ui.Strong, Int32.MaxValue, StringFormat.GenericTypographic).Width) + 24;
                if (x > 3 && x + width > Width - 3) { x = 3; y += 36; }
                boxes[i] = new Rectangle(x, y, width, 32);
                x += width + 2;
            }
        Height = y + 35;
        Invalidate();
    }
    internal Rectangle TabBounds(int index) { return boxes[index]; }
    protected override void OnSizeChanged(EventArgs e) { base.OnSizeChanged(e); Arrange(); }
    protected override void OnPaint(PaintEventArgs e)
    {
        Ui.Box(e.Graphics, new RectangleF(0, 0, Width, Height), Ui.Dark ? Color.FromArgb(42, 42, 42) : Color.FromArgb(237, 237, 237), Color.Transparent, 8);
        for (int i = 0; i < labels.Length; i++)
        {
            Rectangle box = boxes[i];
            if (i == current || i == hover)
                Ui.Box(e.Graphics, new RectangleF(box.X + .5f, box.Y + .5f, box.Width - 1, box.Height - 1),
                    i == current ? Ui.Dark ? Color.FromArgb(58, 58, 58) : Color.White : Ui.Dark ? Color.FromArgb(51, 51, 51) : Color.FromArgb(228, 228, 228),
                    i == current ? Ui.Dark ? Color.FromArgb(69, 69, 69) : Color.FromArgb(220, 220, 220) : Color.Transparent, 6);
            Ui.Label(e.Graphics, labels[i], i == current ? Ui.Strong : Ui.Setting, box, i == current ? Ui.Text : Ui.Muted, true);
            if (i == current && Focused && ShowFocusCues)
                using (var pen = new Pen(Ui.Accent)) e.Graphics.DrawRectangle(pen, box.X + 2, box.Y + 2, box.Width - 5, box.Height - 5);
        }
    }
    private int Hit(Point point) { for (int i = 0; i < boxes.Length; i++) if (boxes[i].Contains(point)) return i; return -1; }
    protected override void OnMouseMove(MouseEventArgs e) { int index = Hit(e.Location); if (hover != index) { hover = index; Invalidate(); } base.OnMouseMove(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = -1; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnMouseDown(MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left) { int index = Hit(e.Location); if (index >= 0) { Focus(); SelectedIndex = index; } }
        base.OnMouseDown(e);
    }
    protected override bool IsInputKey(Keys data) { return data == Keys.Left || data == Keys.Right || data == Keys.Home || data == Keys.End || base.IsInputKey(data); }
    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Left) SelectedIndex = Math.Max(0, current - 1);
        else if (e.KeyCode == Keys.Right) SelectedIndex = Math.Min(labels.Length - 1, current + 1);
        else if (e.KeyCode == Keys.Home) SelectedIndex = 0;
        else if (e.KeyCode == Keys.End) SelectedIndex = labels.Length - 1;
        else { base.OnKeyDown(e); return; }
        e.Handled = true;
    }
}

internal sealed class FluentField : Control
{
    internal readonly TextBox Editor;
    private readonly FluentButton reveal;
    private readonly ToolTip tips = new ToolTip();
    private string cue = "";
    private bool secret;
    internal string Value { get { return Editor.Text; } set { Editor.Text = value; } }
    internal string Cue { get { return cue; } set { cue = value; ApplyCue(); } }
    internal bool Secret
    {
        get { return secret; }
        set { secret = value; reveal.Visible = value; Editor.UseSystemPasswordChar = value; LayoutEditor(); }
    }
    internal FluentField()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Height = 38; Width = 320; TabStop = false;
        Editor = new TextBox { BorderStyle = BorderStyle.None, Font = Ui.Setting, BackColor = Ui.Input, ForeColor = Ui.Text };
        reveal = new FluentButton { GlyphText = "\uE890", Text = "", Width = 32, Height = 30, Visible = false, BackColor = Ui.Input, TabStop = false, AccessibleName = Program.T("显示密钥", "Show key") };
        reveal.Click += delegate { Editor.UseSystemPasswordChar = !Editor.UseSystemPasswordChar; Editor.Focus(); };
        tips.SetToolTip(reveal, Program.T("显示或隐藏密钥", "Show or hide key"));
        Controls.Add(Editor); Controls.Add(reveal);
        Editor.GotFocus += delegate { Invalidate(); }; Editor.LostFocus += delegate { Invalidate(); };
        Editor.HandleCreated += delegate { ApplyCue(); };
        LayoutEditor();
    }
    internal void RefreshTheme() { Editor.BackColor = reveal.BackColor = Ui.Input; Editor.ForeColor = Ui.Text; Invalidate(); }
    private void ApplyCue() { if (Editor.IsHandleCreated) NativeUi.SendMessage(Editor.Handle, 0x1501, new IntPtr(1), cue); }
    private void LayoutEditor()
    {
        if (Editor == null) return;
        int extra = secret ? 38 : 0;
        Editor.SetBounds(11, (Height - Editor.PreferredHeight) / 2, Math.Max(20, Width - 22 - extra), Editor.PreferredHeight);
        if (reveal != null) reveal.SetBounds(Width - 36, 4, 30, Height - 8);
    }
    protected override void OnSizeChanged(EventArgs e) { base.OnSizeChanged(e); LayoutEditor(); }
    protected override void OnPaint(PaintEventArgs e)
    {
        Ui.Box(e.Graphics, new RectangleF(0.5f, 0.5f, Width - 1, Height - 1), Ui.Input, Ui.InputLine, 4);
        using (var pen = new Pen(Editor.Focused ? Ui.Accent : Ui.Muted, Editor.Focused ? 2 : 1))
            e.Graphics.DrawLine(pen, 4, Height - 1.5f, Width - 5, Height - 1.5f);
    }
    internal void Snapshot(Graphics g, Point origin)
    {
        using (var image = new Bitmap(Width, Height))
        using (Graphics local = Graphics.FromImage(image))
        {
            OnPaint(new PaintEventArgs(local, ClientRectangle));
            string text = Editor.UseSystemPasswordChar ? new string('\u2022', Editor.Text.Length) : Editor.Text;
            Ui.Label(local, text.Length > 0 ? text : cue, Ui.Setting,
                new RectangleF(11, 0, Width - (secret ? 50 : 22), Height), text.Length > 0 ? Ui.Text : Ui.Muted);
            if (secret) Ui.Glyph(local, "\uE890", reveal.Bounds, Ui.Muted);
            g.DrawImageUnscaled(image, origin);
        }
    }
    protected override void Dispose(bool disposing) { if (disposing) tips.Dispose(); base.Dispose(disposing); }
}

internal sealed class SettingRow : Control
{
    internal string Title = "", Description = "", Value = "";
    internal FluentField Field;
    internal SettingRow(string title, string description)
    {
        Title = title; Description = description; Height = 88; BackColor = Ui.Surface;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    }
    internal void Attach(FluentField field) { Field = field; Controls.Add(field); LayoutField(); }
    private void LayoutField()
    {
        if (Field == null) return;
        int width = Math.Min(360, Math.Max(280, Width / 2));
        Field.SetBounds(Width - width - 18, (Height - Field.Height) / 2, width, 38);
    }
    protected override void OnSizeChanged(EventArgs e) { base.OnSizeChanged(e); LayoutField(); }
    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(Ui.Surface);
        if (Top > 1) using (var pen = new Pen(Ui.Line)) e.Graphics.DrawLine(pen, 0, 0, Width, 0);
        int right = Field != null ? Field.Left - 20 : Width / 2;
        Ui.Label(e.Graphics, Title, Ui.Setting, new RectangleF(18, Description.Length > 0 ? 14 : 0, right - 18, Description.Length > 0 ? 26 : Height), Ui.Text);
        if (Description.Length > 0)
            Ui.Label(e.Graphics, Description, Ui.Caption, new RectangleF(18, 41, right - 18, Height - 48), Ui.Muted, false, true);
        if (Field == null) Ui.Label(e.Graphics, Value, Ui.Setting, new RectangleF(Width / 2, 0, Width / 2 - 18, Height), Ui.Muted);
    }
    internal void Snapshot(Graphics g, Point origin)
    {
        using (var image = new Bitmap(Width, Height))
        using (Graphics local = Graphics.FromImage(image))
        {
            OnPaint(new PaintEventArgs(local, ClientRectangle));
            if (Field != null) Field.Snapshot(local, Field.Location);
            g.DrawImageUnscaled(image, origin);
        }
    }
}

internal sealed class SettingsGroup : Control
{
    internal SettingsGroup() { SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true); }
    internal void Arrange()
    {
        int y = 1;
        foreach (Control row in Controls) { row.SetBounds(1, y, Width - 2, row.Height); y += row.Height; }
        Height = y + 1;
    }
    protected override void OnSizeChanged(EventArgs e)
    {
        base.OnSizeChanged(e);
        if (Width > 0 && Height > 0)
        {
            Region old = Region;
            using (var path = Ui.Round(new RectangleF(0, 0, Width, Height), 6)) Region = new Region(path);
            if (old != null) old.Dispose();
        }
        foreach (Control row in Controls) row.Width = Math.Max(1, Width - 2);
    }
    protected override void OnPaint(PaintEventArgs e) { Ui.Box(e.Graphics, new RectangleF(.5f, .5f, Width - 1, Height - 1), Ui.Surface, Ui.Line, 6); }
    internal void Snapshot(Graphics g, Point origin)
    {
        using (var image = new Bitmap(Width, Height))
        using (Graphics local = Graphics.FromImage(image))
        {
            foreach (SettingRow row in Controls) row.Snapshot(local, row.Location);
            Ui.Box(local, new RectangleF(.5f, .5f, Width - 1, Height - 1), Color.Transparent, Ui.Line, 6);
            g.DrawImageUnscaled(image, origin);
        }
    }
}

internal sealed class LanguagePicker : Control
{
    internal readonly List<object> Items = new List<object>();
    internal event EventHandler SelectedIndexChanged;
    private int index = -1;
    private bool hover;
    private ContextMenuStrip menu;
    internal int SelectedIndex
    {
        get { return index; }
        set { if (index == value) return; index = value; Invalidate(); if (SelectedIndexChanged != null) SelectedIndexChanged(this, EventArgs.Empty); }
    }
    internal object SelectedItem { get { return index >= 0 && index < Items.Count ? Items[index] : null; } }
    internal LanguagePicker()
    {
        Font = Ui.Body; Size = new Size(130, 34); TabStop = true; Cursor = Cursors.Hand; AccessibleRole = AccessibleRole.ComboBox;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnPaint(PaintEventArgs e)
    {
        Ui.Box(e.Graphics, new RectangleF(0, 0, Width - 1, Height - 1), hover && Enabled ? Ui.Hover : BackColor, Color.Transparent, 4);
        Ui.Label(e.Graphics, Convert.ToString(SelectedItem), Font, new RectangleF(8, 0, Width - 28, Height), Enabled ? Ui.Text : Ui.Muted);
        Ui.Glyph(e.Graphics, "\uE70D", new RectangleF(Width - 24, 0, 22, Height), Ui.Muted);
    }
    private void OpenMenu()
    {
        if (!Enabled) return;
        if (menu != null) menu.Dispose();
        menu = FluentMenu.Create();
        for (int i = 0; i < Items.Count; i++)
        {
            int target = i;
            var item = new ToolStripMenuItem(Convert.ToString(Items[i])) { Checked = i == index, Height = 34 };
            item.Click += delegate { SelectedIndex = target; };
            menu.Items.Add(item);
        }
        menu.Show(this, new Point(0, Height + 4));
    }
    protected override void OnClick(EventArgs e) { base.OnClick(e); OpenMenu(); }
    protected override void OnKeyDown(KeyEventArgs e) { if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space || e.KeyCode == Keys.Down) { e.Handled = true; OpenMenu(); } base.OnKeyDown(e); }
    protected override void Dispose(bool disposing) { if (disposing && menu != null) menu.Dispose(); base.Dispose(disposing); }
}

internal sealed class FluentMenu : ToolStripProfessionalRenderer
{
    internal static ContextMenuStrip Create()
    {
        return new ContextMenuStrip { Renderer = new FluentMenu(), Font = Ui.Body, BackColor = Ui.Surface, ForeColor = Ui.Text, Padding = new Padding(4), ShowImageMargin = true };
    }
    protected override void OnRenderToolStripBackground(ToolStripRenderEventArgs e) { e.Graphics.Clear(Ui.Surface); }
    protected override void OnRenderToolStripBorder(ToolStripRenderEventArgs e) { Ui.Box(e.Graphics, new RectangleF(.5f, .5f, e.ToolStrip.Width - 1, e.ToolStrip.Height - 1), Color.Transparent, Ui.InputLine, 6); }
    protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs e)
    {
        if (e.Item.Selected) Ui.Box(e.Graphics, new RectangleF(2, 1, e.Item.Width - 4, e.Item.Height - 2), Ui.Hover, Color.Transparent, 4);
    }
    protected override void OnRenderImageMargin(ToolStripRenderEventArgs e) { }
    protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e) { e.TextColor = Ui.Text; base.OnRenderItemText(e); }
}

internal class SurfaceWindow : Form
{
    internal SurfaceWindow()
    {
        FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; AutoScaleMode = AutoScaleMode.None;
        BackColor = Ui.Page; Font = Ui.Body; DoubleBuffered = true;
    }
    protected override CreateParams CreateParams { get { CreateParams p = base.CreateParams; p.ClassStyle |= 0x20000; return p; } }
    protected override void OnHandleCreated(EventArgs e) { base.OnHandleCreated(e); NativeUi.Decorate(Handle); }
    internal virtual void ApplyTheme() { BackColor = Ui.Page; Ui.Refresh(this); if (IsHandleCreated) NativeUi.Decorate(Handle); }
    protected override void OnPaint(PaintEventArgs e) { base.OnPaint(e); Ui.Box(e.Graphics, new RectangleF(.5f, .5f, Width - 1, Height - 1), Color.Transparent, Ui.InputLine, 6); }
}

internal sealed class ChromeBar : Control
{
    private readonly Form window;
    internal string Caption;
    internal ChromeBar(Form form, string caption)
    {
        window = form; Caption = caption; Height = 48; Dock = DockStyle.Top;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        var close = new FluentButton { Text = "", GlyphText = "\uE8BB", CaptionButton = true, Width = 46, Height = 48, Anchor = AnchorStyles.Top | AnchorStyles.Right, Destructive = true, BackColor = Ui.Page, AccessibleName = Program.T("关闭", "Close") };
        var maximize = new FluentButton { Text = "", GlyphText = "\uE922", CaptionButton = true, Width = 46, Height = 48, Anchor = AnchorStyles.Top | AnchorStyles.Right, BackColor = Ui.Page, AccessibleName = Program.T("最大化", "Maximize") };
        var minimize = new FluentButton { Text = "", GlyphText = "\uE921", CaptionButton = true, Width = 46, Height = 48, Anchor = AnchorStyles.Top | AnchorStyles.Right, BackColor = Ui.Page, AccessibleName = Program.T("最小化", "Minimize") };
        Controls.Add(minimize); Controls.Add(maximize); Controls.Add(close);
        close.Left = Width - 46; maximize.Left = Width - 92; minimize.Left = Width - 138;
        close.Click += delegate { window.Close(); }; minimize.Click += delegate { window.WindowState = FormWindowState.Minimized; };
        maximize.Click += delegate { window.WindowState = window.WindowState == FormWindowState.Maximized ? FormWindowState.Normal : FormWindowState.Maximized; };
        window.SizeChanged += delegate { maximize.GlyphText = window.WindowState == FormWindowState.Maximized ? "\uE923" : "\uE922"; maximize.Invalidate(); };
        MouseDown += delegate(object sender, MouseEventArgs e) { if (e.Button == MouseButtons.Left && window.IsHandleCreated) NativeUi.Drag(window.Handle); };
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(Ui.Page);
        Ui.Glyph(e.Graphics, "\uE8C1", new RectangleF(16, 0, 16, Height), Ui.Accent);
        Ui.Label(e.Graphics, Caption, Ui.Caption, new RectangleF(48, 0, Width - 198, Height), Ui.Text);
    }
}

internal sealed class FloatingToolbar : SurfaceWindow
{
    internal FloatingToolbar()
    {
        TopMost = true; StartPosition = FormStartPosition.Manual; ClientSize = new Size(488, 46); BackColor = Ui.Surface;
        SetStyle(ControlStyles.StandardClick | ControlStyles.StandardDoubleClick, true);
    }
    protected override bool ShowWithoutActivation { get { return true; } }
    internal override void ApplyTheme()
    {
        BackColor = Ui.Surface;
        foreach (Control control in Controls) control.BackColor = Ui.Surface;
        Ui.Refresh(this); if (IsHandleCreated) NativeUi.Decorate(Handle);
    }
}

internal static class NativeUi
{
    [StructLayout(LayoutKind.Sequential)] private struct Margins { internal int Left, Right, Top, Bottom; }
    [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);
    [DllImport("dwmapi.dll")] private static extern int DwmExtendFrameIntoClientArea(IntPtr window, ref Margins margins);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] internal static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wParam, string lParam);
    [DllImport("user32.dll")] private static extern bool ReleaseCapture();
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] internal static extern int AddFontResourceEx(string path, uint flags, IntPtr reserved);
    [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
    internal static IntPtr OwnerOf(IntPtr window) { return GetWindow(window, 4); }
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] private static extern IntPtr SetWindowLongPtr(IntPtr window, int index, IntPtr value);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongW")] private static extern IntPtr SetWindowLong(IntPtr window, int index, IntPtr value);
    internal static void SetOwner(IntPtr window, IntPtr owner)
    {
        if (owner == IntPtr.Zero || !IsWindow(owner)) return;
        if (IntPtr.Size == 8) SetWindowLongPtr(window, -8, owner);
        else SetWindowLong(window, -8, owner);
    }
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wParam, IntPtr lParam);
    internal static void Decorate(IntPtr window)
    {
        try
        {
            int round = 2, dark = Ui.Dark ? 1 : 0;
            DwmSetWindowAttribute(window, 33, ref round, 4);
            DwmSetWindowAttribute(window, 20, ref dark, 4);
            var margins = new Margins { Left = 1, Right = 1, Top = 1, Bottom = 1 };
            DwmExtendFrameIntoClientArea(window, ref margins);
        }
        catch (DllNotFoundException) { }
    }
    internal static void Drag(IntPtr window) { ReleaseCapture(); SendMessage(window, 0xA1, new IntPtr(2), IntPtr.Zero); }
}

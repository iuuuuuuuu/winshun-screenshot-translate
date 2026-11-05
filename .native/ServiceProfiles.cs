// Historical native source, not part of the community runtime.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Dict = System.Collections.Generic.Dictionary<string, object>;

internal sealed class ServiceProfile
{
    internal string Provider = "mymemory", ApiKey = "", Endpoint = "", Model = "", Region = "", Proxy = "";
    internal ServiceProfile Clone() { return (ServiceProfile)MemberwiseClone(); }
    internal static ServiceProfile FromSettings(Dict settings)
    {
        return new ServiceProfile {
            Provider = SelectedProvider(settings), ApiKey = Read(settings, "apiKey", ""),
            Endpoint = Read(settings, "endpoint", ""), Model = Read(settings, "model", ""), Region = Read(settings, "region", ""), Proxy = Read(settings, "proxy", "")
        };
    }
    internal static string SelectedProvider(Dict settings)
    {
        string provider = Read(settings, "provider", "mymemory");
        if (provider == "ai")
        {
            string modelProvider = Read(settings, "aiProvider", "openai");
            return new[] { "openai", "deepseek", "doubao", "hunyuan" }.Contains(modelProvider) ? modelProvider : "openai";
        }
        return ServiceProfiles.Providers.Contains(provider) ? provider : "mymemory";
    }
    internal static string Read(Dict map, string key, string fallback)
    {
        object value;
        return map != null && map.TryGetValue(key, out value) && value != null ? Convert.ToString(value).Trim() : fallback;
    }
}

internal sealed class ServiceProfiles
{
    internal static readonly string[] Providers = { "mymemory", "google", "edge", "zvo", "microsoft", "deepl", "openai", "deepseek", "doubao", "hunyuan" };
    internal static readonly string[] Names = { "MyMemory", "Google", "Edge", "translate.js", "微软", "DeepL", "OpenAI", "DeepSeek", "豆包", "混元" };
    internal static readonly string[] EnglishNames = { "MyMemory", "Google", "Edge", "translate.js", "Microsoft", "DeepL", "OpenAI", "DeepSeek", "Doubao", "Hunyuan" };
    private readonly object gate = new object();
    private readonly string file;
    private readonly Dictionary<string, Dict> stored = new Dictionary<string, Dict>();
    private static readonly byte[] Entropy = Encoding.UTF8.GetBytes("iuuuuuuuu.screenshot-translate/profiles/v1");
    private string active, sharedProxy = "";
    private bool saved;
    internal ServiceProfiles(string directory, Dict legacy)
    {
        file = Path.Combine(directory, "services.json");
        if (File.Exists(file))
        {
            saved = true;
            var json = new JavaScriptSerializer();
            Dict document = json.DeserializeObject(File.ReadAllText(file, Encoding.UTF8)) as Dict;
            if (document == null || !(document["profiles"] is Dict)) throw new InvalidDataException("Invalid translation profiles; services.json was preserved");
            active = ServiceProfile.Read(document, "active", "mymemory");
            foreach (var item in (Dict)document["profiles"])
                if (Providers.Contains(item.Key) && item.Value is Dict) stored[item.Key] = (Dict)item.Value;
        }
        else
        {
            ServiceProfile previous = ServiceProfile.FromSettings(legacy);
            active = Providers.Contains(previous.Provider) ? previous.Provider : "mymemory";
            previous.Provider = active; stored[active] = Encode(previous);
        }
        if (!Providers.Contains(active)) active = "mymemory";
        ApplySettings(legacy);
    }
    internal string Active { get { lock (gate) return active; } }
    internal ServiceProfile Current()
    {
        lock (gate)
        {
            ServiceProfile profile = Get(active);
            profile.Proxy = sharedProxy;
            return profile;
        }
    }
    internal void ApplySettings(Dict values)
    {
        lock (gate)
        {
            if (values == null) return;
            // The host owns channel selection and the common proxy, even after profile saves.
            if (values.ContainsKey("provider"))
            {
                active = ServiceProfile.SelectedProvider(values);
            }
            sharedProxy = ServiceProfile.Read(values, "proxy", "");
            if (!saved && new[] { "apiKey", "endpoint", "model", "region" }.Any(values.ContainsKey))
            {
                ServiceProfile previous = ServiceProfile.FromSettings(values);
                previous.Provider = active; stored[active] = Encode(previous);
            }
        }
    }
    internal ServiceProfile Get(string provider)
    {
        lock (gate)
        {
            Dict data;
            if (!stored.TryGetValue(provider, out data)) return new ServiceProfile { Provider = provider };
            string encrypted = ServiceProfile.Read(data, "secret", "");
            string key = "";
            if (encrypted.Length > 0)
                key = Encoding.UTF8.GetString(ProtectedData.Unprotect(Convert.FromBase64String(encrypted), Entropy, DataProtectionScope.CurrentUser));
            return new ServiceProfile {
                Provider = provider, ApiKey = key, Endpoint = ServiceProfile.Read(data, "endpoint", ""),
                Model = ServiceProfile.Read(data, "model", ""), Region = ServiceProfile.Read(data, "region", "")
            };
        }
    }
    private static Dict Encode(ServiceProfile profile)
    {
        string secret = "";
        if (profile.ApiKey.Length > 0)
            secret = Convert.ToBase64String(ProtectedData.Protect(Encoding.UTF8.GetBytes(profile.ApiKey), Entropy, DataProtectionScope.CurrentUser));
        return Program.Obj("secret", secret, "endpoint", profile.Endpoint, "model", profile.Model, "region", profile.Region);
    }
    internal void Save(IEnumerable<ServiceProfile> values)
    {
        var next = new Dictionary<string, Dict>();
        foreach (ServiceProfile value in values)
        {
            if (!Providers.Contains(value.Provider)) throw new ArgumentException("Unknown translation service");
            next[value.Provider] = Encode(value);
        }
        lock (gate)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(file));
            string temporary = file + ".tmp";
            try
            {
                File.WriteAllText(temporary, new JavaScriptSerializer().Serialize(Program.Obj("version", 2, "profiles", next)), new UTF8Encoding(false));
                if (File.Exists(file)) File.Replace(temporary, file, null);
                else File.Move(temporary, file);
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
            stored.Clear();
            foreach (var value in next) stored[value.Key] = value.Value;
            saved = true;
        }
    }
    internal static void Verify(string directory, string output)
    {
        var profiles = new ServiceProfiles(directory, Program.Obj("provider", "mymemory"));
        var first = new ServiceProfile { Provider = "openai", ApiKey = "test-secret-one", Endpoint = "https://example.com/v1", Model = "model-one" };
        var second = new ServiceProfile { Provider = "deepseek", ApiKey = "test-secret-two", Model = "model-two", Proxy = "http://127.0.0.1:7890" };
        profiles.Save(new[] { first, second });
        if (profiles.Active != "mymemory") throw new Exception("Profile save changed the host channel");
        var loaded = new ServiceProfiles(directory, Program.Obj("provider", "deepseek", "proxy", "http://127.0.0.1:7890"));
        if (loaded.Active != "deepseek" || loaded.Get("openai").ApiKey != first.ApiKey ||
            loaded.Get("deepseek").Model != second.Model || loaded.Current().Proxy != second.Proxy || loaded.Get("microsoft").ApiKey.Length != 0)
            throw new Exception("Service profile isolation failed");
        string raw = File.ReadAllText(Path.Combine(directory, "services.json"));
        if (raw.Contains(first.ApiKey) || raw.Contains(second.ApiKey)) throw new Exception("Plaintext API key found");
        if (raw.Contains("\"active\"") || raw.Contains("\"proxy\"")) throw new Exception("Common settings were stored in provider profiles");
        loaded.ApplySettings(Program.Obj("provider", "openai", "proxy", "http://127.0.0.1:8080", "apiKey", "wrong-legacy-key"));
        if (loaded.Current().Provider != "openai" || loaded.Current().ApiKey != first.ApiKey ||
            loaded.Current().Proxy != "http://127.0.0.1:8080") throw new Exception("Host channel or common proxy update failed");
        loaded.ApplySettings(Program.Obj("provider", "mymemory", "proxy", ""));
        if (loaded.Current().Provider != "mymemory" || loaded.Current().Proxy.Length != 0) throw new Exception("Common proxy did not clear");
        loaded.ApplySettings(Program.Obj("provider", "deepseek"));
        if (loaded.Current().ApiKey != second.ApiKey) throw new Exception("Host selection damaged a saved key");
        loaded.ApplySettings(Program.Obj("provider", "ai", "aiProvider", "deepseek"));
        if (loaded.Active != "deepseek" || loaded.Current().ApiKey != second.ApiKey) throw new Exception("Flat model provider selection failed");
        loaded.ApplySettings(Program.Obj("provider", "google", "aiProvider", "openai"));
        if (loaded.Active != "google") throw new Exception("Inactive model selection changed the main channel");
        loaded.ApplySettings(Program.Obj("provider", "deepseek"));
        var legacyNone = new ServiceProfiles(Path.Combine(directory, "legacy"), Program.Obj("provider", "none"));
        if (legacyNone.Active != "mymemory" || Providers.Contains("none")) throw new Exception("Removed service migration failed");
        File.WriteAllText(Path.Combine(directory, "services.json"), new JavaScriptSerializer().Serialize(Program.Obj("active", "none", "profiles", new Dict())));
        if (new ServiceProfiles(directory, null).Active != "mymemory") throw new Exception("Saved disabled service migration failed");
        File.WriteAllText(Path.Combine(directory, "services.json"), new JavaScriptSerializer().Serialize(Program.Obj("version", 1, "active", "google",
            "profiles", Program.Obj("openai", Encode(first), "google", Program.Obj("proxy", "http://127.0.0.1:1111")))));
        var migrated = new ServiceProfiles(directory, Program.Obj("provider", "openai", "proxy", "http://127.0.0.1:2222"));
        if (migrated.Current().ApiKey != first.ApiKey || migrated.Current().Proxy != "http://127.0.0.1:2222" ||
            migrated.Get("google").Proxy.Length != 0) throw new Exception("Legacy profile migration failed");
        var usage = new MyMemoryUsage(directory);
        usage.RecordSubmission(new string('x', 123));
        try { SettingsWindow.VerifyHidden(loaded, output, usage); }
        finally { File.Delete(Path.Combine(directory, "mymemory-usage.json")); }
    }
}

internal sealed class SettingsWindow : SurfaceWindow
{
    private readonly ServiceProfiles store;
    private readonly Dictionary<string, ServiceProfile> drafts = new Dictionary<string, ServiceProfile>();
    private readonly ServiceTabs tabs;
    private readonly Panel content;
    private readonly IntPtr owner;
    private readonly FluentField key, endpoint, model, region;
    private readonly MyMemoryUsage usage;
    private readonly Timer usageTimer;
    private SettingRow usageRow;
    private readonly SettingsGroup group;
    private readonly FluentButton save, cancel;
    private readonly ChromeBar chrome;
    private string message = "";
    private bool saveFailed;
    private string selected;
    private bool loading;
    internal SettingsWindow(ServiceProfiles profiles, MyMemoryUsage usage = null, IntPtr owner = default(IntPtr))
    {
        store = profiles; this.usage = usage; this.owner = owner; selected = store.Active;
        foreach (string provider in ServiceProfiles.Providers) drafts[provider] = store.Get(provider);
        Text = Program.T("截图翻译设置", "Screenshot Translate Settings");
        StartPosition = FormStartPosition.CenterScreen; ShowInTaskbar = false; TopMost = true;
        ClientSize = new Size(920, 640); MinimumSize = new Size(720, 480); KeyPreview = true;
        chrome = new ChromeBar(this, Text + " - Win顺"); Controls.Add(chrome);
        tabs = new ServiceTabs(ServiceProfiles.Names.Select((name, i) => Program.T(name, ServiceProfiles.EnglishNames[i])).ToArray()) { Left = 24, Top = 102, Width = ClientSize.Width - 48, BackColor = Ui.Page, AccessibleName = Program.T("渠道配置", "Service configuration") };
        tabs.SelectedIndexChanged += delegate { if (!loading) SelectProvider(ServiceProfiles.Providers[tabs.SelectedIndex]); };
        Controls.Add(tabs);
        content = new Panel { Left = 24, AutoScroll = true, BackColor = Ui.Page };
        Controls.Add(content);
        key = new FluentField { Secret = true, Cue = Program.T("API 密钥", "API key"), AccessibleName = "API key" };
        endpoint = new FluentField { AccessibleName = Program.T("服务地址", "Endpoint") };
        model = new FluentField { AccessibleName = Program.T("模型", "Model") };
        region = new FluentField { Cue = "eastasia", AccessibleName = Program.T("服务区域", "Region") };
        group = new SettingsGroup { Width = ClientSize.Width - 48 };
        content.Controls.Add(group);
        save = new FluentButton { Text = Program.T("保存", "Save"), Font = Ui.Setting, Primary = true, Top = ClientSize.Height - 60, Left = ClientSize.Width - 120, BackColor = Ui.Page, Anchor = AnchorStyles.Right | AnchorStyles.Bottom };
        cancel = new FluentButton { Text = Program.T("取消", "Cancel"), Font = Ui.Setting, Top = save.Top, Left = save.Left - 108, BackColor = Ui.Input, Anchor = AnchorStyles.Right | AnchorStyles.Bottom, DialogResult = DialogResult.Cancel };
        save.Click += delegate {
            try { SaveDrafts(); Close(); }
            catch (ArgumentException ex) { saveFailed = true; message = ex.Message; Invalidate(); }
            catch { saveFailed = true; message = Program.T("保存失败，配置未更改。请检查数据目录权限。", "Save failed. Settings were not changed. Check folder permissions."); Invalidate(); }
        };
        Controls.AddRange(new Control[] { save, cancel }); AcceptButton = save; CancelButton = cancel;
        loading = true; tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, selected); loading = false; LoadFields();
        usageTimer = new Timer { Interval = 2000 };
        usageTimer.Tick += delegate { RefreshUsage(); };
        usageTimer.Start();
        LayoutContent();
    }
    private void Remember()
    {
        ServiceProfile value = drafts[selected];
        value.ApiKey = key.Value.Trim(); value.Endpoint = endpoint.Value.Trim(); value.Model = model.Value.Trim(); value.Region = region.Value.Trim();
    }
    private void SaveDrafts() { Remember(); store.Save(drafts.Values); }
    private void SelectProvider(string provider) { Remember(); selected = provider; LoadFields(); }
    private void LoadFields()
    {
        ServiceProfile value = drafts[selected];
        key.Value = value.ApiKey; endpoint.Value = value.Endpoint; model.Value = value.Model; region.Value = value.Region;
        bool free = FreeServices.Handles(selected), configured = selected != "mymemory" && !free, ai = new[] { "openai", "deepseek", "doubao", "hunyuan" }.Contains(selected);
        key.Editor.UseSystemPasswordChar = true;
        usageRow = null;
        foreach (FluentField field in new[] { key, endpoint, model, region }) if (field.Parent != null) field.Parent.Controls.Remove(field);
        foreach (Control row in group.Controls.Cast<Control>().ToArray()) row.Dispose();
        group.Controls.Clear();
        if (configured)
        {
            AddRow(Program.T("API 密钥", "API key"), Program.T("Windows 当前用户加密", "Encrypted for this Windows user"), key, "");
            AddRow(Program.T("服务地址", "Endpoint"), Program.T("留空使用默认地址", "Empty uses the default endpoint"), endpoint, "");
            if (ai) AddRow(selected == "doubao" ? Program.T("模型 / 接入点 ID", "Model / Endpoint ID") : Program.T("模型", "Model"),
                selected == "doubao" ? Program.T("填写已开通的模型或接入点", "Your model or inference endpoint") : Program.T("留空使用默认模型", "Empty uses the default model"), model, "");
            if (selected == "microsoft") AddRow(Program.T("服务区域", "Region"), Program.T("单区域资源需填写", "Required for regional resources"), region, "");
        }
        else if (selected == "mymemory")
        {
            AddRow(Program.T("API 密钥", "API key"), "", null, Program.T("无需密钥", "Not required"));
            usageRow = AddRow(Program.T("本插件今日提交", "Submitted today by this plugin"), Program.T("匿名额度 5,000 字符 / 日；按本机日期统计，非服务余额", "Anonymous quota: 5,000/day. Local-date estimate, not server balance."), null, "");
            RefreshUsage();
            AddRow(Program.T("翻译文字", "Translation text"), Program.T("仅调用官方查询接口", "Official query endpoint"), null, Program.T("发送至 MyMemory", "Sent to MyMemory"));
        }
        else if (free)
        {
            if (selected == "zvo") AddRow(Program.T("API 密钥（可选）", "API key (optional)"), Program.T("免费通道留空，企业通道填写密钥", "Empty for free access; key for enterprise access"), key, "");
            else AddRow(Program.T("API 密钥", "API key"), "", null, Program.T("无需密钥", "Not required"));
            AddRow(Program.T("服务地址", "Endpoint"), Program.T("留空使用默认地址", "Empty uses the default endpoint"), endpoint, "");
            AddRow(Program.T("翻译文字", "Translation text"), Program.T("免费通道可能限流或变更", "Free access can be rate-limited or changed"), null,
                Program.T("发送至所选服务", "Sent to the selected service"));
        }
        group.Arrange();
        content.AutoScrollPosition = Point.Empty;
        LayoutContent();
        saveFailed = false;
        endpoint.Cue = DefaultEndpoint(selected);
        model.Cue = selected == "deepseek" ? "deepseek-flash" : selected == "hunyuan" ? "hunyuan-turbos-latest" : selected == "doubao" ? Program.T("模型或接入点 ID", "Model or endpoint ID") : "gpt-4o-mini";
        if (selected == "mymemory")
            message = Program.T("从新版启用起统计，失败请求也计入提交量；共享 IP 用量不可查询。", "Tracked since this update, including failed requests. Shared-IP usage is unavailable.");
        else message = "";
        Invalidate();
    }
    private void RefreshUsage()
    {
        if (usageRow == null) return;
        if (usage != null && !usage.Available)
        {
            usageRow.Value = Program.T("统计暂不可用", "Usage unavailable");
            usageRow.Invalidate(); return;
        }
        long submitted = usage == null ? 0 : usage.Today;
        usageRow.Value = submitted.ToString("N0") + Program.T(" / 5,000 字符", " / 5,000 characters");
        usageRow.Invalidate();
    }
    private static string DefaultEndpoint(string provider)
    {
        if (FreeServices.Handles(provider)) return FreeServices.DefaultEndpoint(provider);
        return provider == "deepseek" ? "https://api.deepseek.com" : provider == "hunyuan" ? "https://api.hunyuan.cloud.tencent.com/v1"
            : provider == "doubao" ? "https://ark.cn-beijing.volces.com/api/v3" : provider == "microsoft" ? "https://api.cognitive.microsofttranslator.com"
            : provider == "deepl" ? "https://api-free.deepl.com" : "https://api.openai.com/v1";
    }
    private SettingRow AddRow(string title, string description, FluentField field, string value)
    {
        var row = new SettingRow(title, description) { Width = group.Width - 2, Value = value };
        if (field != null) row.Attach(field);
        group.Controls.Add(row);
        return row;
    }
    protected override void OnSizeChanged(EventArgs e)
    {
        base.OnSizeChanged(e);
        if (tabs != null) tabs.Width = ClientSize.Width - 48;
        LayoutContent();
    }
    private void LayoutContent()
    {
        if (content == null || group == null || save == null) return;
        content.SetBounds(24, tabs.Bottom + 16, ClientSize.Width - 48, Math.Max(40, save.Top - tabs.Bottom - 36));
        group.Width = content.Width - (group.Height > content.Height ? SystemInformation.VerticalScrollBarWidth + 2 : 0);
        group.Arrange();
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Ui.Label(e.Graphics, Program.T("截图翻译", "Screenshot Translate"), Ui.Heading, new RectangleF(24, 56, Width - 48, 38), Ui.Text);
        Ui.Label(e.Graphics, message, Ui.Caption, new RectangleF(24, save.Top, cancel.Left - 48, 36), saveFailed ? Color.FromArgb(196, 43, 28) : Ui.Muted, false, true);
    }
    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (keyData == Keys.Escape) { Close(); return true; }
        return base.ProcessCmdKey(ref msg, keyData);
    }
    protected override void OnShown(EventArgs e) { base.OnShown(e); Present(); }
    private void AttachOwner() { NativeUi.SetOwner(Handle, owner); }
    internal void Present()
    {
        // WinForms assigns its own hidden owner during handle creation.
        AttachOwner();
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        BringToFront(); Activate();
    }
    protected override void WndProc(ref Message message)
    {
        base.WndProc(ref message);
        if (message.Msg != 0x84 || WindowState != FormWindowState.Normal || message.Result.ToInt32() != 1) return;
        long position = message.LParam.ToInt64();
        Point point = PointToClient(new Point((short)(position & 0xffff), (short)((position >> 16) & 0xffff)));
        bool left = point.X < 6, right = point.X >= Width - 6, top = point.Y < 6, bottom = point.Y >= Height - 6;
        int edge = top ? left ? 13 : right ? 14 : 12 : bottom ? left ? 16 : right ? 17 : 15 : left ? 10 : right ? 11 : 1;
        message.Result = new IntPtr(edge);
    }
    internal static void VerifyHidden(ServiceProfiles profiles, string output, MyMemoryUsage usage)
    {
        using (var main = new ServiceTabs(new[] { "MyMemory", "Google", "Edge", "Zvo", "微软", "DeepL", "模型服务" }) { Width = 600 })
        using (var models = new ServiceTabs(new[] { "OpenAI", "DeepSeek", "豆包", "混元" }) { Width = 450 })
            if (main.Height != 38 || models.Height != 38) throw new Exception("Host channel groups do not fit flat rows");
        using (var window = new SettingsWindow(profiles, usage))
        {
            if (!window.TopMost) throw new Exception("Settings can be covered by the host");
            window.tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, "openai");
            window.key.Value = "draft-a"; window.model.Value = "draft-model-a";
            window.tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, "microsoft");
            if (window.key.Value.Length != 0 || window.region.Parent == null || window.model.Parent != null) throw new Exception("Provider fields leaked");
            window.key.Value = "draft-b";
            window.tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, "openai");
            if (window.key.Value != "draft-a" || window.model.Value != "draft-model-a") throw new Exception("Provider drafts were lost");
            window.SaveHidden(Path.Combine(output, "selftest-settings.png"));
            window.tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, "mymemory");
            if (!window.usageRow.Value.StartsWith("123")) throw new Exception("Usage display failed");
            usage.RecordSubmission("test"); window.RefreshUsage();
            if (!window.usageRow.Value.StartsWith("127")) throw new Exception("Usage live refresh failed");
            window.SaveHidden(Path.Combine(output, "selftest-settings-trial.png"));
            window.ClientSize = new Size(780, 640);
            window.SaveHidden(Path.Combine(output, "selftest-settings-compact.png"));
            window.tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, "zvo");
            if (window.endpoint.Parent == null || window.key.Parent == null || window.model.Parent != null ||
                window.group.Controls.Count != 3) throw new Exception("Provider-only free service fields failed");
            window.SaveHidden(Path.Combine(output, "selftest-settings-zvo.png"));
            bool previous = Ui.Dark; Ui.Dark = true;
            try { window.ApplyTheme(); window.SaveHidden(Path.Combine(output, "selftest-settings-dark.png")); }
            finally { Ui.Dark = previous; window.ApplyTheme(); }
            window.ClientSize = new Size(720, 480);
            for (int i = 0; i < ServiceProfiles.Providers.Length; i++)
            {
                window.tabs.SelectedIndex = i;
                window.VerifyLayout();
                if (profiles.Active != "deepseek") throw new Exception("Viewing configuration changed the enabled channel");
            }
            window.tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, "openai");
            window.SaveHidden(Path.Combine(output, "selftest-settings-narrow.png"));
            window.cancel.PerformClick();
            if (!window.IsDisposed || profiles.Active != "deepseek") throw new Exception("Cancel changed the saved service");
            if (profiles.Get("openai").ApiKey == "draft-a") throw new Exception("Cancel saved provider drafts");
        }
        using (var window = new SettingsWindow(profiles, usage))
        {
            window.tabs.SelectedIndex = Array.IndexOf(ServiceProfiles.Providers, "openai");
            window.model.Value = "saved-draft-model";
            profiles.ApplySettings(Program.Obj("provider", "google", "proxy", "http://127.0.0.1:3333"));
            window.SaveDrafts();
            if (profiles.Active != "google" || profiles.Current().Proxy != "http://127.0.0.1:3333" ||
                profiles.Get("openai").Model != "saved-draft-model") throw new Exception("Saving a provider draft changed common settings");
        }
        using (var parent = new Form())
        using (var window = new SettingsWindow(profiles, usage, parent.Handle))
        {
            window.AttachOwner();
            if (NativeUi.OwnerOf(window.Handle) != parent.Handle || window.Visible || parent.Visible)
                throw new Exception("Hidden settings owner relationship failed");
        }
    }
    private void VerifyLayout()
    {
        for (int i = 0; i < ServiceProfiles.Providers.Length; i++)
        {
            Rectangle box = tabs.TabBounds(i);
            if (box.Left < 0 || box.Right > tabs.Width || box.Bottom > tabs.Height) throw new Exception("Flat channel label was clipped");
            for (int j = 0; j < i; j++) if (box.IntersectsWith(tabs.TabBounds(j))) throw new Exception("Flat channel labels overlap");
        }
        if (content.Top <= tabs.Bottom || content.Bottom >= save.Top || save.Bottom > ClientSize.Height)
            throw new Exception("Settings header, fields or footer overlap");
        foreach (SettingRow row in group.Controls)
            if (row.Field != null && (!row.ClientRectangle.Contains(row.Field.Bounds) || row.Field.Editor.Width < 40))
                throw new Exception("Provider input escaped its row");
        if (Visible) throw new Exception("Settings test showed a window");
    }
    private void SaveHidden(string path)
    {
        PerformLayout();
        using (var bitmap = new Bitmap(ClientSize.Width, ClientSize.Height))
        using (Graphics g = Graphics.FromImage(bitmap))
        {
            g.Clear(BackColor);
            OnPaint(new PaintEventArgs(g, ClientRectangle));
            Ui.Snapshot(chrome, g, chrome.Location); Ui.Snapshot(tabs, g, tabs.Location);
            GraphicsState state = g.Save();
            g.SetClip(content.Bounds);
            group.Snapshot(g, new Point(content.Left + group.Left, content.Top + group.Top));
            g.Restore(state);
            Ui.Snapshot(save, g, save.Location); Ui.Snapshot(cancel, g, cancel.Location);
            bitmap.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
    }
    internal override void ApplyTheme()
    {
        base.ApplyTheme(); content.BackColor = tabs.BackColor = chrome.BackColor = save.BackColor = Ui.Page; cancel.BackColor = Ui.Input;
        foreach (Control control in chrome.Controls) control.BackColor = Ui.Page;
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            if (usageTimer != null) usageTimer.Dispose();
            foreach (FluentField field in new[] { key, endpoint, model, region }) if (field != null && field.Parent == null) field.Dispose();
        }
        base.Dispose(disposing);
    }
}

// MIT. Protocol, service defaults and OCR spacing follow WinShun's documented APIs.
// Compiled during installation, never at plugin startup.
using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Automation;
using System.Windows.Forms;
using Windows.Globalization;
using Windows.Graphics.Imaging;
using Windows.Media.Ocr;
using Windows.Storage.Streams;
using Dict = System.Collections.Generic.Dictionary<string, object>;

internal static class Program
{
    private static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 };
    private static readonly BlockingCollection<Dict> Input = new BlockingCollection<Dict>();
    private static readonly Queue<Dict> Backlog = new Queue<Dict>();
    private static readonly TriggerGate Triggers = new TriggerGate();
    private static readonly Dictionary<string, Pending> Results = new Dictionary<string, Pending>();
    private static readonly Dictionary<string, string> Keywords = new Dictionary<string, string> {
        {"shot", "jst"}, {"clip", "jft"}, {"jsel", "jsel"}
    };
    private static Dict settings = new Dict();
    private static bool chinese = true, hostHotkeys, running = true, dirty = true;
    private static bool inputTest;
    private static DateTime activity = DateTime.UtcNow;
    private static string[] registeredHotkeys;
    private static string dataDir, dpiBefore, dpiAfter, dpiMode;
    private static int sequence;
    private static PinWindow pin;
    private static Thread pinThread;
    private static Action postAction;
    private static ServiceProfiles profiles;
    private static MyMemoryUsage trialUsage;
    private static SettingsWindow configWindow;
    private static Thread configThread;

    internal static Dict Obj(params object[] pairs)
    {
        var value = new Dict();
        for (int i = 0; i < pairs.Length; i += 2) value[(string)pairs[i]] = pairs[i + 1];
        return value;
    }
    private static object Get(object value, string key)
    {
        var map = value as Dict;
        object result;
        return map != null && map.TryGetValue(key, out result) ? result : null;
    }
    private static string Str(object value) { return value == null ? "" : Convert.ToString(value); }
    private static string Setting(string key) { return Str(Get(settings, key)).Trim(); }
    internal static string T(string zh, string en) { return chinese ? zh : en; }
    internal static void Log(string text) { Console.Error.WriteLine("[screenshot-translate] " + text); }
    private static void Send(Dict message) { Console.Out.WriteLine(Json.Serialize(message)); Console.Out.Flush(); }
    private static void Reply(object id, object result) { if (id != null) Send(Obj("jsonrpc", "2.0", "id", id, "result", result)); }
    private static void Error(object id, int code, string message) { if (id != null) Send(Obj("jsonrpc", "2.0", "id", id, "error", Obj("code", code, "message", message))); }
    private static object Tool(string name, Dict arguments)
    {
        string id = "p" + ++sequence;
        Send(Obj("jsonrpc", "2.0", "id", id, "method", "winshun/tool", "params", Obj("name", name, "arguments", arguments)));
        DateTime deadline = DateTime.UtcNow.AddSeconds(9);
        while (DateTime.UtcNow < deadline)
        {
            Dict message;
            if (!Input.TryTake(out message, 100)) { if (Input.IsCompleted) break; continue; }
            if (Str(Get(message, "id")) == id)
            {
                if (Get(message, "error") != null) throw new Exception("Host tool failed: " + name);
                return Get(message, "result");
            }
            Backlog.Enqueue(message);
        }
        throw new TimeoutException("Host did not answer: " + name);
    }
    private static void ReadInput()
    {
        var parser = new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 };
        try
        {
            string line;
            while ((line = Console.In.ReadLine()) != null)
            {
                if (String.IsNullOrWhiteSpace(line)) continue;
                Dict message = null;
                try { message = parser.DeserializeObject(line) as Dict; } catch { }
                if (message == null) { Log("ignored a line that is not JSON"); continue; }
                QueueInput(message);
            }
        }
        catch (IOException ex) { Log("stdin closed: " + ex.Message); }
        finally { Input.CompleteAdding(); }
    }
    private static string TriggerKind(Dict message)
    {
        string method = Str(Get(message, "method"));
        if (method == "hotkey" || method == "internal-retry" || method == "internal-translate") return method;
        if (method != "action") return "";
        string action = Str(Get(Get(message, "params"), "action"));
        return new[] { "start-region", "retry-shot", "start-full", "start-window", "retry-clip", "read-selection" }.Contains(action) ? action : "";
    }
    private static void QueueInput(Dict message)
    {
        string kind = TriggerKind(message);
        if (kind.Length > 0)
        {
            TriggerGate.Receipt receipt = Triggers.Receive();
            message["__receipt"] = receipt;
            if (kind == "hotkey" && receipt.Busy)
            {
                Log("input: ignored kind=hotkey reason=busy");
                return;
            }
        }
        Input.Add(message);
    }
    [STAThread]
    private static int Main(string[] args)
    {
        Console.InputEncoding = Encoding.UTF8;
        Console.OutputEncoding = new UTF8Encoding(false);
        System.Net.ServicePointManager.SecurityProtocol = System.Net.SecurityProtocolType.Tls12;
        dataDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"WinShun\plugins\iuuuuuuuu.screenshot-translate.data");
        inputTest = args.Contains("--input-test");
        SetDpi();
        if (args.Contains("--self-test")) return SelfTest();
        if (args.Length >= 4 && args[0] == "--image-translation-test")
        {
            settings = Obj("provider", args[2], "target", "zh", "proxy", args.Length > 4 ? args[4] : "");
            Recognition ocr = Recognize(File.ReadAllBytes(args[1]));
            if (ocr.Lines.Length == 0) return 1;
            object[] items = Translate(ocr.Text, "start-region", "shot", null, "en", "zh", ocr.Lines.Select(x => x.Text).ToArray());
            var data = Get(items[0], "data");
            string[] parts = Get(data, "segments") as string[];
            if (parts == null) { Console.WriteLine(Json.Serialize(items[0])); return 1; }
            using (var original = new Bitmap(args[1]))
            using (var rendered = PinWindow.Render(original, ocr.Lines, Str(Get(data, "translated")), 1, parts))
            {
                rendered.Save(args[3], ImageFormat.Png);
                bool sameSize = original.Size == rendered.Size;
                Console.WriteLine("image-translation-test: provider=" + args[2] + " blocks=" + parts.Length + " sameSize=" + sameSize);
                if (!sameSize && parts.Length > 1) return 1;
            }
            return 0;
        }
        if (args.Length >= 2 && args[0] == "--blocks-test")
        {
            settings = Obj("provider", args[1], "target", "auto");
            if (args.Length > 2) { settings["endpoint"] = args[2]; settings["apiKey"] = "test-only"; }
            string[] parts = { "Pull requests", "Agents", "Security and quality", "Insights" };
            if (args.Length > 3) parts = args[3].StartsWith("[") ? Json.Deserialize<string[]>(args[3])
                : Enumerable.Range(1, Int32.Parse(args[3])).Select(i => "Part " + i).ToArray();
            object[] items = Translate(String.Join("\n", parts), "start-region", "shot", null, "en", "zh", parts);
            var result = Get(items[0], "data");
            string[] translated = Get(result, "segments") as string[];
            if (translated == null || translated.Length != parts.Length) { Console.WriteLine(Json.Serialize(items[0])); return 1; }
            Console.WriteLine("blocks-test: provider=" + args[1] + " mapped=" + translated.Length + " PASS");
            Console.WriteLine(Json.Serialize(translated));
            return 0;
        }
        if (args.Length >= 2 && (args[0] == "--ocr-test" || args[0] == "--layout-test"))
        {
            Recognition ocr = Recognize(File.ReadAllBytes(args[1]));
            bool matches = args[0] == "--layout-test" || args.Length < 3 || ocr.Text.IndexOf(args[2], StringComparison.OrdinalIgnoreCase) >= 0;
            Size size;
            using (var image = new Bitmap(args[1])) size = image.Size;
            bool inBounds = ocr.Lines.All(x => !x.Box.IsEmpty && x.Box.Left >= 0 && x.Box.Top >= 0 && x.Box.Right <= size.Width && x.Box.Bottom <= size.Height);
            Console.WriteLine("ocr-test: lines=" + ocr.Lines.Length + " chars=" + ocr.Text.Length + " matches=" + matches + " inBounds=" + inBounds + " detected=" + DetectLanguage(ocr.Text) + " target=" + TargetLanguage(ocr.Text));
            if (args[0] == "--layout-test")
            {
                foreach (var block in ocr.Lines) Console.WriteLine(Json.Serialize(Obj("text", block.Text, "box", new[] { block.Box.X, block.Box.Y, block.Box.Width, block.Box.Height })));
                if (args.Length > 2 && args[2] == "--candidates")
                {
                    Recognition candidate = RecognizeEnlarged(File.ReadAllBytes(args[1]), CreateEngine(), size, 2, 6);
                    foreach (var block in candidate.Lines) Console.WriteLine(Json.Serialize(Obj("candidate", block.Text,
                        "box", new[] { block.Box.X, block.Box.Y, block.Box.Width, block.Box.Height })));
                    return ocr.Lines.Length > 0 && inBounds ? 0 : 1;
                }
                string[] translations = args.Length > 2 ? Json.Deserialize<string[]>(args[2]) : ocr.Lines.Select(x => x.Text).ToArray();
                using (var image = new Bitmap(args[1]))
                using (var rendered = PinWindow.Render(image, ocr.Lines, String.Join("\n", translations), 1, translations))
                    rendered.Save(Path.Combine(ShotsDir(), "selftest-spatial.png"), ImageFormat.Png);
            }
            return ocr.Lines.Length > 0 && matches && inBounds ? 0 : 1;
        }
        var reader = new Thread(ReadInput) { IsBackground = true, Name = "translation-protocol" };
        reader.Start();
        try
        {
            while (running)
            {
                if (postAction != null)
                {
                    Action work = postAction; postAction = null;
                    using (Triggers.Busy())
                        try { work(); } catch (Exception ex) { Log("flow failed: " + ex.Message); }
                    dirty = true; activity = DateTime.UtcNow;
                    continue;
                }
                if (pin != null && pin.IsDisposed) { ClosePin(); }
                Maintenance();
                Dict message;
                if (Backlog.Count > 0) message = Backlog.Dequeue();
                else if (!Input.TryTake(out message, 250)) { if (Input.IsCompleted) break; continue; }
                dirty = true; activity = DateTime.UtcNow;
                try { Dispatch(message); }
                catch (Exception ex) { Log("request failed: " + ex.Message); Error(Get(message, "id"), -32603, ex.Message); }
                message = null;
            }
        }
        finally { ClosePin(); CloseSettings(); }
        return 0;
    }
    private static void Maintenance()
    {
        DateTime now = DateTime.UtcNow;
        foreach (string key in Results.Where(x => (now - x.Value.Time).TotalSeconds > 120).Select(x => x.Key).ToArray())
        { Results.Remove(key); dirty = true; }
        if (!dirty || (now - activity).TotalSeconds < 5) return;
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        dirty = false;
        Log("memory: idle managedMB=" + Math.Round(GC.GetTotalMemory(false) / 1048576.0, 1));
    }
    private static void Dispatch(Dict message)
    {
        object id = Get(message, "id"), p = Get(message, "params");
        string method = Str(Get(message, "method"));
        if (method.Length == 0) return;
        string kind = TriggerKind(message);
        if (kind.Length > 0)
        {
            double age;
            string reason;
            if (!Triggers.Current(Get(message, "__receipt") as TriggerGate.Receipt, out age, out reason))
            {
                Log("input: ignored kind=" + kind + " reason=" + reason + " queueMs=" + Math.Round(age));
                Reply(id, method == "action" ? Obj("effects", new object[] { Obj("type", "keep") }) : Obj());
                return;
            }
            Log("input: accepted kind=" + kind + " queueMs=" + Math.Round(age));
        }
        switch (method)
        {
            case "initialize":
                settings = Get(p, "settings") as Dict ?? new Dict();
                chinese = !Str(Get(p, "locale")).StartsWith("en", StringComparison.OrdinalIgnoreCase);
                Ui.Dark = Str(Get(p, "theme")) == "dark" || Str(Get(Get(p, "appearance"), "theme")) == "dark";
                hostHotkeys = Get(Get(p, "capabilities"), "hostHotkeys") is bool && (bool)Get(Get(p, "capabilities"), "hostHotkeys");
                string hostDir = Str(Get(p, "dataDir")), envDir = Environment.GetEnvironmentVariable("WS_DATA_DIR");
                if (!String.IsNullOrEmpty(hostDir)) dataDir = hostDir;
                if (!String.IsNullOrEmpty(envDir) && Directory.Exists(envDir)) dataDir = envDir;
                profiles = new ServiceProfiles(dataDir, settings);
                trialUsage = new MyMemoryUsage(dataDir);
                Log("startup: version=0.1.10 runtime=compiled-dotnet ocr=on-demand hostHotkeys=" + hostHotkeys);
                Reply(id, Obj("protocolVersion", "1", "name", "iuuuuuuuu.screenshot-translate"));
                if (hostHotkeys) RegisterHotkeys();
                return;
            case "settingsChanged":
                settings = Get(p, "settings") as Dict ?? settings;
                if (profiles != null) profiles.ApplySettings(settings);
                if (hostHotkeys) RegisterHotkeys();
                return;
            case "appearance":
                Ui.Dark = Str(Get(p, "theme")) == "dark";
                if (Str(Get(p, "locale")).Length > 0) chinese = !Str(Get(p, "locale")).StartsWith("en", StringComparison.OrdinalIgnoreCase);
                RefreshAppearance();
                return;
            case "query": Query(id, p); return;
            case "action": HandleAction(id, p); return;
            case "hotkey":
                string key = Str(Get(p, "key"));
                string route = HotkeyCommand(key);
                if (route == "shot") Shot("region");
                else if (route.Length > 0) TextFlow(route);
                return;
            case "internal-retry": Shot("region"); return;
            case "internal-translate": TranslatePin(p); return;
            case "showSettings": Reply(id, Obj()); OpenSettings(); return;
            case "internal-settings": OpenSettings(); return;
            case "cancel": return;
            case "shutdown": ClosePin(); CloseSettings(); Reply(id, Obj()); running = false; return;
            default: Error(id, -32601, "unknown method: " + method); return;
        }
    }
    private static void RegisterHotkeys()
    {
        string[] keys = new[] { Setting("shotHotkey"), Setting("clipHotkey"), Setting("selectionHotkey") }
            .Where(x => x.Length > 0).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (registeredHotkeys != null && registeredHotkeys.SequenceEqual(keys, StringComparer.OrdinalIgnoreCase)) return;
        Send(Obj("jsonrpc", "2.0", "method", "winshun/hotkeys", "params", Obj("keys", keys)));
        try
        {
            var errors = Get(Tool("register_hotkeys", Obj("keys", keys)), "errors") as Dict;
            if (errors != null) foreach (var error in errors) Log("hotkey " + error.Key + " rejected: " + Str(error.Value));
            registeredHotkeys = keys;
        }
        catch (Exception ex) { Log("register_hotkeys failed: " + ex.Message); }
    }
    private static string HotkeyCommand(string key)
    {
        if (String.IsNullOrWhiteSpace(key)) return "";
        if (key.Equals(Setting("shotHotkey"), StringComparison.OrdinalIgnoreCase)) return "shot";
        if (key.Equals(Setting("clipHotkey"), StringComparison.OrdinalIgnoreCase)) return "clip";
        if (key.Equals(Setting("selectionHotkey"), StringComparison.OrdinalIgnoreCase)) return "jsel";
        return "";
    }
    private static Dict Row(string id, string title, string subtitle)
    { return Obj("id", id, "title", title, "subtitle", subtitle, "icon", "glyph:U+E8C1", "valid", false); }
    private static Dict Config(string reason)
    {
        Dict row = Row("error-config", T("还没有设置翻译服务", "No translation service is set up"), reason);
        row.Remove("valid"); row["open"] = Obj("url", "winshun:settings"); return row;
    }
    private static Dict SelectionHint()
    {
        string key = Setting("selectionHotkey");
        return Row("hint-selection", T("翻译当前窗口里选中的文字", "Translate the selected text in the active window"),
            key.Length > 0 ? T("先选中文字，再按 " + key + "；关键字模式读不到其他窗口的选区", "Select text and press " + key + "; keyword mode cannot read another window's selection")
            : T("划词热键还没设置；打开设置 → 插件 → 截图翻译", "Set the selection hotkey in Settings → Plugins → Screenshot Translate"));
    }
    private static void Query(object id, object p)
    {
        string command = Str(Get(p, "command")), text = Str(Get(p, "text")).Trim(), keyword = Str(Get(p, "keyword"));
        if (Keywords.ContainsKey(command) && keyword.Length > 0) Keywords[command] = keyword;
        Pending pending;
        object[] items;
        if (Results.TryGetValue(command, out pending)) { items = pending.Items; Results.Remove(command); }
        else if (command == "shot")
        {
            Dict hint = Row("hint-start", T("框选屏幕区域翻译", "Translate a screen region"),
                T("Enter 框选；Ctrl+Enter 全屏；Shift+Enter 当前窗口", "Enter selects a region; Ctrl+Enter captures the screen; Shift+Enter captures the active window"));
            hint["valid"] = true; items = new object[] { hint };
        }
        else if (command == "clip" || command == "jsel")
        {
            if (command == "jsel" && text.Length == 0) items = new object[] { SelectionHint() };
            else
            {
                bool typed = text.Length > 0;
                if (!typed) text = ClipboardText();
                items = text.Length == 0 ? new object[] { EmptyText("clip") }
                    : Translate(text, typed ? "retry-text" : "retry-clip", command, Get(p, "queryId"));
            }
        }
        else items = new object[0];
        Reply(id, Obj("items", items, "rerun", 0, "cache", 0));
    }
    private static Dict EmptyText(string command)
    {
        return command == "clip"
            ? Row("empty-clip", T("剪贴板里没有文字", "The clipboard has no text"), T("复制一段文字后再试，或用 jft 直接输入文字", "Copy text first, or type text after jft"))
            : Row("empty-selection", T("没有读到选中的文字", "No selected text was found"), T("该应用可能不支持 UI Automation；可以先 Ctrl+C，再用 jft", "The app may not support UI Automation; copy with Ctrl+C and use jft"));
    }
    private static string ClipboardText()
    {
        for (int i = 0; i < 5; i++)
        {
            try { return Clipboard.ContainsText() ? Clipboard.GetText().Trim() : ""; }
            catch (ExternalException) { Thread.Sleep(80); }
        }
        return "";
    }
    private static string SelectedText()
    {
        AutomationElement focus = AutomationElement.FocusedElement;
        object pattern;
        if (focus == null || !focus.TryGetCurrentPattern(TextPattern.Pattern, out pattern)) return "";
        return String.Join("\n", ((TextPattern)pattern).GetSelection().Select(x => x.GetText(-1))).Trim();
    }
    private static void TextFlow(string command)
    {
        if (inputTest) { using (Triggers.Busy()) Log("input-test: text flow"); return; }
        using (Triggers.Busy())
        try
        {
            string text = command == "clip" ? ClipboardText() : SelectedText();
            Show(command, text.Length == 0 ? new object[] { EmptyText(command) }
                : Translate(text, command == "clip" ? "retry-clip" : "read-selection", command, null));
        }
        catch (Exception ex) { Show(command, new object[] { Row("empty-selection", T("读取文字失败", "Could not read text"), ex.Message) }); }
    }
    private static void Show(string command, object[] items)
    {
        Results[command] = new Pending { Items = items, Time = DateTime.UtcNow };
        if (command == "shot") return;
        try { Tool("show_launcher", Obj("text", Keywords[command] + " " + T("刚刚", "just now"))); }
        catch (Exception ex) { Log("show_launcher failed: " + ex.Message); }
    }
    private static void HandleAction(object id, object p)
    {
        string action = Str(Get(p, "action")), source = Str(Get(Get(Get(p, "item"), "data"), "source"));
        string translated = Str(Get(Get(Get(p, "item"), "data"), "translated"));
        string command = Str(Get(Get(Get(p, "item"), "data"), "command"));
        if (action == "copy-translation" || action == "copy-source")
        {
            string text = action == "copy-source" ? source : translated;
            if (text.Length == 0) text = Str(Get(Get(p, "item"), "copy"));
            Reply(id, Obj("effects", text.Length == 0
                ? new object[] { Obj("type", "notify", "text", T("没有可复制的内容", "Nothing to copy")), Obj("type", "keep") }
                : new object[] { Obj("type", "copy", "text", text), Obj("type", "notify", "text", T("已复制", "Copied")), Obj("type", "hide") }));
            return;
        }
        if (action == "retry-text")
        {
            string keyword = Keywords.ContainsKey(command) ? Keywords[command] : Keywords["clip"];
            Reply(id, Obj("effects", new object[] { source.Length > 0 ? Obj("type", "setQuery", "text", keyword + " " + source) : Obj("type", "keep") }));
            return;
        }
        switch (action)
        {
            case "start-region": case "retry-shot": postAction = delegate { Thread.Sleep(350); Shot("region"); }; break;
            case "start-full": postAction = delegate { Thread.Sleep(350); Shot("full"); }; break;
            case "start-window": postAction = delegate { Thread.Sleep(350); Shot("window"); }; break;
            case "retry-clip": postAction = delegate { TextFlow("clip"); }; break;
            case "read-selection": postAction = delegate { TextFlow("jsel"); }; break;
            case "pin-close": ClosePin(); break;
        }
        Reply(id, Obj("effects", new object[] { Obj("type", "hide") }));
    }
    internal static string DetectLanguage(string text)
    {
        int latin = Regex.Matches(text, @"[A-Za-z\u00c0-\u024f]").Count;
        int han = Regex.Matches(text, @"[\u3400-\u9fff\uf900-\ufaff]").Count;
        int kana = Regex.Matches(text, @"[\u3041-\u30fa\uff66-\uff9d]").Count;
        int hangul = Regex.Matches(text, @"[\u1100-\u11ff\u3130-\u318f\uac00-\ud7af]").Count;
        int cyrillic = Regex.Matches(text, @"[\u0400-\u052f]").Count;
        // CJK characters carry more information, but isolated OCR artifacts must
        // not reverse a predominantly Latin screenshot's translation direction.
        if (kana > 0 && (han + kana) * 2 >= latin && kana >= hangul && (kana >= 2 || latin + han == 0)) return "ja";
        if (hangul > 0 && hangul * 2 >= latin + han * 2 + kana * 2) return "ko";
        if (han > 0 && han * 2 >= latin + cyrillic) return "zh";
        if (cyrillic > latin && cyrillic > 0) return "ru";
        return "en";
    }
    private static string TargetLanguage(string text)
    {
        string target = Setting("target");
        return LanguageChoice.Codes.Contains(target) ? target : DetectLanguage(text) == "zh" ? "en" : "zh";
    }
    private static object[] Translate(string text, string retry, string command, object queryId, string from = "auto", string to = "", string[] parts = null)
    {
        using (Triggers.Busy()) return TranslateCore(text, retry, command, queryId, from, to, parts);
    }
    private static object[] TranslateCore(string text, string retry, string command, object queryId, string from, string to, string[] parts)
    {
        ServiceProfile service = profiles == null ? ServiceProfile.FromSettings(settings) : profiles.Current();
        string provider = service.Provider, key = service.ApiKey, model = service.Model;
        if (!ServiceProfiles.Providers.Contains(provider))
            return new object[] { Config(T("请选择翻译渠道并填写 API 密钥。", "Choose a service and add an API key.")) };
        if (provider != "mymemory" && !FreeServices.Handles(provider) && key.Length == 0) return new object[] { Config(T("还没有填 API 密钥。", "The API key is empty.")) };
        if (provider == "doubao" && model.Length == 0) return new object[] { Config(T("豆包需要填写模型或推理接入点 ID。", "Doubao needs a model or endpoint ID.")) };
        string target = to.Length > 0 ? to : TargetLanguage(text);
        if (queryId != null) Send(Obj("jsonrpc", "2.0", "method", "query/partial", "params",
            Obj("queryId", queryId, "items", new object[] { Row("working", T("翻译中…", "Translating…"), Short(text)) }, "replace", false)));
        try
        {
            string endpoint = service.Endpoint.TrimEnd('/'), language = LanguageChoice.EnglishName(target);
            string detected = "", translated = null;
            string[] translatedParts = null;
            if (provider == "mymemory")
            {
                if (parts == null) translated = TranslateTrial(text, target, from, service.Proxy);
                else { translatedParts = TranslateTrialParts(parts, target, from, service.Proxy); translated = String.Join("\n", translatedParts); }
            }
            else if (provider == "deepl" && parts != null && parts.Length > 50)
            {
                FreeTranslation result = TranslateDeepLBlocks(service, parts, from, target);
                translated = result.Text; detected = result.Detected; translatedParts = result.Parts;
            }
            else if (FreeServices.Handles(provider))
            {
                FreeTranslation result = FreeServices.Translate(service, text, from, target, parts);
                translated = result.Text; detected = result.Detected; translatedParts = result.Parts;
            }
            if (translated == null)
            {
            var headers = new Dictionary<string, string>(); object body;
            if (provider == "deepl")
            {
                if (endpoint.Length == 0) endpoint = "https://" + (key.EndsWith(":fx") ? "api-free.deepl.com" : "api.deepl.com");
                if (!endpoint.EndsWith("/v2/translate")) endpoint += "/v2/translate";
                headers["Authorization"] = "DeepL-Auth-Key " + key; body = Obj("text", parts ?? new[] { text }, "target_lang", target.ToUpperInvariant());
                if (from != "auto") ((Dict)body)["source_lang"] = from.ToUpperInvariant();
            }
            else if (provider == "microsoft")
            {
                if (endpoint.Length == 0) endpoint = "https://api.cognitive.microsofttranslator.com";
                if (!endpoint.EndsWith("/translate")) endpoint += "/translate";
                endpoint += "?api-version=3.0&to=" + (target == "zh" ? "zh-Hans" : target);
                if (from != "auto") endpoint += "&from=" + (from == "zh" ? "zh-Hans" : from);
                headers["Ocp-Apim-Subscription-Key"] = key;
                if (service.Region.Length > 0) headers["Ocp-Apim-Subscription-Region"] = service.Region;
                body = (parts ?? new[] { text }).Select(x => Obj("Text", x)).ToArray();
            }
            else
            {
                if (endpoint.Length == 0) endpoint = provider == "deepseek" ? "https://api.deepseek.com"
                    : provider == "doubao" ? "https://ark.cn-beijing.volces.com/api/v3"
                    : provider == "hunyuan" ? "https://api.hunyuan.cloud.tencent.com/v1" : "https://api.openai.com/v1";
                if (!endpoint.EndsWith("/chat/completions")) endpoint += "/chat/completions";
                if (model.Length == 0) model = provider == "deepseek" ? "deepseek-flash" : provider == "hunyuan" ? "hunyuan-turbos-latest" : "gpt-4o-mini";
                headers["Authorization"] = "Bearer " + key;
                body = Obj("model", model, "temperature", 0, "messages", new[] {
                    Obj("role", "system", "content", "Translate the following text " + (from == "auto" ? "" : "from " + LanguageChoice.EnglishName(from) + " ") + "into " + language + ". " +
                        (parts == null ? "Preserve line breaks. Reply with the translation only, no explanations." : "Input is a JSON array of independent screen text blocks. Return a JSON array of translated strings with exactly the same length and order. Do not merge, omit or add blocks. Keep identifiers unchanged. No markdown or explanations.")),
                    Obj("role", "user", "content", parts == null ? text : Json.Serialize(parts))
                });
            }
            Uri uri;
            if (!Uri.TryCreate(endpoint, UriKind.Absolute, out uri) || (uri.Scheme != "http" && uri.Scheme != "https")) throw new Exception(T("接口地址不是有效的 HTTP URL", "The endpoint is not a valid HTTP URL"));
            string answer;
            using (var client = FreeServices.CreateClient(service.Proxy))
            using (var request = new HttpRequestMessage(HttpMethod.Post, uri))
            {
                request.Headers.ExpectContinue = false;
                foreach (var header in headers) request.Headers.TryAddWithoutValidation(header.Key, header.Value);
                request.Content = new StringContent(Json.Serialize(body), Encoding.UTF8, "application/json");
                using (var response = client.SendAsync(request).GetAwaiter().GetResult())
                {
                    answer = response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                    if (!response.IsSuccessStatusCode)
                    {
                        string reason = "HTTP " + (int)response.StatusCode;
                        try { string detail = Str(Get(Get(Json.DeserializeObject(answer), "error"), "message")); if (detail.Length > 0) reason += ": " + detail; } catch { }
                        throw new Exception(reason);
                    }
                }
            }
            object decoded = Json.DeserializeObject(answer);
            if (provider == "microsoft")
            {
                object first = ((object[])decoded)[0];
                translatedParts = ((object[])decoded).Select(x => Str(Get(((object[])Get(x, "translations"))[0], "text"))).ToArray();
                translated = parts == null ? translatedParts[0] : String.Join("\n", translatedParts);
                detected = Str(Get(Get(first, "detectedLanguage"), "language"));
            }
            else if (provider == "deepl")
            {
                object first = ((object[])Get(decoded, "translations"))[0];
                translatedParts = ((object[])Get(decoded, "translations")).Select(x => Str(Get(x, "text"))).ToArray();
                translated = parts == null ? translatedParts[0] : String.Join("\n", translatedParts); detected = Str(Get(first, "detected_source_language"));
            }
            else
            {
                translated = Str(Get(Get(((object[])Get(decoded, "choices"))[0], "message"), "content"));
                if (parts != null)
                {
                    string content = Regex.Replace(translated.Trim(), @"^```(?:json)?\s*|\s*```$", "", RegexOptions.IgnoreCase);
                    translatedParts = Json.Deserialize<string[]>(content);
                    translated = String.Join("\n", translatedParts);
                }
            }
            }
            if (parts != null && (translatedParts == null || translatedParts.Length != parts.Length || translatedParts.Any(String.IsNullOrWhiteSpace)))
                throw new Exception(T("服务返回的文字块不完整，请重试", "The service returned incomplete text blocks. Try again."));
            if (parts != null)
            {
                for (int i = 0; i < parts.Length; i++)
                    if (SpatialText.IsLiteral(parts[i])) translatedParts[i] = parts[i];
                translated = String.Join("\n", translatedParts);
            }
            translated = translated.Trim();
            if (translated.Length == 0) throw new Exception(T("服务没有返回译文", "The service returned no translation"));
            detected = LanguageChoice.Normalize(detected);
            if (from != "auto") detected = from;
            else if (detected.Length == 0) detected = DetectLanguage(text);
            Dict data = Obj("source", text, "translated", translated, "command", command, "sourceLanguage", detected, "targetLanguage", target);
            if (parts != null) data["segments"] = translatedParts;
            string retryTitle = retry == "retry-text" ? T("重新翻译", "Translate again") : retry == "start-region" ? T("重新截图", "Capture again") : T("重新读取", "Read again");
            object[] actions = { Obj("id", "copy-translation", "title", T("复制译文", "Copy translation"), "key", "Enter"),
                Obj("id", "copy-source", "title", T("复制原文", "Copy source"), "key", "Ctrl+Enter"), Obj("id", retry, "title", retryTitle, "key", "Shift+Enter") };
            return new object[] {
                Obj("id", "translation", "title", translated, "subtitle", "→ " + language + (detected.Length > 0 ? " · " + detected : ""), "icon", "glyph:U+E8C1", "copy", translated, "score", 90, "data", data, "actions", actions),
                Obj("id", "source", "title", text, "subtitle", T("原文 · Ctrl+Enter 复制", "Source · Ctrl+Enter copies"), "icon", "glyph:U+E8C1", "copy", text, "score", 80, "data", data, "actions", actions)
            };
        }
        catch (OperationCanceledException) { return new object[] { Row("error-translation", T("翻译超时", "Translation timed out"), T("服务没有及时回答", "The service did not answer in time")) }; }
        catch (HttpRequestException ex)
        {
            Log("http: " + ex.GetBaseException().Message);
            return new object[] { Row("error-translation", T("翻译失败", "Translation failed"), T("连不上服务，检查网络或代理", "Cannot reach the service; check the network or proxy")) };
        }
        catch (Exception ex) { return new object[] { Row("error-translation", T("翻译失败", "Translation failed"), ex.Message) }; }
    }
    private static string[] TranslateTrialParts(string[] parts, string target, string from, string proxy)
    {
        // Reuse one connection pool and deadline, with bounded request concurrency.
        using (var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(8)))
        using (var client = FreeServices.CreateClient(proxy))
        using (var slots = new SemaphoreSlim(4))
        {
            var tasks = parts.Select(async part => {
                await slots.WaitAsync(cancellation.Token).ConfigureAwait(false);
                try { return await TranslateTrialAsync(part, target, from, client, cancellation).ConfigureAwait(false); }
                finally { slots.Release(); }
            }).ToArray();
            return Task.WhenAll(tasks).GetAwaiter().GetResult();
        }
    }
    private static FreeTranslation TranslateDeepLBlocks(ServiceProfile service, string[] parts, string from, string target)
    {
        string endpoint = service.Endpoint.TrimEnd('/');
        if (endpoint.Length == 0) endpoint = "https://" + (service.ApiKey.EndsWith(":fx") ? "api-free.deepl.com" : "api.deepl.com");
        if (!endpoint.EndsWith("/v2/translate")) endpoint += "/v2/translate";
        Uri uri;
        if (!Uri.TryCreate(endpoint, UriKind.Absolute, out uri) || (uri.Scheme != "http" && uri.Scheme != "https")) throw new Exception(T("接口地址不是有效的 HTTP URL", "The endpoint is not a valid HTTP URL"));
        using (var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(8)))
        using (var client = FreeServices.CreateClient(service.Proxy))
        using (var slots = new SemaphoreSlim(4))
        {
            var tasks = Enumerable.Range(0, (parts.Length + 49) / 50).Select(async index => {
                await slots.WaitAsync(cancellation.Token).ConfigureAwait(false);
                try
                {
                    string[] batch = parts.Skip(index * 50).Take(50).ToArray();
                    Dict body = Obj("text", batch, "target_lang", target.ToUpperInvariant());
                    if (from != "auto") body["source_lang"] = from.ToUpperInvariant();
                    var json = new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 };
                    using (var request = new HttpRequestMessage(HttpMethod.Post, uri))
                    {
                        request.Headers.ExpectContinue = false;
                        request.Headers.TryAddWithoutValidation("Authorization", "DeepL-Auth-Key " + service.ApiKey);
                        request.Content = new StringContent(json.Serialize(body), Encoding.UTF8, "application/json");
                        using (var response = await client.SendAsync(request, cancellation.Token).ConfigureAwait(false))
                        {
                            if (!response.IsSuccessStatusCode) throw new Exception("DeepL HTTP " + (int)response.StatusCode);
                            string answer = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                            object[] rows = Get(json.DeserializeObject(answer), "translations") as object[];
                            if (rows == null || rows.Length != batch.Length) throw new Exception("DeepL returned incomplete text blocks");
                            string[] translations = rows.Select(x => Str(Get(x, "text"))).ToArray();
                            if (translations.Any(String.IsNullOrWhiteSpace)) throw new Exception("DeepL returned empty text blocks");
                            return new FreeTranslation { Parts = translations, Detected = Str(Get(rows[0], "detected_source_language")) };
                        }
                    }
                }
                finally { slots.Release(); }
            }).ToArray();
            FreeTranslation[] results = Task.WhenAll(tasks).GetAwaiter().GetResult();
            string[] translated = results.SelectMany(x => x.Parts).ToArray();
            return new FreeTranslation { Text = String.Join("\n", translated), Parts = translated, Detected = results[0].Detected };
        }
    }
    private static string TranslateTrial(string text, string target, string from, string proxy)
    {
        using (var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(8)))
        using (var client = FreeServices.CreateClient(proxy))
            return TranslateTrialAsync(text, target, from, client, cancellation).GetAwaiter().GetResult();
    }
    private static async Task<string> TranslateTrialAsync(string text, string target, string from, HttpClient sharedClient, CancellationTokenSource sharedCancellation)
    {
        string source = from == "auto" ? DetectLanguage(text) : from;
        if (source == "zh") source = "zh-CN";
        string destination = target == "zh" ? "zh-CN" : target;
        if (source == destination) return text;
        var chunks = new List<string>();
        var current = new StringBuilder();
        int bytes = 0;
        var elements = System.Globalization.StringInfo.GetTextElementEnumerator(text);
        while (elements.MoveNext())
        {
            string element = elements.GetTextElement();
            int length = Encoding.UTF8.GetByteCount(element);
            if (length > 500) throw new Exception(T("文字片段过长，超出试用接口限制", "A text element exceeds the trial API limit"));
            if (bytes + length > 500) { chunks.Add(current.ToString()); current.Clear(); bytes = 0; }
            current.Append(element); bytes += length;
        }
        if (current.Length > 0) chunks.Add(current.ToString());
        var translations = new List<string>();
        {
            foreach (string chunk in chunks)
            {
                string url = "https://api.mymemory.translated.net/get?q=" + Uri.EscapeDataString(chunk) + "&langpair=" + Uri.EscapeDataString(source + "|" + destination);
                sharedCancellation.Token.ThrowIfCancellationRequested();
                if (trialUsage != null) trialUsage.RecordSubmission(chunk);
                using (var response = await sharedClient.GetAsync(url, sharedCancellation.Token).ConfigureAwait(false))
                {
                    if (!response.IsSuccessStatusCode) throw new Exception("MyMemory HTTP " + (int)response.StatusCode);
                    string answer = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                    object decoded = new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 }.DeserializeObject(answer);
                    if (Convert.ToInt32(Get(decoded, "responseStatus")) != 200)
                        throw new Exception(T("MyMemory 试用服务暂不可用或额度已用完", "MyMemory is unavailable or the trial quota is exhausted"));
                    string translated = System.Net.WebUtility.HtmlDecode(Str(Get(Get(decoded, "responseData"), "translatedText"))).Trim();
                    if (translated.Length == 0) throw new Exception(T("试用服务没有返回译文", "The trial service returned no translation"));
                    translations.Add(translated);
                }
            }
        }
        return String.Join("\n", translations);
    }
    private static string Short(string text) { string one = Regex.Replace(text, @"\s+", " "); return one.Length > 100 ? one.Substring(0, 100) + "…" : one; }
    [DllImport("user32.dll")] private static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("shcore.dll")] private static extern int SetProcessDpiAwareness(int value);
    [DllImport("user32.dll")] private static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out NativeRect rect);
    [StructLayout(LayoutKind.Sequential)] private struct NativeRect { public int Left, Top, Right, Bottom; }
    private static void SetDpi()
    {
        dpiBefore = RectText(SystemInformation.VirtualScreen);
        bool ok = false;
        try { ok = SetProcessDpiAwarenessContext(new IntPtr(-4)); } catch { }
        if (!ok) { try { ok = SetProcessDpiAwareness(2) == 0; } catch { } }
        if (!ok) { try { ok = SetProcessDPIAware(); } catch { } }
        dpiAfter = RectText(SystemInformation.VirtualScreen);
        dpiMode = ok ? "per-monitor-v2" : "inherited";
        Log("dpi: mode=" + dpiMode + " before=" + dpiBefore + " after=" + dpiAfter);
    }
    private static string RectText(Rectangle rect) { return String.Format("{0},{1} {2}x{3}", rect.X, rect.Y, rect.Width, rect.Height); }
    private static string ShotsDir() { string path = Path.Combine(dataDir, "shots"); Directory.CreateDirectory(path); return path; }
    private static Capture CaptureRect(Rectangle rect, string kind)
    {
        using (var bitmap = new Bitmap(rect.Width, rect.Height, PixelFormat.Format24bppRgb))
        {
            using (Graphics g = Graphics.FromImage(bitmap)) g.CopyFromScreen(rect.Location, Point.Empty, rect.Size, CopyPixelOperation.SourceCopy);
            var colors = new HashSet<int>();
            for (int y = 0; y < rect.Height; y += Math.Max(1, rect.Height / 32))
                for (int x = 0; x < rect.Width; x += Math.Max(1, rect.Width / 32)) colors.Add(bitmap.GetPixel(x, y).ToArgb());
            double factor = Math.Min(1.0, 8000.0 / Math.Max(rect.Width, rect.Height));
            byte[] png;
            using (var stream = new MemoryStream())
            {
                if (factor < 1)
                {
                    using (var scaled = new Bitmap(bitmap, Math.Max(2, (int)Math.Round(rect.Width * factor)), Math.Max(2, (int)Math.Round(rect.Height * factor))))
                        scaled.Save(stream, ImageFormat.Png);
                }
                else bitmap.Save(stream, ImageFormat.Png);
                png = stream.ToArray();
            }
            var capture = new Capture { Rect = rect, Png = png, Width = Math.Max(2, (int)Math.Round(rect.Width * factor)), Height = Math.Max(2, (int)Math.Round(rect.Height * factor)) };
            string name = DateTime.Now.ToString("yyyyMMdd-HHmmss-fff") + "-" + kind, directory = ShotsDir();
            File.WriteAllBytes(Path.Combine(directory, name + ".png"), png);
            File.WriteAllText(Path.Combine(directory, name + ".json"), Json.Serialize(Obj("time", DateTime.Now.ToString("o"), "kind", kind,
                "rect", Obj("x", rect.X, "y", rect.Y, "width", rect.Width, "height", rect.Height), "width", capture.Width, "height", capture.Height,
                "bytes", png.Length, "colors", colors.Count, "scaled", factor < 1, "dpi", dpiMode, "dpiBefore", dpiBefore, "dpiAfter", dpiAfter,
                "virtual", RectText(SystemInformation.VirtualScreen), "screens", Screen.AllScreens.Select(x => RectText(x.Bounds)).ToArray())), new UTF8Encoding(false));
            foreach (string old in Directory.GetFiles(directory, "*.png").Where(x => !Path.GetFileName(x).StartsWith("selftest-")).OrderByDescending(x => x).Skip(30))
            { try { File.Delete(old); File.Delete(Path.ChangeExtension(old, ".json")); } catch { } }
            Log("shot saved: " + name + " colors=" + colors.Count + " image=" + capture.Width + "x" + capture.Height);
            return capture;
        }
    }
    private static OcrEngine CreateEngine()
    {
        var available = OcrEngine.AvailableRecognizerLanguages;
        foreach (string tag in Windows.System.UserProfile.GlobalizationPreferences.Languages)
        {
            if (!Regex.IsMatch(tag, "^(zh|ja|ko)")) continue;
            var wanted = new Language(tag);
            foreach (Language language in available)
                if (language.LanguageTag.Substring(0, 2) == tag.Substring(0, 2) && language.Script == wanted.Script)
                { OcrEngine engine = OcrEngine.TryCreateFromLanguage(language); if (engine != null) return engine; }
        }
        return OcrEngine.TryCreateFromUserProfileLanguages()
            ?? available.Select(OcrEngine.TryCreateFromLanguage).FirstOrDefault(x => x != null);
    }
    private static bool Spaceless(char c) { return c >= 0x2e80 && c <= 0x9fff || c >= 0xf900 && c <= 0xfaff || c >= 0xff00 && c <= 0xffef || Char.IsSurrogate(c); }
    private static T Await<T>(object operation)
    {
        // Windows ships split WinMDs, while the framework helper references the
        // SDK's merged Windows.winmd. Resolve its generic projection at runtime.
        var method = typeof(System.WindowsRuntimeSystemExtensions).GetMethods().First(x => x.Name == "AsTask"
            && x.IsGenericMethodDefinition && x.GetParameters().Length == 1 && x.GetParameters()[0].ParameterType.Name == "IAsyncOperation`1");
        var task = (Task<T>)method.MakeGenericMethod(typeof(T)).Invoke(null, new[] { operation });
        return task.GetAwaiter().GetResult();
    }
    private static Recognition Recognize(byte[] png)
    {
        OcrEngine engine = CreateEngine();
        if (engine == null) throw new Exception(T("没有可用的 Windows OCR 识别语言", "No Windows OCR language is installed"));
        Size originalSize;
        using (var stream = new MemoryStream(png))
        using (Image image = Image.FromStream(stream)) originalSize = image.Size;
        Recognition result = RecognizeOnce(png, engine, originalSize, 1, 0);
        if (result.Lines.Length > 0 && originalSize.Height <= 800 &&
            (originalSize.Width + 12) * 2 <= OcrEngine.MaxImageDimension &&
            result.Lines.Select(x => x.Box.Height).OrderBy(x => x).ElementAt(result.Lines.Length / 2) < 22)
        {
            Recognition enlarged = RecognizeEnlarged(png, engine, originalSize, 2, 6);
            if (enlarged.Lines.Length > 0 && Regex.Matches(enlarged.Text, @"\p{L}").Count >= Regex.Matches(result.Text, @"\p{L}").Count * .8)
            {
                TextLine[] refined = SpatialText.Refine(result.Lines, enlarged.Lines);
                result = new Recognition { Text = String.Join("\n", refined.Select(x => x.Text)), Lines = refined };
            }
        }
        if (result.Lines.Length > 0 && DetectLanguage(result.Text) == "en" && !engine.RecognizerLanguage.LanguageTag.StartsWith("en", StringComparison.OrdinalIgnoreCase))
        {
            Language english = OcrEngine.AvailableRecognizerLanguages.FirstOrDefault(x => x.LanguageTag.StartsWith("en", StringComparison.OrdinalIgnoreCase));
            OcrEngine alternate = english == null ? null : OcrEngine.TryCreateFromLanguage(english);
            if (alternate != null)
            {
                Recognition candidate = RecognizeOnce(png, alternate, originalSize, 1, 0);
                int originalLatin = Regex.Matches(result.Text, @"[A-Za-z]").Count;
                int candidateLatin = Regex.Matches(candidate.Text, @"[A-Za-z]").Count;
                bool chineseLine = result.Lines.Any(x => Regex.Matches(x.Text, @"[\u3400-\u9fff]").Count >= 3 && DetectLanguage(x.Text) == "zh");
                if (!chineseLine && candidate.Lines.Length > 0 && candidateLatin > 0 && candidateLatin >= originalLatin * 0.75 && DetectLanguage(candidate.Text) == "en")
                    result = candidate;
            }
        }
        if (result.Lines.Length > 0 || Math.Max(originalSize.Width, originalSize.Height) > 2048) return result;

        // Short words in tight, small crops can disappear from the OCR detector.
        // Retry with a border and larger glyphs, then map boxes back to the crop.
        return RecognizeEnlarged(png, engine, originalSize, 3, 12);
    }
    private static Recognition RecognizeEnlarged(byte[] png, OcrEngine engine, Size originalSize, int scale, int border)
    {
        using (var stream = new MemoryStream(png))
        using (var original = new Bitmap(stream))
        using (var expanded = new Bitmap((original.Width + border * 2) * scale, (original.Height + border * 2) * scale, PixelFormat.Format24bppRgb))
        {
            using (Graphics graphics = Graphics.FromImage(expanded))
            {
                graphics.Clear(original.GetPixel(0, 0));
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                graphics.DrawImage(original, border * scale, border * scale, original.Width * scale, original.Height * scale);
            }
            using (var output = new MemoryStream())
            {
                expanded.Save(output, ImageFormat.Png);
                Log("ocr: retry scale=" + scale + " border=" + border + " image=" + originalSize.Width + "x" + originalSize.Height);
                return RecognizeOnce(output.ToArray(), engine, originalSize, scale, border * scale);
            }
        }
    }
    private static Recognition RecognizeOnce(byte[] png, OcrEngine engine, Size originalSize, float scale, float padding)
    {
        using (var stream = new InMemoryRandomAccessStream())
        {
            using (var writer = new DataWriter(stream))
            { writer.WriteBytes(png); Await<uint>(writer.StoreAsync()); writer.DetachStream(); }
            stream.Seek(0);
            BitmapDecoder decoder = Await<BitmapDecoder>(BitmapDecoder.CreateAsync(stream));
            using (SoftwareBitmap bitmap = Await<SoftwareBitmap>(decoder.GetSoftwareBitmapAsync()))
            {
                OcrResult result = Await<OcrResult>(engine.RecognizeAsync(bitmap));
                var wordBoxes = new List<TextLine>();
                int words = 0, chars = 0;
                foreach (OcrLine line in result.Lines)
                {
                    foreach (OcrWord word in line.Words)
                    {
                        var r = word.BoundingRect;
                        var b = RectangleF.Intersect(
                            new RectangleF(((float)r.X - padding) / scale, ((float)r.Y - padding) / scale, (float)r.Width / scale, (float)r.Height / scale),
                            new RectangleF(PointF.Empty, originalSize));
                        if (word.Text.Length == 0) continue;
                        if (b.Width > 0 && b.Height > 0) wordBoxes.Add(new TextLine { Text = word.Text, Box = b });
                        words++; chars += word.Text.Length;
                    }
                }
                TextLine[] lines = SpatialText.Group(wordBoxes);
                Log("ocr: engine=" + engine.RecognizerLanguage.LanguageTag + " lines=" + lines.Length + " words=" + words + " rawChars=" + chars);
                return new Recognition { Text = String.Join("\n", lines.Select(x => x.Text)), Lines = lines };
            }
        }
    }
    private static void Shot(string kind)
    {
        if (inputTest) { using (Triggers.Busy()) Log("input-test: shot"); return; }
        using (Triggers.Busy())
        try
        {
            Results.Remove("shot");
            ClosePin();
            Rectangle rect;
            if (kind == "region")
            {
                using (var mask = new RegionMask())
                { if (mask.ShowDialog() != DialogResult.OK) return; rect = mask.Selection; }
                Thread.Sleep(80);
            }
            else if (kind == "window")
            {
                NativeRect native;
                if (!GetWindowRect(GetForegroundWindow(), out native)) throw new Exception("No foreground window");
                rect = Rectangle.Intersect(Rectangle.FromLTRB(native.Left, native.Top, native.Right, native.Bottom), SystemInformation.VirtualScreen);
            }
            else rect = Setting("shotMode") == "single" ? Screen.FromPoint(Cursor.Position).Bounds : SystemInformation.VirtualScreen;
            if (rect.Width < 2 || rect.Height < 2) return;
            Capture capture = CaptureRect(rect, kind);
            OpenPin(capture);
            Recognition ocr;
            try { ocr = Recognize(capture.Png); }
            catch (Exception ex) { UpdatePin("", "", T("识别失败：", "Recognition failed: ") + ex.Message, null); Show("shot", new object[] { Row("error-ocr", T("识别文字失败", "Text recognition failed"), ex.Message) }); return; }
            if (ocr.Text.Length == 0) { UpdatePin("", "", T("没有识别到文字", "No text recognized"), null); Show("shot", new object[] { Row("error-ocr", T("没有识别到文字", "No text recognized"), T("换一块区域再试", "Try another region")) }); return; }
            UpdatePin(ocr.Text, "", T("正在翻译…", "Translating…"), ocr.Lines, true, DetectLanguage(ocr.Text), TargetLanguage(ocr.Text));
            CompletePin(pin, ocr.Text, ocr.Lines, "auto", TargetLanguage(ocr.Text));
        }
        catch (Exception ex) { Log("shot flow failed: " + ex.Message); Show("shot", new object[] { Row("error-capture", T("截图失败", "Capture failed"), ex.Message) }); }
    }
    private static void VerifyHotkeys()
    {
        Dict previousSettings = settings;
        string[] previousKeys = registeredHotkeys;
        TextWriter previousOutput = Console.Out;
        var output = new StringWriter();
        try
        {
            Console.SetOut(output);
            settings = Obj("provider", "google", "shotHotkey", "Alt+Shift+T", "clipHotkey", "Alt+Shift+C");
            registeredHotkeys = null;
            Input.Add(Obj("id", "p" + (sequence + 1), "result", Obj()));
            RegisterHotkeys();
            settings["provider"] = "mymemory";
            RegisterHotkeys();
            string[] messages = output.ToString().Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
            if (messages.Length != 2 || HotkeyCommand("Alt+Shift+T") != "shot" || HotkeyCommand("alt+shift+c") != "clip")
                throw new Exception("Hotkeys were re-registered or blocked after a service change");
            settings["shotHotkey"] = "Alt+Shift+S";
            Input.Add(Obj("id", "p" + (sequence + 1), "result", Obj()));
            RegisterHotkeys();
            if (HotkeyCommand("Alt+Shift+T").Length != 0 || HotkeyCommand("Alt+Shift+S") != "shot" || HotkeyCommand("").Length != 0)
                throw new Exception("Changed hotkey routing failed");
            registeredHotkeys = null;
            Input.Add(Obj("id", "p" + (sequence + 1), "error", Obj("code", -1)));
            RegisterHotkeys();
            if (registeredHotkeys != null) throw new Exception("Failed hotkey registration was cached as successful");
            Input.Add(Obj("id", "p" + (sequence + 1), "result", Obj()));
            RegisterHotkeys();
            if (registeredHotkeys == null) throw new Exception("Hotkey registration was not retried after failure");
        }
        finally { Console.SetOut(previousOutput); settings = previousSettings; registeredHotkeys = previousKeys; output.Dispose(); }
    }
    private static void OpenPin(Capture capture)
    {
        var ready = new ManualResetEventSlim(false);
        Exception failure = null;
        pinThread = new Thread(delegate()
        {
            try
            {
                pin = new PinWindow(capture, delegate { Enqueue("internal-retry", null); },
                    delegate(PinWindow window, string text, string from, string to, TextLine[] boxes) {
                        Enqueue("internal-translate", Obj("window", window, "text", text, "from", from, "to", to, "lines", boxes));
                    }, delegate { Enqueue("internal-settings", null); });
                capture = null;
                pin.Shown += delegate { ready.Set(); };
                Application.Run(pin);
            }
            catch (Exception ex) { failure = ex; ready.Set(); Log("pin: could not open: " + ex.Message); }
            finally { if (pin != null) pin.Dispose(); }
        }) { IsBackground = true, Name = "translation-pin" };
        pinThread.SetApartmentState(ApartmentState.STA);
        pinThread.Start();
        if (!ready.Wait(3000)) throw new Exception("Pin window did not open");
        // The event can still be used by the UI thread if startup times out.
        ready.Dispose();
        if (failure != null) throw failure;
        Log("pin: opened");
    }
    private static void Enqueue(string method, Dict parameters)
    {
        try { QueueInput(Obj("method", method, "params", parameters)); } catch (InvalidOperationException) { }
    }
    private static void TranslatePin(object p)
    {
        PinWindow window = Get(p, "window") as PinWindow;
        if (window == null || window != pin || window.IsDisposed) return;
        CompletePin(window, Str(Get(p, "text")), Get(p, "lines") as TextLine[], Str(Get(p, "from")), Str(Get(p, "to")));
    }
    private static void CompletePin(PinWindow window, string text, TextLine[] boxes, string from, string to)
    {
        object[] items = Translate(text, "start-region", "shot", null, from, to, boxes == null ? null : boxes.Select(x => x.Text).ToArray());
        object result = Get(items[0], "data");
        string translation = Str(Get(result, "translated"));
        string progress = translation.Length > 0 ? "" : Str(Get(items[0], "title")) + "\n" + Str(Get(items[0], "subtitle"));
        if (window == pin && window != null && !window.IsDisposed)
            UpdatePin(text, translation, progress, boxes, false, Str(Get(result, "sourceLanguage")), to, Get(result, "segments") as string[]);
        Show("shot", items);
    }
    private static void UpdatePin(string source, string translated, string status, TextLine[] lines, bool busy = false, string detected = "", string target = "", string[] segments = null)
    {
        PinWindow window = pin;
        if (window == null || window.IsDisposed || !window.IsHandleCreated) return;
        try { window.BeginInvoke((Action)delegate { if (!window.IsDisposed) window.UpdateText(source, translated, status, lines, busy, detected, target, segments); }); }
        catch (InvalidOperationException) { }
    }
    private static void OpenSettings()
    {
        if (configWindow != null && !configWindow.IsDisposed)
        {
            try { configWindow.BeginInvoke((Action)configWindow.Present); } catch (InvalidOperationException) { }
            return;
        }
        if (configThread != null && configThread.IsAlive) return;
        IntPtr owner = GetForegroundWindow();
        uint ownerId;
        GetWindowThreadProcessId(owner, out ownerId);
        try
        {
            using (var process = System.Diagnostics.Process.GetProcessById((int)ownerId))
                if (!process.ProcessName.Equals("WinShun", StringComparison.OrdinalIgnoreCase)) owner = IntPtr.Zero;
        }
        catch (ArgumentException) { owner = IntPtr.Zero; }
        configThread = new Thread(delegate()
        {
            try
            {
                configWindow = new SettingsWindow(profiles, trialUsage, owner);
                Application.Run(configWindow);
            }
            catch (Exception ex) { Log("settings: " + ex.Message); }
            finally { if (configWindow != null) configWindow.Dispose(); }
        }) { IsBackground = true, Name = "translation-settings" };
        configThread.SetApartmentState(ApartmentState.STA); configThread.Start();
    }
    private static void RefreshAppearance()
    {
        PinWindow image = pin; SettingsWindow config = configWindow;
        if (image != null && !image.IsDisposed && image.IsHandleCreated)
            try { image.BeginInvoke((Action)image.ApplyTheme); } catch (InvalidOperationException) { }
        if (config != null && !config.IsDisposed && config.IsHandleCreated)
            try { config.BeginInvoke((Action)config.ApplyTheme); } catch (InvalidOperationException) { }
    }
    private static void CloseSettings()
    {
        SettingsWindow window = configWindow;
        if (window != null && !window.IsDisposed && window.IsHandleCreated)
        { try { window.BeginInvoke((Action)window.Close); } catch (InvalidOperationException) { } }
        if (configThread != null && configThread != Thread.CurrentThread) configThread.Join(3000);
        configWindow = null; configThread = null;
    }
    private static void ClosePin()
    {
        PinWindow window = pin;
        Thread thread = pinThread;
        if (window != null && !window.IsDisposed && window.IsHandleCreated)
        { try { window.BeginInvoke((Action)window.Close); } catch (InvalidOperationException) { } }
        if (thread != null && thread != Thread.CurrentThread)
        {
            if (!thread.Join(3000)) { Log("pin: close is still pending"); return; }
            Log("pin: closed");
        }
        pin = null; pinThread = null; dirty = true;
    }
    private static int SelfTest()
    {
        string directory = ShotsDir(), inputPath = Path.Combine(directory, "selftest-input.png"), outputPath = Path.Combine(directory, "selftest-overlay.png");
        using (var image = new Bitmap(900, 260))
        using (Graphics g = Graphics.FromImage(image))
        using (var font = new Font("Microsoft YaHei UI", 24))
        {
            g.Clear(Color.White);
            g.DrawString("你好，世界。", font, Brushes.Black, 30, 20);
            g.DrawString("Screenshot translation", font, Brushes.Black, 30, 90);
            g.DrawString("Hello world", font, Brushes.Black, 30, 160);
            image.Save(inputPath, ImageFormat.Png);
        }
        try
        {
            TriggerGate.Verify();
            VerifyHotkeys();
            foreach (var sample in new[] {
                Obj("text", "Pull requests 朵 Agents Actions Projects Security and quality Insights", "language", "en"),
                Obj("text", "Hello world あ", "language", "en"), Obj("text", "Hello world 한", "language", "en"),
                Obj("text", "这是中文的截图翻译 OpenAI", "language", "zh"), Obj("text", "こんにちは世界", "language", "ja"),
                Obj("text", "안녕하세요 세계", "language", "ko"), Obj("text", "Привет мир", "language", "ru") })
                if (DetectLanguage((string)sample["text"]) != (string)sample["language"]) throw new Exception("Dominant language detection failed");
            if (TargetLanguage("Hello world 朵") != "zh" || TargetLanguage("你好世界") != "en") throw new Exception("Automatic translation direction failed");
            FreeServices.Verify();
            SpatialText.Verify();
            Recognition ocr = Recognize(File.ReadAllBytes(inputPath));
            if (ocr.Lines.Length != 3 || ocr.Lines.Any(x => x.Box.IsEmpty || x.Box.Left < 0 || x.Box.Top < 0 || x.Box.Right > 900 || x.Box.Bottom > 260)) throw new Exception("OCR boxes failed");
            using (var image = new Bitmap(inputPath))
            using (Bitmap overlay = PinWindow.Render(image, ocr.Lines, "Hello, world.\n截图译文\n你好世界", 1.0f))
                overlay.Save(outputPath, ImageFormat.Png);
            var capture = new Capture { Rect = new Rectangle(0, 0, 900, 260), Png = File.ReadAllBytes(inputPath), Width = 900, Height = 260 };
            PinWindow.VerifyHiddenDoubleClick(capture);
            PinWindow.VerifyHiddenInteractions(capture, ocr.Lines, directory);
            PinWindow.VerifySpatialLayout(directory);
            string profileTest = Path.Combine(Path.GetTempPath(), "screenshot-profiles-" + Guid.NewGuid().ToString("N"));
            try { MyMemoryUsage.Verify(profileTest); ServiceProfiles.Verify(profileTest, directory); }
            finally
            {
                string testFile = Path.Combine(profileTest, "services.json");
                if (File.Exists(testFile)) File.Delete(testFile);
                if (Directory.Exists(profileTest)) Directory.Delete(profileTest);
            }
            TextWriter previous = Console.Out;
            var output = new StringWriter();
            try { Console.SetOut(output); Show("shot", new object[0]); }
            finally { Console.SetOut(previous); }
            Results.Remove("shot");
            if (output.GetStringBuilder().Length != 0) throw new Exception("Screenshot result invoked a host tool");
            Console.WriteLine("selftest: lines=3 boxes=3 inBounds=True mode=lines segments=3 result=PASS");
            Console.WriteLine("selftest: hiddenDoubleClick=PASS screenshotNoLauncher=PASS");
            Console.WriteLine("selftest: hiddenEsc=PASS languageSwitch=PASS swap=PASS comparison=PASS loading=PASS profilesEncrypted=PASS draftsIsolated=PASS");
            Console.WriteLine("selftest: dominantLanguage=PASS autoDirection=PASS freeServiceResponses=PASS usagePersistence=PASS usageDayReset=PASS removedService=PASS");
            Console.WriteLine("selftest: spatialReadingOrder=PASS mappedMultiline=PASS iconsPreserved=PASS compactFallback=PASS longTextScroll=PASS");
            Console.WriteLine("selftest: hostChannel=PASS commonProxy=PASS profileSaveKeepsChannel=PASS bottomToolbar=PASS workingAreaBounds=PASS");
            Console.WriteLine("selftest: flatConfigurationTabs=PASS configurationKeepsChannel=PASS compactSettingsLayout=PASS settingsTopMost=PASS hiddenSettingsOwner=PASS");
            Console.WriteLine("selftest: crowdedBlockKeepsPositions=PASS numericBadgePreserved=PASS hotkeysWithoutBlackout=PASS unchangedHotkeysNotReregistered=PASS");
            Console.WriteLine("selftest: busyTriggersDropped=PASS queuedTriggersExpired=PASS freshTriggerAfterCompletion=PASS nestedBusyScope=PASS");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine("selftest: FAIL " + ex.Message); return 1; }
    }
    private sealed class Pending { internal object[] Items; internal DateTime Time; }
    internal sealed class Capture { internal Rectangle Rect; internal byte[] Png; internal int Width, Height; }
    internal sealed class TextLine { internal string Text; internal RectangleF Box; internal float GlyphHeight; }
    private sealed class Recognition { internal string Text; internal TextLine[] Lines; }
}

internal sealed class TriggerGate
{
    internal sealed class Receipt { internal long Epoch, Ticks; internal bool Busy; }
    private readonly object gate = new object();
    private long epoch;
    private int depth;
    internal Receipt Receive()
    {
        lock (gate) return new Receipt { Epoch = epoch, Ticks = System.Diagnostics.Stopwatch.GetTimestamp(), Busy = depth > 0 };
    }
    internal bool Current(Receipt receipt, out double age, out string reason)
    {
        age = receipt == null ? 0 : (System.Diagnostics.Stopwatch.GetTimestamp() - receipt.Ticks) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
        lock (gate)
        {
            reason = receipt == null ? "missing-receipt" : receipt.Busy || depth > 0 ? "busy"
                : receipt.Epoch != epoch ? "completed-work" : age > 1000 ? "expired" : "";
            return reason.Length == 0;
        }
    }
    internal IDisposable Busy()
    {
        lock (gate) { if (depth++ == 0) epoch++; }
        return new Lease(this);
    }
    private sealed class Lease : IDisposable
    {
        private TriggerGate owner;
        internal Lease(TriggerGate owner) { this.owner = owner; }
        public void Dispose()
        {
            TriggerGate value = Interlocked.Exchange(ref owner, null);
            if (value != null) lock (value.gate) { if (--value.depth == 0) value.epoch++; }
        }
    }
    internal static void Verify()
    {
        var gate = new TriggerGate();
        double age; string reason;
        Receipt before = gate.Receive(), pending = gate.Receive(), during;
        if (!gate.Current(before, out age, out reason)) throw new Exception("Fresh trigger was blocked");
        using (gate.Busy())
        {
            during = gate.Receive();
            if (gate.Current(before, out age, out reason) || gate.Current(during, out age, out reason))
                throw new Exception("Busy trigger was accepted");
            using (gate.Busy()) { }
            if (!gate.Receive().Busy) throw new Exception("Nested work released the busy gate");
        }
        if (gate.Current(pending, out age, out reason) || gate.Current(during, out age, out reason))
            throw new Exception("Queued trigger survived completed work");
        Receipt fresh = gate.Receive();
        if (!gate.Current(fresh, out age, out reason)) throw new Exception("Fresh trigger after completion was blocked");
        fresh.Ticks -= System.Diagnostics.Stopwatch.Frequency * 2;
        if (gate.Current(fresh, out age, out reason) || reason != "expired") throw new Exception("Old trigger did not expire");
    }
}

internal sealed class RegionMask : Form
{
    private Point start;
    private bool selecting;
    internal Rectangle Selection;
    internal RegionMask()
    {
        FormBorderStyle = FormBorderStyle.None; StartPosition = FormStartPosition.Manual;
        Bounds = SystemInformation.VirtualScreen; BackColor = Color.Black; Opacity = 0.28;
        TopMost = true; ShowInTaskbar = false; KeyPreview = true; Cursor = Cursors.Cross; DoubleBuffered = true;
    }
    protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != MouseButtons.Left) return; start = e.Location; selecting = true; }
    protected override void OnMouseMove(MouseEventArgs e)
    { base.OnMouseMove(e); if (!selecting) return; Selection = Rectangle.FromLTRB(Math.Min(start.X, e.X), Math.Min(start.Y, e.Y), Math.Max(start.X, e.X), Math.Max(start.Y, e.Y)); Invalidate(); }
    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e); if (!selecting || e.Button != MouseButtons.Left) return;
        Selection = Rectangle.FromLTRB(Math.Min(start.X, e.X), Math.Min(start.Y, e.Y), Math.Max(start.X, e.X), Math.Max(start.Y, e.Y));
        if (Selection.Width < 2 || Selection.Height < 2) { DialogResult = DialogResult.Cancel; return; }
        Selection = new Rectangle(PointToScreen(Selection.Location), Selection.Size); DialogResult = DialogResult.OK;
    }
    protected override void OnKeyDown(KeyEventArgs e) { base.OnKeyDown(e); if (e.KeyCode == Keys.Escape) DialogResult = DialogResult.Cancel; }
    protected override void OnPaint(PaintEventArgs e) { base.OnPaint(e); if (selecting) { using (var pen = new Pen(Color.White, 2)) e.Graphics.DrawRectangle(pen, Selection); } }
}

internal sealed class PinWindow : SurfaceWindow, IMessageFilter
{
    private readonly Bitmap original;
    private Bitmap overlay;
    private readonly PinCanvas picture;
    private readonly VScrollBar scroll;
    private readonly FloatingToolbar toolbar;
    private readonly LanguagePicker sourceChoice, targetChoice;
    private readonly FluentButton swap;
    private readonly FluentToggle compare;
    private readonly ToolTip tips;
    private readonly ContextMenuStrip menu;
    private string source = "", translated = "";
    private string[] translatedBlocks;
    private Program.TextLine[] lines;
    private bool updating, busy = true;
    private string detectedLanguage = "en", progress = "";
    private readonly Action<PinWindow, string, string, string, Program.TextLine[]> translateAgain;
    private Rectangle area;
    private readonly Point anchor;
    private Point dragStart, dragOrigin;
    private bool dragging;
    private bool resourcesDisposed;
    private bool filtering;
    private readonly float factor;
    internal PinWindow(Program.Capture capture, Action retry,
        Action<PinWindow, string, string, string, Program.TextLine[]> retranslate, Action settings)
    {
        area = Screen.FromRectangle(capture.Rect).WorkingArea; anchor = capture.Rect.Location;
        translateAgain = retranslate;
        float fit = Math.Min(1.0f, Math.Min((area.Width - 2) / (float)capture.Width, Math.Max(1, area.Height - 70) / (float)capture.Height));
        int width = Math.Max(1, (int)(capture.Width * fit)), height = Math.Max(1, (int)(capture.Height * fit));
        factor = width / (float)capture.Width;
        using (var stream = new MemoryStream(capture.Png))
        using (Image image = Image.FromStream(stream)) original = new Bitmap(image, width, height);
        FormBorderStyle = FormBorderStyle.None; StartPosition = FormStartPosition.Manual; TopMost = true;
        ShowInTaskbar = false; KeyPreview = true; AutoScaleMode = AutoScaleMode.None;
        BackColor = Ui.Surface;
        toolbar = new FloatingToolbar();
        sourceChoice = new LanguagePicker { Left = 8, Top = 6, Width = 134, BackColor = Ui.Surface, AccessibleName = Program.T("原文语言", "Source language") };
        targetChoice = new LanguagePicker { Left = 180, Top = 6, Width = 118, BackColor = Ui.Surface, AccessibleName = Program.T("目标语言", "Target language") };
        sourceChoice.Items.Add(new LanguageChoice("auto", Program.T("检测语言", "Detect")));
        foreach (string code in LanguageChoice.Codes)
        { sourceChoice.Items.Add(new LanguageChoice(code)); targetChoice.Items.Add(new LanguageChoice(code)); }
        sourceChoice.SelectedIndex = 0; targetChoice.SelectedIndex = 0;
        swap = SymbolButton("\uE8AB", 144);
        compare = new FluentToggle { Left = 312, Top = 6, Text = Program.T("对照", "Compare"), BackColor = Ui.Surface };
        FluentButton config = SymbolButton("\uE713", 410), close = SymbolButton("\uE8BB", 446);
        close.Destructive = true;
        tips = new ToolTip();
        tips.SetToolTip(swap, Program.T("交换语言", "Swap languages"));
        tips.SetToolTip(config, Program.T("翻译服务设置", "Translation services"));
        tips.SetToolTip(close, Program.T("关闭", "Close"));
        toolbar.Controls.AddRange(new Control[] { sourceChoice, swap, targetChoice, compare, config, close });
        picture = new PinCanvas { Dock = DockStyle.Fill };
        scroll = new VScrollBar { Dock = DockStyle.Right, Width = 14, Visible = false, SmallChange = 32 };
        scroll.ValueChanged += delegate { picture.Invalidate(); };
        picture.Paint += PaintImage;
        Controls.Add(picture); Controls.Add(scroll);
        LayoutImage(true);
        sourceChoice.SelectedIndexChanged += delegate { RequestTranslation(false); };
        targetChoice.SelectedIndexChanged += delegate { RequestTranslation(false); };
        swap.Click += delegate { RequestTranslation(true); };
        compare.CheckedChanged += delegate { LayoutImage(false); picture.Invalidate(); };
        config.Click += delegate { settings(); };
        close.Click += delegate { Close(); };
        menu = FluentMenu.Create();
        menu.Items.Add(Program.T("复制译文", "Copy translation"), null, delegate { Copy(translated); });
        menu.Items.Add(Program.T("复制原文", "Copy source"), null, delegate { Copy(source); });
        menu.Items.Add(Program.T("重新截图", "Capture again"), null, delegate { retry(); Close(); });
        menu.Items.Add(Program.T("关闭", "Close"), null, delegate { Close(); });
        foreach (ToolStripItem item in menu.Items) item.Height = 34;
        ContextMenuStrip = menu; picture.ContextMenuStrip = menu;
        picture.MouseDown += delegate(object sender, MouseEventArgs e) { picture.Focus(); DragDown(sender, e); }; picture.MouseMove += DragMove; picture.MouseUp += delegate { dragging = false; };
        toolbar.MouseDown += DragDown; toolbar.MouseMove += DragMove; toolbar.MouseUp += delegate { dragging = false; };
        picture.MouseDoubleClick += CloseOnDoubleClick;
        toolbar.MouseDoubleClick += CloseOnDoubleClick;
        MouseDoubleClick += CloseOnDoubleClick;
        MouseWheel += ScrollOrOpacity; picture.MouseWheel += ScrollOrOpacity;
        progress = Program.T("正在识别…", "Recognizing…");
        SetBusy(true);
        HandleCreated += delegate { Application.AddMessageFilter(this); filtering = true; };
        Shown += delegate { PositionToolbar(); toolbar.Show(this); };
        LocationChanged += delegate { PositionToolbar(); };
    }
    private FluentButton SymbolButton(string symbol, int left)
    {
        return new FluentButton { Left = left, Top = 6, Width = 34, Height = 34, Text = "", GlyphText = symbol,
            BackColor = Ui.Surface, TabStop = false };
    }
    private void SetBusy(bool value)
    {
        busy = value;
        sourceChoice.Enabled = targetChoice.Enabled = !value && source.Length > 0;
        swap.Enabled = !value && translated.Length > 0;
        picture.Animate = value; LayoutImage(false); picture.Invalidate();
    }
    private void RequestTranslation(bool reverse)
    {
        if (updating || busy || source.Length == 0) return;
        string from = ((LanguageChoice)sourceChoice.SelectedItem).Code;
        string to = ((LanguageChoice)targetChoice.SelectedItem).Code;
        string text = source;
        Program.TextLine[] blocks = lines;
        if (reverse)
        {
            if (translated.Length == 0) return;
            text = translated;
            if (translatedBlocks != null && lines != null && translatedBlocks.Length == lines.Length)
                blocks = lines.Select((line, i) => new Program.TextLine { Text = translatedBlocks[i], Box = line.Box, GlyphHeight = line.GlyphHeight }).ToArray();
            string nextTarget = from == "auto" ? detectedLanguage : from;
            from = to; to = nextTarget;
            updating = true;
            LanguageChoice.Select(sourceChoice, from); LanguageChoice.Select(targetChoice, to);
            updating = false;
        }
        progress = Program.T("正在翻译…", "Translating…"); SetBusy(true);
        translateAgain(this, text, from, to, blocks);
    }
    private void LayoutImage(bool initial)
    {
        Bitmap result = overlay ?? original;
        int contentWidth = Math.Max(result.Width, compare.Checked ? original.Width : 0);
        int contentHeight = result.Height + (compare.Checked ? original.Height + 48 : 0);
        bool error = !busy && translated.Length == 0 && progress.Length > 0;
        if (error) { contentWidth = Math.Max(260, contentWidth); contentHeight = Math.Max(100, contentHeight); }
        int availableHeight = Math.Max(50, area.Height - toolbar.Height - 14);
        float scale = Math.Min(1, (area.Width - 16) / (float)contentWidth);
        int fullHeight = Math.Max(2, (int)(contentHeight * scale));
        bool overflow = fullHeight > availableHeight - 2;
        int width = Math.Max(2, (int)(contentWidth * scale) + 2 + (overflow ? scroll.Width : 0)), height = Math.Max(2, Math.Min(availableHeight, fullHeight + 2));
        scroll.Visible = overflow;
        scroll.Maximum = Math.Max(0, fullHeight - 1); scroll.LargeChange = Math.Max(1, height - 2);
        scroll.Value = Math.Min(scroll.Value, Math.Max(0, scroll.Maximum - scroll.LargeChange + 1));
        Point origin = initial ? anchor : Location;
        Bounds = new Rectangle(Math.Max(area.Left, Math.Min(origin.X, area.Right - width)),
            Math.Max(area.Top, Math.Min(origin.Y, area.Bottom - height - toolbar.Height - 8)), width, height);
        PositionToolbar();
    }
    private void PositionToolbar()
    {
        if (toolbar == null || toolbar.IsDisposed) return;
        Point origin = new Point(Math.Max(area.Left, Math.Min(Left, area.Right - Width)),
            Math.Max(area.Top, Math.Min(Top, area.Bottom - Height - toolbar.Height - 8)));
        if (Location != origin) Location = origin;
        toolbar.Location = new Point(Math.Max(area.Left, Math.Min(Left + (Width - toolbar.Width) / 2, area.Right - toolbar.Width)), Bottom + 8);
    }
    private void PaintImage(object sender, PaintEventArgs e)
    {
        Bitmap result = overlay ?? original;
        int totalHeight = result.Height + (compare.Checked ? original.Height + 48 : 0);
        int totalWidth = Math.Max(result.Width, compare.Checked ? original.Width : 0);
        bool error = !busy && translated.Length == 0 && progress.Length > 0;
        if (error) { totalWidth = Math.Max(260, totalWidth); totalHeight = Math.Max(100, totalHeight); }
        float scale = Math.Min(1, (picture.Width - 2) / (float)totalWidth);
        int top = 1 - scroll.Value;
        using (var background = new SolidBrush(Ui.Surface)) e.Graphics.FillRectangle(background, picture.ClientRectangle);
        if (compare.Checked)
        {
            int caption = (int)(24 * scale);
            Ui.Label(e.Graphics, Program.T("译文", "Translation"), Ui.Caption, new RectangleF(10, top, picture.Width - 20, caption), Ui.Muted);
            top += caption;
        }
        Rectangle imageBox = new Rectangle(1, top, Math.Max(1, (int)(result.Width * scale)), Math.Max(1, (int)(result.Height * scale)));
        e.Graphics.DrawImage(result, imageBox);
        if (compare.Checked)
        {
            int caption = (int)(24 * scale);
            top = imageBox.Bottom;
            using (var pen = new Pen(Ui.Line)) e.Graphics.DrawLine(pen, 1, top, picture.Width - 2, top);
            Ui.Label(e.Graphics, Program.T("原文", "Original"), Ui.Caption, new RectangleF(10, top, picture.Width - 20, caption), Ui.Muted);
            e.Graphics.DrawImage(original, new Rectangle(1, top + caption, Math.Max(1, (int)(original.Width * scale)), Math.Max(1, (int)(original.Height * scale))));
        }
        if (busy || (translated.Length == 0 && progress.Length > 0))
        {
            Rectangle viewport = new Rectangle(1, 1, picture.Width - 2, picture.Height - 2);
            Rectangle visible = Rectangle.Intersect(imageBox, viewport);
            PaintProgress(e.Graphics, error || visible.IsEmpty ? viewport : visible, busy, progress, picture.Frame);
        }
        Ui.Box(e.Graphics, new RectangleF(.5f, .5f, picture.Width - 1, picture.Height - 1), Color.Transparent, Ui.InputLine, 4);
    }
    internal static void PaintProgress(Graphics g, Rectangle box, bool loading, string text, int frame)
    {
        using (var veil = new SolidBrush(Color.FromArgb(215, Color.White))) g.FillRectangle(veil, box);
        if (loading)
        {
            int size = Math.Max(4, Math.Min(18, Math.Min(box.Width, box.Height) - 4));
            int x = box.Left + (box.Width - size) / 2, y = box.Top + (box.Height - size) / 2;
            bool hasText = box.Width >= 150 && box.Height >= 52;
            if (hasText) y -= 11;
            using (var pen = new Pen(Ui.AccentFill, 2))
                g.DrawArc(pen, x, y, size, size, frame * 30, 270);
            if (hasText)
                Ui.Label(g, text, Ui.Caption, new Rectangle(box.Left + 4, y + size + 4, box.Width - 8, 22), Color.FromArgb(50, 60, 65), true);
        }
        else
        {
            Rectangle message = box; message.Inflate(-3, -2);
            using (var font = new Font("Microsoft YaHei UI", Math.Max(7, Math.Min(12, box.Height / 3)), GraphicsUnit.Pixel))
            using (var brush = new SolidBrush(Color.FromArgb(166, 43, 47)))
            using (var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center, Trimming = StringTrimming.EllipsisCharacter })
                g.DrawString(text, font, brush, message, format);
        }
    }
    private void Copy(string text) { if (text.Length == 0) return; try { Clipboard.SetText(text); } catch (ExternalException) { } }
    private void ScrollOrOpacity(object sender, MouseEventArgs e)
    {
        var handled = e as HandledMouseEventArgs;
        if (handled != null && handled.Handled) return;
        if (scroll.Visible) scroll.Value = Math.Max(0, Math.Min(scroll.Maximum - scroll.LargeChange + 1, scroll.Value - Math.Sign(e.Delta) * 64));
        else Opacity = Math.Max(0.3, Math.Min(1.0, Opacity + (e.Delta > 0 ? 0.05 : -0.05)));
        if (handled != null) handled.Handled = true;
    }
    private void DragDown(object sender, MouseEventArgs e) { if (e.Button != MouseButtons.Left) return; dragging = true; dragStart = Cursor.Position; dragOrigin = Location; }
    private void DragMove(object sender, MouseEventArgs e)
    {
        if (!dragging) return;
        Point point = Cursor.Position;
        Point next = new Point(dragOrigin.X + point.X - dragStart.X, dragOrigin.Y + point.Y - dragStart.Y);
        area = Screen.FromRectangle(new Rectangle(next, Size)).WorkingArea;
        Location = next; LayoutImage(false);
    }
    private void CloseOnDoubleClick(object sender, MouseEventArgs e) { if (e.Button == MouseButtons.Left) { dragging = false; Close(); } }
    internal static void VerifyHiddenDoubleClick(Program.Capture capture)
    {
        var raise = typeof(Control).GetMethod("OnMouseDoubleClick", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
        for (int target = 0; target < 3; target++)
        {
            using (var window = new PinWindow(capture, delegate { }, delegate { }, delegate { }))
            {
                Control control = target == 0 ? (Control)window.picture : target == 1 ? (Control)window.toolbar : window;
                raise.Invoke(control, new object[] { new MouseEventArgs(MouseButtons.Left, 2, 10, 10, 0) });
                if (!window.IsDisposed) throw new Exception("Double-click did not close the hidden pin");
            }
        }
    }
    internal static void VerifyHiddenInteractions(Program.Capture capture, Program.TextLine[] boxes, string directory)
    {
        string input = "", from = "", to = ""; int calls = 0;
        using (var window = new PinWindow(capture, delegate { },
            delegate(PinWindow pin, string text, string sourceLanguage, string targetLanguage, Program.TextLine[] lines) {
                input = text; from = sourceLanguage; to = targetLanguage; calls++;
            }, delegate { }))
        {
            if (!window.busy || window.Controls.Count != 2) throw new Exception("Pin loading layout failed");
            window.VerifyToolbarBelow();
            window.SaveHidden(Path.Combine(directory, "selftest-loading.png"));
            const string text = "Hello world\nScreenshot translation\nHello again";
            const string translation = "你好世界\n截图翻译\n再次问好";
            window.UpdateText(text, translation, "", boxes, false, "en", "zh");
            window.SaveHidden(Path.Combine(directory, "selftest-pin.png"));
            int singleHeight = window.Height;
            window.compare.Checked = true;
            if (window.Height <= singleHeight || window.overlay == null) throw new Exception("Comparison layout failed");
            window.VerifyToolbarBelow();
            window.SaveHidden(Path.Combine(directory, "selftest-compare.png"));
            LanguageChoice.Select(window.targetChoice, "ja");
            if (calls != 1 || input != text || from != "auto" || to != "ja" || !window.busy) throw new Exception("Language switch failed");
            window.UpdateText(text, translation, "", boxes, false, "en", "zh");
            window.swap.PerformClick();
            // PerformClick on a hidden button can be suppressed by WinForms.
            if (calls == 1) window.RequestTranslation(true);
            if (calls != 2 || input != translation || from != "zh" || to != "en") throw new Exception("Swap direction failed");
            window.UpdateText(text, "", "HTTP 429\n翻译服务暂不可用", boxes);
            window.VerifyToolbarBelow();
            window.SaveHidden(Path.Combine(directory, "selftest-error.png"));
            Message message = new Message();
            if (!window.ProcessCmdKey(ref message, Keys.Escape) || !window.IsDisposed) throw new Exception("Esc did not close the hidden pin");
        }
        using (var window = new PinWindow(capture, delegate { }, delegate { }, delegate { }))
        {
            Message escape = Message.Create(window.targetChoice.Handle, 0x100, new IntPtr((int)Keys.Escape), IntPtr.Zero);
            if (!window.PreFilterMessage(ref escape) || !window.IsDisposed) throw new Exception("Dropdown Esc filtering failed");
        }
        using (var image = new Bitmap(140, 34))
        using (Graphics g = Graphics.FromImage(image))
        using (var buffer = new MemoryStream())
        {
            g.Clear(Color.White); g.DrawString("Hello world", Ui.Setting, Brushes.Black, 8, 6);
            image.Save(buffer, ImageFormat.Png);
            var small = new Program.Capture { Png = buffer.ToArray(), Width = 140, Height = 34, Rect = new Rectangle(400, 300, 140, 34) };
            using (var window = new PinWindow(small, delegate { }, delegate { }, delegate { }))
            {
                if (window.Width != 142 || window.Height != 36) throw new Exception("Small screenshot has toolbar-sized empty padding");
                window.UpdateText("Hello world", "这是一段比原文更长的中文译文，保持正常字号并自动换行。", "",
                    new[] { new Program.TextLine { Text = "Hello world", Box = new RectangleF(8, 6, 105, 22) } }, false, "en", "zh");
                if (window.overlay.Width != 240 || window.overlay.Height <= 34) throw new Exception("Readable small translation fallback failed");
                window.SaveHidden(Path.Combine(directory, "selftest-small.png"));
                foreach (var area in new[] { window.area, new Rectangle(-1920, -80, 1280, 720), new Rectangle(100, 50, 800, 600) })
                {
                    window.area = area;
                    foreach (Point corner in new[] { area.Location, new Point(area.Right - 1, area.Bottom - 1) })
                    {
                        window.Location = corner; window.LayoutImage(false);
                        window.VerifyToolbarBelow();
                    }
                }
                if (window.Visible || window.toolbar.Visible) throw new Exception("UI self-test made a window visible");
            }
        }
    }
    internal static void VerifySpatialLayout(string directory)
    {
        using (var image = new Bitmap(700, 120))
        using (Graphics g = Graphics.FromImage(image))
        using (var buffer = new MemoryStream())
        {
            var blocks = new[] {
                new Program.TextLine { Text = "Repository", Box = new RectangleF(35, 12, 200, 20) },
                new Program.TextLine { Text = "Pull requests", Box = new RectangleF(35, 64, 100, 20) },
                new Program.TextLine { Text = "Actions", Box = new RectangleF(235, 64, 70, 20) },
                new Program.TextLine { Text = "Security and quality", Box = new RectangleF(395, 64, 180, 20) }
            };
            g.Clear(Color.White); g.FillRectangle(Brushes.DodgerBlue, 8, 64, 18, 18);
            foreach (var block in blocks) g.DrawString(block.Text, Ui.Setting, Brushes.Black, block.Box.Location);
            string[] translations = { "项目仓库", "拉取\n请求", "操作", "安全与质量" };
            using (var result = Render(image, blocks, String.Join("\n", translations), 1, translations))
            {
                if (result.Size != image.Size || result.GetPixel(16, 72).ToArgb() != Color.DodgerBlue.ToArgb()) throw new Exception("Spatial layout changed size or covered an icon");
                result.Save(Path.Combine(directory, "selftest-navigation.png"), ImageFormat.Png);
            }
            var crowded = new[] { new string('W', 600), translations[1], translations[2], translations[3] };
            using (var result = Render(image, blocks, String.Join("\n", crowded), 1, crowded))
            {
                if (result.Size != image.Size || result.GetPixel(16, 72).ToArgb() != Color.DodgerBlue.ToArgb())
                    throw new Exception("One crowded block flattened the whole screenshot");
                result.Save(Path.Combine(directory, "selftest-navigation-constrained.png"), ImageFormat.Png);
            }
            var numeric = new Program.TextLine { Text = "499", Box = new RectangleF(270, 12, 25, 11), GlyphHeight = 11 };
            Program.TextLine[] badgeBlocks = new[] { blocks[0], numeric, blocks[1], blocks[2], blocks[3] };
            string[] badgeTranslations = { translations[0], "第499章", translations[1], translations[2], translations[3] };
            using (var result = Render(image, badgeBlocks, String.Join("\n", badgeTranslations), 1, badgeTranslations))
            {
                if (result.Size != image.Size || badgeTranslations[1] != "第499章") throw new Exception("Badge rendering changed size or provider data");
                for (int x = 270; x < 295; x++)
                    for (int y = 12; y < 23; y++)
                        if (result.GetPixel(x, y) != image.GetPixel(x, y)) throw new Exception("Numeric badge was painted over");
            }
            image.Save(buffer, ImageFormat.Png);
            var capture = new Program.Capture { Rect = new Rectangle(400, 300, 700, 120), Width = 700, Height = 120, Png = buffer.ToArray() };
            Program.TextLine[] swapped = null;
            using (var window = new PinWindow(capture, delegate { }, delegate(PinWindow pin, string text, string from, string to, Program.TextLine[] textBlocks) { swapped = textBlocks; }, delegate { }))
            {
                window.UpdateText(String.Join("\n", blocks.Select(x => x.Text)), String.Join("\n", translations), "", blocks, false, "en", "zh", translations);
                if (window.Width != 702 || window.Height != 122) throw new Exception("Navigation result gained blank padding");
                window.swap.PerformClick();
                if (swapped == null || swapped.Length != 4 || swapped[1].Text != "拉取\n请求") throw new Exception("Reverse translation lost mapped block boundaries");
                string longText = String.Join("\n", Enumerable.Repeat("A long translated paragraph that must remain readable.", 160));
                window.UpdateText("Repository", longText, "", new[] { blocks[0] }, false, "en", "zh", new[] { longText });
                if (window.overlay.Width > 640 || window.scroll.Maximum <= window.scroll.LargeChange || window.Height >= window.overlay.Height) throw new Exception("Long translation did not scroll");
                window.VerifyToolbarBelow();
                window.compare.Checked = true;
                window.VerifyToolbarBelow();
                window.SaveHidden(Path.Combine(directory, "selftest-long-compare.png"));
                if (window.Visible || window.toolbar.Visible) throw new Exception("Spatial test showed a window");
            }
        }
    }
    private void SaveHidden(string path)
    {
        PerformLayout();
        int width = Math.Max(Width, toolbar.Width), gap = toolbar.Height + 8;
        using (var bitmap = new Bitmap(width + 16, Height + gap + 16))
        using (Graphics g = Graphics.FromImage(bitmap))
        {
            g.Clear(Ui.Page);
            int imageLeft = 8 + (width - Width) / 2, toolbarLeft = 8 + (width - toolbar.Width) / 2, toolbarTop = 8 + Height + 8;
            GraphicsState state = g.Save();
            g.TranslateTransform(imageLeft, 8);
            g.SetClip(picture.ClientRectangle);
            PaintImage(picture, new PaintEventArgs(g, picture.ClientRectangle));
            g.Restore(state);
            if (scroll.Visible) Ui.Snapshot(scroll, g, new Point(imageLeft + scroll.Left, 8 + scroll.Top));
            Ui.Box(g, new RectangleF(toolbarLeft, toolbarTop, toolbar.Width, toolbar.Height), Ui.Surface, Ui.InputLine, 6);
            foreach (Control control in toolbar.Controls) Ui.Snapshot(control, g, new Point(toolbarLeft + control.Left, toolbarTop + control.Top));
            bitmap.Save(path, ImageFormat.Png);
        }
    }
    private void VerifyToolbarBelow()
    {
        PositionToolbar();
        if (toolbar.Top != Bottom + 8 || Top < area.Top || toolbar.Bottom > area.Bottom ||
            Left < area.Left || Right > area.Right || toolbar.Left < area.Left || toolbar.Right > area.Right)
            throw new Exception("Bottom toolbar escaped the working area");
        if (Visible || toolbar.Visible) throw new Exception("Toolbar positioning test showed a window");
    }
    internal override void ApplyTheme() { BackColor = Ui.Surface; Ui.Refresh(this); toolbar.ApplyTheme(); if (IsHandleCreated) NativeUi.Decorate(Handle); }
    public bool PreFilterMessage(ref Message message)
    {
        if ((message.Msg == 0x100 || message.Msg == 0x104) && message.WParam.ToInt32() == (int)Keys.Escape && !IsDisposed)
        { Close(); return true; }
        return false;
    }
    protected override bool ProcessCmdKey(ref Message message, Keys keyData)
    {
        if (keyData == Keys.Escape) { Close(); return true; }
        if (keyData == Keys.Tab) { compare.Checked = !compare.Checked; return true; }
        return base.ProcessCmdKey(ref message, keyData);
    }
    internal void UpdateText(string text, string translation, string status, Program.TextLine[] boxes, bool working = false, string detected = "", string target = "", string[] segments = null)
    {
        source = text; translated = translation; progress = status;
        if (boxes != null) lines = boxes;
        translatedBlocks = segments;
        if (translatedBlocks == null && translation.Length > 0 && lines != null)
        {
            string[] split = Regex.Split(translation, @"\r?\n").Where(x => x.Length > 0).ToArray();
            if (split.Length == lines.Length) translatedBlocks = split;
        }
        updating = true;
        if (detected.Length > 0)
        {
            detectedLanguage = detected;
            sourceChoice.Items[0] = new LanguageChoice("auto", Program.T("检测到", "Detected ") + new LanguageChoice(detected));
        }
        if (target.Length > 0) LanguageChoice.Select(targetChoice, target);
        updating = false;
        Bitmap old = overlay;
        overlay = translated.Length > 0 && lines != null ? Render(original, lines, translated, factor, translatedBlocks) : null;
        if (old != null) old.Dispose();
        scroll.Value = 0;
        SetBusy(working);
    }
    internal static Bitmap Render(Bitmap original, Program.TextLine[] lines, string text, float scale, string[] aligned = null)
    {
        string[] segments = aligned == null ? Regex.Split(text, @"\r?\n").Select(x => x.Trim()).Where(x => x.Length > 0).ToArray() : aligned.ToArray();
        bool perLine = segments.Length == lines.Length && lines.All(x => !x.Box.IsEmpty);
        var boxes = new Rectangle[lines.Length];
        var sizes = new int[lines.Length];
        int constrained = 0;
        if (perLine)
            using (Graphics g = Graphics.FromImage(original))
            using (var format = new StringFormat(StringFormat.GenericTypographic))
                for (int i = 0; i < lines.Length; i++)
                {
                    if (SpatialText.IsLiteral(lines[i].Text)) { segments[i] = lines[i].Text; continue; }
                    Rectangle box = Scale(lines[i].Box, scale);
                    box.Intersect(new Rectangle(Point.Empty, original.Size));
                    if (box.Width < 2 || box.Height < 2) { perLine = false; break; }
                    int nextTop = original.Height, nextLeft = original.Width;
                    foreach (var other in lines.Where((x, index) => index != i))
                    {
                        Rectangle adjacent = Scale(other.Box, scale);
                        if (adjacent.Top >= box.Bottom - 2 && adjacent.Left < box.Right && adjacent.Right > box.Left)
                            nextTop = Math.Min(nextTop, adjacent.Top - 4);
                        int overlap = Math.Min(box.Bottom, adjacent.Bottom) - Math.Max(box.Top, adjacent.Top);
                        if (adjacent.Left > box.Right && overlap >= Math.Min(box.Height, adjacent.Height) / 2)
                            nextLeft = Math.Min(nextLeft, adjacent.Left - 8);
                    }
                    int top = Math.Max(0, box.Top - 3);
                    int space = Math.Min(original.Height - top, Math.Min(Math.Max(box.Height * 2 + 6, 26), Math.Max(box.Height, nextTop - top)));
                    int availableWidth = Math.Max(box.Width, Math.Min(nextLeft, box.Right + Math.Max(12, box.Height)) - box.Left);
                    boxes[i] = new Rectangle(box.Left, top, availableWidth, space);
                    float glyph = lines[i].GlyphHeight > 0 ? lines[i].GlyphHeight * scale : box.Height;
                    int maxSize = Math.Max(14, Math.Min(26, (int)(glyph * .95)));
                    for (int size = maxSize; size >= 12; size--)
                        using (var font = new Font("Microsoft YaHei UI", size, GraphicsUnit.Pixel))
                        {
                            int fitted, count;
                            SizeF measured = g.MeasureString(segments[i], font, new SizeF(boxes[i].Width, boxes[i].Height), format, out fitted, out count);
                            if (fitted == segments[i].Length && measured.Height <= boxes[i].Height + 1 && measured.Width <= boxes[i].Width + 1)
                            {
                                sizes[i] = size;
                                boxes[i].Width = Math.Min(availableWidth, Math.Max(box.Width, (int)Math.Ceiling(measured.Width) + 3));
                                boxes[i].Height = Math.Min(space, Math.Max(box.Bottom - top + 1, (int)Math.Ceiling(measured.Height) + 3));
                                break;
                            }
                        }
                    if (sizes[i] == 0)
                    {
                        if (lines.Length == 1) { perLine = false; break; }
                        // A crowded block must not move every other block into a text list.
                        boxes[i].Height = Math.Min(original.Height - top, Math.Max(box.Height, nextTop - top));
                        sizes[i] = 12; constrained++;
                    }
                }
        Program.Log("pin: overlay mode=" + (perLine ? "lines" : "readable") + " lines=" + lines.Length + " boxes=" + lines.Length + " segments=" + segments.Length + " constrained=" + constrained);
        if (!perLine) return ReadableTranslation(text, original.Width);
        var result = new Bitmap(original);
        using (Graphics g = Graphics.FromImage(result))
        {
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            for (int i = 0; i < lines.Length; i++)
                if (sizes[i] > 0) DrawText(g, original, boxes[i], segments[i], sizes[i], Scale(lines[i].Box, scale));
        }
        return result;
    }
    private static Bitmap ReadableTranslation(string text, int originalWidth)
    {
        int width = Math.Max(240, Math.Min(640, originalWidth));
        int height;
        using (var probe = new Bitmap(1, 1))
        using (Graphics g = Graphics.FromImage(probe))
            height = Math.Max(56, (int)Math.Ceiling(g.MeasureString(text, Ui.Setting, width - 32).Height) + 32);
        var result = new Bitmap(width, Math.Min(8000, height));
        using (Graphics g = Graphics.FromImage(result))
        using (var brush = new SolidBrush(Ui.Text))
        {
            g.Clear(Ui.Surface); g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            g.DrawString(text, Ui.Setting, brush, new RectangleF(16, 16, width - 32, result.Height - 32));
        }
        return result;
    }
    private static Rectangle Scale(RectangleF box, float scale) { return Rectangle.FromLTRB((int)Math.Floor(box.Left * scale), (int)Math.Floor(box.Top * scale), (int)Math.Ceiling(box.Right * scale), (int)Math.Ceiling(box.Bottom * scale)); }
    private static void DrawText(Graphics g, Bitmap bitmap, Rectangle box, string text, int size, Rectangle sample)
    {
        box.Intersect(new Rectangle(Point.Empty, bitmap.Size));
        if (box.Width < 2 || box.Height < 2) return;
        var red = new List<int>(); var green = new List<int>(); var blue = new List<int>();
        int left = Math.Max(0, sample.Left - 4), right = Math.Min(bitmap.Width - 1, sample.Right + 4);
        int top = Math.Max(0, sample.Top - 4), bottom = Math.Min(bitmap.Height - 1, sample.Bottom + 4);
        for (int x = left; x <= right; x += Math.Max(1, (right - left) / 12))
            foreach (int y in new[] { top, bottom }) { Color c = bitmap.GetPixel(x, y); red.Add(c.R); green.Add(c.G); blue.Add(c.B); }
        for (int y = top; y <= bottom; y += Math.Max(1, (bottom - top) / 8))
            foreach (int x in new[] { left, right }) { Color c = bitmap.GetPixel(x, y); red.Add(c.R); green.Add(c.G); blue.Add(c.B); }
        red.Sort(); green.Sort(); blue.Sort();
        Color background = Color.FromArgb(red[red.Count / 2], green[green.Count / 2], blue[blue.Count / 2]);
        Color foreground = background.GetBrightness() > 0.5 ? Color.Black : Color.White;
        Rectangle patch = box; patch.Inflate(1, 1);
        using (var fill = new SolidBrush(background)) g.FillRectangle(fill, patch);
        using (var format = new StringFormat(StringFormat.GenericTypographic) { Trimming = StringTrimming.EllipsisWord })
        using (var brush = new SolidBrush(foreground))
        {
            using (var font = new Font("Microsoft YaHei UI", size, GraphicsUnit.Pixel))
            {
                GraphicsState state = g.Save();
                try { g.SetClip(patch); g.DrawString(text, font, brush, box, format); }
                finally { g.Restore(state); }
            }
        }
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing && !resourcesDisposed)
        {
            resourcesDisposed = true;
            if (filtering) { Application.RemoveMessageFilter(this); filtering = false; }
            if (overlay != null) { overlay.Dispose(); overlay = null; }
            original.Dispose(); menu.Dispose(); tips.Dispose(); toolbar.Dispose();
        }
        base.Dispose(disposing);
    }
}

internal sealed class PinCanvas : Control
{
    private readonly System.Windows.Forms.Timer timer;
    internal int Frame;
    internal bool Animate { set { timer.Enabled = value; } }
    internal PinCanvas()
    {
        SetStyle(ControlStyles.StandardClick | ControlStyles.StandardDoubleClick | ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        timer = new System.Windows.Forms.Timer { Interval = 80 };
        timer.Tick += delegate { Frame = (Frame + 1) % 12; Invalidate(); };
    }
    protected override void Dispose(bool disposing) { if (disposing) timer.Dispose(); base.Dispose(disposing); }
}

internal sealed class LanguageChoice
{
    internal static readonly string[] Codes = { "zh", "en", "ja", "ko", "fr", "de", "es", "ru" };
    private static readonly string[] English = { "Chinese", "English", "Japanese", "Korean", "French", "German", "Spanish", "Russian" };
    private static readonly string[] Chinese = { "中文简体", "英语", "日语", "韩语", "法语", "德语", "西班牙语", "俄语" };
    internal readonly string Code;
    private readonly string label;
    internal LanguageChoice(string code, string title = null) { Code = code; label = title; }
    public override string ToString() { int index = Array.IndexOf(Codes, Code); return label ?? (index >= 0 ? Program.T(Chinese[index], English[index]) : Code); }
    internal static string EnglishName(string code) { int index = Array.IndexOf(Codes, code); return index >= 0 ? English[index] : code; }
    internal static string Normalize(string code)
    {
        code = code.ToLowerInvariant().Split('-')[0];
        return Codes.Contains(code) ? code : "";
    }
    internal static void Select(LanguagePicker picker, string code)
    {
        for (int i = 0; i < picker.Items.Count; i++) if (((LanguageChoice)picker.Items[i]).Code == code) { picker.SelectedIndex = i; return; }
    }
}

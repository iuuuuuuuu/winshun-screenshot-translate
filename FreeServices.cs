using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Dict = System.Collections.Generic.Dictionary<string, object>;

internal sealed class MyMemoryUsage
{
    private readonly object gate = new object();
    private readonly string file;
    private readonly Func<DateTime> clock;
    private string day = "";
    private long submitted;
    private bool available = true;
    internal MyMemoryUsage(string directory, Func<DateTime> clock = null)
    {
        file = Path.Combine(directory, "mymemory-usage.json");
        this.clock = clock ?? (() => DateTime.Now);
        if (!File.Exists(file)) return;
        try
        {
            var document = new JavaScriptSerializer().Deserialize<Dict>(File.ReadAllText(file, Encoding.UTF8));
            day = Convert.ToString(document["day"]);
            submitted = Math.Max(0, Convert.ToInt64(document["submitted"]));
        }
        catch { available = false; Program.Log("MyMemory usage unavailable; existing file preserved"); }
    }
    private void RefreshDay()
    {
        string now = clock().ToString("yyyy-MM-dd");
        if (day == now) return;
        day = now; submitted = 0;
    }
    internal bool Available { get { lock (gate) return available; } }
    internal long Today { get { lock (gate) { RefreshDay(); return submitted; } } }
    internal static int Characters(string text)
    {
        int count = 0;
        for (int i = 0; i < text.Length; i++, count++)
            if (Char.IsHighSurrogate(text[i]) && i + 1 < text.Length && Char.IsLowSurrogate(text[i + 1])) i++;
        return count;
    }
    internal void RecordSubmission(string text)
    {
        lock (gate)
        {
            if (!available) return;
            RefreshDay(); submitted += Characters(text);
            string temporary = file + ".tmp";
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(file));
                File.WriteAllText(temporary, new JavaScriptSerializer().Serialize(Program.Obj("version", 1, "day", day, "submitted", submitted)), new UTF8Encoding(false));
                if (File.Exists(file)) File.Replace(temporary, file, null);
                else File.Move(temporary, file);
            }
            catch { available = false; Program.Log("MyMemory usage could not be saved; translation continues"); }
            finally { try { if (File.Exists(temporary)) File.Delete(temporary); } catch { } }
        }
    }
    internal static void Verify(string directory)
    {
        DateTime date = new DateTime(2026, 11, 5);
        var usage = new MyMemoryUsage(directory, () => date);
        usage.RecordSubmission("Hello"); usage.RecordSubmission("\u4f60\u597d\ud83d\ude00");
        if (usage.Today != 8 || new MyMemoryUsage(directory, () => date).Today != 8) throw new Exception("Usage persistence failed");
        date = date.AddDays(1);
        if (usage.Today != 0) throw new Exception("Usage day reset failed");
        usage.RecordSubmission("A");
        if (new MyMemoryUsage(directory, () => date).Today != 1) throw new Exception("Usage new-day persistence failed");
        File.Delete(Path.Combine(directory, "mymemory-usage.json"));
    }
}

internal sealed class FreeTranslation
{
    internal string Text, Detected;
    internal string[] Parts;
}

internal static class FreeServices
{
    private static string edgeToken = "";
    private static DateTime edgeExpiry;
    internal static bool Handles(string provider) { return provider == "google" || provider == "edge" || provider == "zvo"; }
    internal static string DefaultEndpoint(string provider)
    {
        return provider == "google" ? "https://translate.googleapis.com/translate_a/single"
            : provider == "edge" ? "https://api-edge.cognitive.microsofttranslator.com/translate"
            : "https://api.translate.zvo.cn/translate.json";
    }
    internal static void ValidateProxy(string proxy)
    {
        if (String.IsNullOrWhiteSpace(proxy)) return;
        Uri uri;
        if (!Uri.TryCreate(proxy, UriKind.Absolute, out uri) || (uri.Scheme != "http" && uri.Scheme != "https") || uri.UserInfo.Length > 0)
            throw new ArgumentException(Program.T("代理需为不含账号密码的 HTTP / HTTPS 地址", "Proxy must be an HTTP/HTTPS URL without credentials"));
    }
    internal static HttpClient CreateClient(string proxy, bool browserHeaders = false)
    {
        if (String.IsNullOrWhiteSpace(proxy))
            foreach (string name in new[] { "OPENAPI_TRANSLATE_PROXY", "HTTPS_PROXY", "HTTP_PROXY", "ALL_PROXY" })
            {
                string value = Environment.GetEnvironmentVariable(name);
                if (String.IsNullOrWhiteSpace(value)) continue;
                proxy = value.Contains("://") ? value : "http://" + value;
                break;
            }
        ValidateProxy(proxy);
        var handler = new HttpClientHandler();
        if (!String.IsNullOrWhiteSpace(proxy)) handler.Proxy = new WebProxy(proxy);
        var client = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(8), MaxResponseContentBufferSize = 8 * 1024 * 1024 };
        client.DefaultRequestHeaders.TryAddWithoutValidation("User-Agent", browserHeaders
            ? "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36" : "WinShun-screenshot-translate/0.1.10");
        client.DefaultRequestHeaders.TryAddWithoutValidation("Accept", "application/json,text/plain,*/*");
        return client;
    }
    private static object Get(object value, string key)
    {
        object result; var map = value as Dict;
        return map != null && map.TryGetValue(key, out result) ? result : null;
    }
    private static string Text(object value) { return value as string ?? ""; }
    private static object[] Array(object value) { return value as object[] ?? new object[0]; }
    private static string Query(string endpoint, params string[] pairs)
    {
        Uri uri;
        if (!Uri.TryCreate(endpoint, UriKind.Absolute, out uri) || (uri.Scheme != "http" && uri.Scheme != "https") || uri.Fragment.Length > 0)
            throw new ArgumentException(Program.T("接口地址不是有效的 HTTP URL", "The endpoint is not a valid HTTP URL"));
        return endpoint + (endpoint.Contains("?") ? "&" : "?") + String.Join("&",
            Enumerable.Range(0, pairs.Length / 2).Select(i => Uri.EscapeDataString(pairs[i * 2]) + "=" + Uri.EscapeDataString(pairs[i * 2 + 1])));
    }
    private static string Send(HttpClient client, HttpRequestMessage request, CancellationToken cancellation)
    {
        using (request)
        using (var response = client.SendAsync(request, cancellation).GetAwaiter().GetResult())
        {
            if (!response.IsSuccessStatusCode) throw new Exception("HTTP " + (int)response.StatusCode);
            return response.Content.ReadAsStringAsync().GetAwaiter().GetResult();
        }
    }
    internal static FreeTranslation Translate(ServiceProfile profile, string text, string from, string target, string[] parts = null)
    {
        string endpoint = profile.Endpoint.Length == 0 ? DefaultEndpoint(profile.Provider) : profile.Endpoint;
        var json = new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 };
        using (var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(8)))
        using (var client = CreateClient(profile.Proxy, true))
        {
            if (profile.Provider == "google")
            {
                string[] endpoints = profile.Endpoint.Length == 0 ? new[] { endpoint, "https://translate.google.com/translate_a/single" } : new[] { endpoint };
                if (parts == null) return Google(client, endpoints, text, from, target, cancellation.Token).GetAwaiter().GetResult();
                using (var slots = new SemaphoreSlim(4))
                {
                    var tasks = parts.Select(async part => {
                        await slots.WaitAsync(cancellation.Token).ConfigureAwait(false);
                        try { return await Google(client, endpoints, part, from, target, cancellation.Token).ConfigureAwait(false); }
                        finally { slots.Release(); }
                    }).ToArray();
                    FreeTranslation[] results = Task.WhenAll(tasks).GetAwaiter().GetResult();
                    string[] translated = results.Select(x => x.Text).ToArray();
                    string detected = results.Select((x, i) => new { Code = LanguageChoice.Normalize(x.Detected), Weight = parts[i].Length })
                        .Where(x => x.Code.Length > 0).GroupBy(x => x.Code).OrderByDescending(x => x.Sum(y => y.Weight)).Select(x => x.Key).FirstOrDefault() ?? "";
                    return new FreeTranslation { Text = String.Join("\n", translated), Parts = translated, Detected = detected };
                }
            }
            if (profile.Provider == "edge")
            {
                Query(endpoint);
                if (edgeToken.Length == 0 || edgeExpiry <= DateTime.UtcNow.AddSeconds(60))
                {
                    edgeToken = Send(client, new HttpRequestMessage(HttpMethod.Get, "https://edge.microsoft.com/translate/auth"), cancellation.Token).Trim();
                    if (edgeToken.Length == 0) throw new Exception("Edge returned an empty token");
                    edgeExpiry = DateTime.UtcNow.AddMinutes(8);
                    try
                    {
                        string encoded = edgeToken.Split('.')[1].Replace('-', '+').Replace('_', '/');
                        encoded = encoded.PadRight((encoded.Length + 3) / 4 * 4, '=');
                        object payload = json.DeserializeObject(Encoding.UTF8.GetString(Convert.FromBase64String(encoded)));
                        edgeExpiry = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddSeconds(Convert.ToDouble(Get(payload, "exp")));
                    }
                    catch { }
                }
                string query = Query(endpoint, "api-version", "3.0", "to", target == "zh" ? "zh-Hans" : target);
                if (from != "auto") query += "&from=" + Uri.EscapeDataString(from == "zh" ? "zh-Hans" : from);
                var request = new HttpRequestMessage(HttpMethod.Post, query);
                request.Headers.TryAddWithoutValidation("Authorization", "Bearer " + edgeToken);
                request.Content = new StringContent(json.Serialize((parts ?? new[] { text }).Select(x => Program.Obj("Text", x)).ToArray()), Encoding.UTF8, "application/json");
                try { return ParseEdge(json.DeserializeObject(Send(client, request, cancellation.Token)), parts == null ? 1 : parts.Length); }
                catch { edgeToken = ""; throw; }
            }
            Query(endpoint);
            var form = new Dictionary<string, string> {
                { "from", ZvoLanguage(from) }, { "to", ZvoLanguage(target) },
                { "lowercase", "0" }, { "text", json.Serialize(parts ?? new[] { text }) }
            };
            if (profile.ApiKey.Length > 0) form["key"] = profile.ApiKey;
            var zvoRequest = new HttpRequestMessage(HttpMethod.Post, endpoint) { Content = new FormUrlEncodedContent(form) };
            return ParseZvo(json.DeserializeObject(Send(client, zvoRequest, cancellation.Token)), parts == null ? 1 : parts.Length);
        }
    }
    private static async Task<FreeTranslation> Google(HttpClient client, string[] endpoints, string text, string from, string target, CancellationToken cancellation)
    {
        var json = new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 };
        Exception failure = null;
        foreach (string variant in new[] { "dict-chrome-ex", "chrome-extension" })
            foreach (string url in endpoints)
            {
                cancellation.ThrowIfCancellationRequested();
                try
                {
                    string query = Query(url, "client", variant, "sl", from == "zh" ? "zh-CN" : from, "tl", target == "zh" ? "zh-CN" : target, "dt", "t", "q", text);
                    using (var request = new HttpRequestMessage(HttpMethod.Get, query))
                    using (var response = await client.SendAsync(request, cancellation).ConfigureAwait(false))
                    {
                        if (!response.IsSuccessStatusCode) throw new Exception("HTTP " + (int)response.StatusCode);
                        string answer = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                        return ParseGoogle(json.DeserializeObject(answer));
                    }
                }
                catch (OperationCanceledException) { throw; }
                catch (Exception ex) { failure = ex; }
            }
        throw failure ?? new Exception("Google returned no translation");
    }
    internal static FreeTranslation ParseGoogle(object decoded)
    {
        object[] root = Array(decoded);
        string text = root.Length > 0 ? String.Concat(Array(root[0]).Select(x => Array(x).Length > 0 ? Text(Array(x)[0]) : "")) : "";
        if (text.Trim().Length == 0) throw new Exception("Google returned no translation");
        return new FreeTranslation { Text = text, Detected = root.Length > 2 ? Text(root[2]) : "" };
    }
    internal static FreeTranslation ParseEdge(object decoded, int expected = 1)
    {
        object[] root = Array(decoded), translations = root.Length > 0 ? Array(Get(root[0], "translations")) : new object[0];
        string text = translations.Length > 0 ? Text(Get(translations[0], "text")) : "";
        if (text.Trim().Length == 0) throw new Exception("Edge returned no translation");
        string[] parts = root.Select(x => Array(Get(x, "translations")).Length > 0 ? Text(Get(Array(Get(x, "translations"))[0], "text")) : "").ToArray();
        if (parts.Length != expected || parts.Any(String.IsNullOrWhiteSpace)) throw new Exception("Edge returned incomplete text blocks");
        return new FreeTranslation { Text = String.Join("\n", parts), Parts = parts, Detected = Text(Get(Get(root[0], "detectedLanguage"), "language")) };
    }
    internal static FreeTranslation ParseZvo(object decoded, int expected = 1)
    {
        if (Convert.ToInt32(Get(decoded, "result")) != 1)
            throw new Exception(Program.T("translate.js 服务不可用或已限流", "translate.js is unavailable or rate-limited"));
        object[] texts = Array(Get(decoded, "text"));
        if (texts.Length != expected || texts.Any(x => Text(x).Trim().Length == 0)) throw new Exception("translate.js returned invalid translation data");
        string[] parts = texts.Select(Text).ToArray();
        return new FreeTranslation { Text = String.Join("\n", parts), Parts = parts, Detected = "" };
    }
    private static string ZvoLanguage(string language)
    {
        var names = new Dictionary<string, string> { { "zh", "chinese_simplified" }, { "en", "english" }, { "ja", "japanese" }, { "ko", "korean" },
            { "fr", "french" }, { "de", "deutsch" }, { "es", "spanish" }, { "ru", "russian" } };
        string name; return names.TryGetValue(language, out name) ? name : language;
    }
    internal static void Verify()
    {
        var json = new JavaScriptSerializer();
        FreeTranslation google = ParseGoogle(json.DeserializeObject("[[[\"Hello\\n\",\"\",null,null],[\"World\",\"\",null,null]],null,\"en\"]"));
        if (google.Text != "Hello\nWorld" || google.Detected != "en") throw new Exception("Google segments failed");
        if (ParseEdge(json.DeserializeObject("[{\"detectedLanguage\":{\"language\":\"en\"},\"translations\":[{\"text\":\"OK\"}]}]")).Detected != "en") throw new Exception("Edge detection failed");
        if (ParseZvo(json.DeserializeObject("{\"result\":1,\"text\":[\"Hello World\"]}")).Text != "Hello World") throw new Exception("Zvo translation failed");
        var batch = ParseZvo(json.DeserializeObject("{\"result\":1,\"text\":[\"first\\nsecond\",\"third\"]}"), 2);
        if (batch.Parts.Length != 2 || batch.Parts[0] != "first\nsecond") throw new Exception("Zvo block boundaries failed");
        var edge = ParseEdge(json.DeserializeObject("[{\"translations\":[{\"text\":\"first\\nsecond\"}]},{\"translations\":[{\"text\":\"third\"}]}]"), 2);
        if (edge.Parts.Length != 2 || edge.Parts[0] != "first\nsecond") throw new Exception("Edge block boundaries failed");
        foreach (string invalid in new[] { "{\"result\":0,\"text\":[\"bad\"]}", "{\"result\":1,\"text\":[]}", "{\"result\":1,\"text\":[\"\"]}" })
        {
            bool rejected = false; try { ParseZvo(json.DeserializeObject(invalid)); } catch { rejected = true; }
            if (!rejected) throw new Exception("Invalid Zvo response accepted");
        }
        if (ZvoLanguage("zh") != "chinese_simplified" || ZvoLanguage("en") != "english") throw new Exception("Zvo language mapping failed");
    }
}

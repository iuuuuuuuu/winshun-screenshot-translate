"""Translation routes and current-user encrypted, independent profiles."""
import base64
import ctypes
import datetime
import html
import json
import os
from pathlib import Path
import re
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

PROVIDERS = ("mymemory", "google", "edge", "zvo", "microsoft", "deepl",
             "openai", "deepseek", "doubao", "hunyuan")
NAMES = ("MyMemory", "Google", "Edge", "Zvo", "Microsoft", "DeepL",
         "OpenAI", "DeepSeek", "Doubao", "Hunyuan")
ENDPOINTS = {
    "mymemory": "https://api.mymemory.translated.net/get",
    "google": "https://translate.googleapis.com/translate_a/single",
    "edge": "https://api-edge.cognitive.microsofttranslator.com/translate",
    "zvo": "https://api.translate.zvo.cn/translate.json",
    "microsoft": "https://api.cognitive.microsofttranslator.com",
    "deepl": "https://api-free.deepl.com",
    "openai": "https://api.openai.com/v1",
    "deepseek": "https://api.deepseek.com",
    "doubao": "https://ark.cn-beijing.volces.com/api/v3",
    "hunyuan": "https://api.hunyuan.cloud.tencent.com/v1",
}
LANGUAGES = {"auto": "Automatic", "zh": "Chinese", "en": "English", "ja": "Japanese",
             "ko": "Korean", "fr": "French", "de": "German", "es": "Spanish", "ru": "Russian"}
ENTROPY = b"iuuuuuuuu.screenshot-translate/profiles/v1"


def detect(text):
    latin = len(re.findall("[A-Za-z]", text))
    han = len(re.findall("[\u3400-\u9fff]", text))
    kana = len(re.findall("[\u3040-\u30ff]", text))
    hangul = len(re.findall("[\uac00-\ud7af]", text))
    cyrillic = len(re.findall("[\u0400-\u052f]", text))
    if kana and (han + kana) * 2 >= latin and kana >= hangul and (kana >= 2 or not latin + han):
        return "ja"
    if hangul and hangul * 2 >= latin + han * 2 + kana * 2:
        return "ko"
    if han and han * 2 >= latin + cyrillic:
        return "zh"
    return "ru" if cyrillic > latin else "en"


def target_for(text, setting="auto"):
    return setting if setting in LANGUAGES and setting != "auto" else ("en" if detect(text) == "zh" else "zh")


def selected(settings):
    value = settings.get("provider", "mymemory")
    if value == "ai":
        value = settings.get("aiProvider", "openai")
        if value not in PROVIDERS[6:]:
            value = "openai"
    return value if value in PROVIDERS else "mymemory"


def validate_url(value, proxy=False):
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.fragment or parsed.username or parsed.password:
        raise ValueError("Invalid HTTP/HTTPS proxy" if proxy else "Invalid HTTP/HTTPS endpoint")
    return value


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def protect(data, decrypt=False):
    from ctypes import wintypes as w

    class Blob(ctypes.Structure):
        _fields_ = [("size", w.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]

    def blob(value):
        buf = (ctypes.c_ubyte * len(value)).from_buffer_copy(value)
        return Blob(len(value), buf), buf

    source, source_buffer = blob(data)
    entropy, entropy_buffer = blob(ENTROPY)
    result = Blob()
    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    fn = crypt.CryptUnprotectData if decrypt else crypt.CryptProtectData
    fn.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.POINTER(Blob),
                   ctypes.c_void_p, ctypes.c_void_p, w.DWORD, ctypes.POINTER(Blob)]
    fn.restype = w.BOOL
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    if not fn(ctypes.byref(source), None, ctypes.byref(entropy), None, None, 1, ctypes.byref(result)):
        raise OSError("Windows could not decrypt the service key" if decrypt else "Windows could not encrypt the service key")
    try:
        return ctypes.string_at(result.data, result.size)
    finally:
        kernel.LocalFree(result.data)


class Profiles:
    def __init__(self, directory):
        self.file = Path(directory) / "services.json"
        self.lock = threading.RLock()
        self.stored = {}
        if self.file.exists():
            document = json.loads(self.file.read_text(encoding="utf-8-sig"))
            if not isinstance(document.get("profiles"), dict):
                raise ValueError("Invalid services.json; existing configuration was preserved")
            self.stored = {p: v for p, v in document["profiles"].items() if p in PROVIDERS}

    def get(self, provider):
        with self.lock:
            data = self.stored.get(provider, {})
            secret = data.get("secret", "")
            return {"provider": provider,
                    "apiKey": protect(base64.b64decode(secret), True).decode("utf-8") if secret else "",
                    **{k: data.get(k, "") for k in ("endpoint", "model", "region")}}

    def current(self, settings):
        return {**self.get(selected(settings)), "proxy": settings.get("proxy", "").strip()}

    def save(self, drafts):
        with self.lock:
            values = dict(self.stored)
            for provider, draft in drafts.items():
                if provider not in PROVIDERS:
                    raise ValueError("Unknown provider")
                endpoint = draft.get("endpoint", "").strip()
                if endpoint:
                    validate_url(endpoint)
                key = draft.get("apiKey", "").strip()
                values[provider] = {
                    "secret": base64.b64encode(protect(key.encode("utf-8"))).decode("ascii") if key else "",
                    "endpoint": endpoint, "model": draft.get("model", "").strip(),
                    "region": draft.get("region", "").strip(),
                }
            atomic_json(self.file, {"version": 2, "profiles": values})
            self.stored = values


class Usage:
    def __init__(self, directory, clock=datetime.date.today):
        self.file = Path(directory) / "mymemory-usage.json"
        self.clock, self.lock = clock, threading.Lock()
        self.day, self.submitted, self.available = "", 0, True
        if self.file.exists():
            try:
                data = json.loads(self.file.read_text(encoding="utf-8-sig"))
                self.day, self.submitted = data["day"], max(0, int(data["submitted"]))
            except (ValueError, KeyError, OSError):
                self.available = False

    def _refresh(self):
        day = self.clock().isoformat()
        if day != self.day:
            self.day, self.submitted = day, 0

    def today(self):
        with self.lock:
            self._refresh()
            return self.submitted

    def record(self, text):
        with self.lock:
            if not self.available:
                return
            self._refresh()
            self.submitted += len(text)
            try:
                atomic_json(self.file, {"version": 1, "day": self.day, "submitted": self.submitted})
            except OSError:
                self.available = False


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward keys or selected text to an unconfigured redirect target.
        raise ValueError("The service redirected the request; configure its final endpoint")


class Http:
    def __init__(self, proxy="", budget=8):
        if not proxy:
            proxy = next((os.environ[k] for k in
                          ("OPENAPI_TRANSLATE_PROXY", "HTTPS_PROXY", "HTTP_PROXY", "ALL_PROXY")
                          if os.environ.get(k)), "")
        if proxy:
            proxy = validate_url(proxy if "://" in proxy else "http://" + proxy, True)
        handler = urllib.request.ProxyHandler({"http": proxy, "https": proxy}) if proxy else urllib.request.ProxyHandler()
        self.opener = urllib.request.build_opener(handler, NoRedirect())
        self.deadline = time.monotonic() + budget

    def request(self, url, body=None, headers=None, form=False, raw=False):
        validate_url(url)
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Translation timed out")
        h = {"User-Agent": "WinShun-screenshot-translate/0.2.0", "Accept": "application/json,text/plain"}
        h.update(headers or {})
        data = None
        if body is not None:
            data = (urllib.parse.urlencode(body) if form else json.dumps(body, ensure_ascii=False)).encode("utf-8")
            h["Content-Type"] = "application/x-www-form-urlencoded" if form else "application/json; charset=utf-8"
        try:
            with self.opener.open(urllib.request.Request(url, data, h), timeout=remaining) as response:
                answer = response.read(8 * 1024 * 1024 + 1)
            if len(answer) > 8 * 1024 * 1024:
                raise ValueError("The service response is too large")
            text = answer.decode("utf-8-sig")
            return text if raw else json.loads(text)
        except urllib.error.HTTPError as ex:
            # Response bodies may echo user text or secrets. Do not log them.
            ex.close()
            raise ValueError(f"HTTP {ex.code}") from None
        except urllib.error.URLError:
            raise ValueError("Cannot reach the service; check the network or shared proxy") from None


def query(url, **values):
    return url + ("&" if "?" in url else "?") + urllib.parse.urlencode(values)


def chunks_utf8(text, limit=500):
    chunks, current, size = [], [], 0
    for char in text:
        length = len(char.encode("utf-8"))
        if size + length > limit:
            chunks.append("".join(current))
            current, size = [], 0
        current.append(char)
        size += length
    if current:
        chunks.append("".join(current))
    return chunks


class Translator:
    def __init__(self, usage):
        self.usage = usage
        self.edge_token, self.edge_expiry = "", 0

    def translate(self, profile, parts, source="auto", target="zh"):
        provider, key = profile["provider"], profile.get("apiKey", "")
        if provider not in PROVIDERS:
            raise ValueError("Unknown translation provider")
        if provider not in PROVIDERS[:4] and not key:
            raise ValueError("API key is empty. Configure this service in Settings.")
        if provider == "doubao" and not profile.get("model"):
            raise ValueError("Doubao needs a model or inference endpoint ID")
        http = Http(profile.get("proxy", ""))
        url = profile.get("endpoint") or ENDPOINTS[provider]
        detected = detect("\n".join(parts)) if source == "auto" else source
        destination = "zh-CN" if target == "zh" else target
        origin = "zh-CN" if source == "zh" else source

        if provider in ("mymemory", "google"):
            def one(text):
                if provider == "mymemory":
                    src = detect(text) if source == "auto" else source
                    src = "zh-CN" if src == "zh" else src
                    if src == destination:
                        return text
                    translations = []
                    for chunk in chunks_utf8(text):
                        self.usage.record(chunk)
                        data = http.request(query(url, q=chunk, langpair=f"{src}|{destination}"))
                        if int(data.get("responseStatus", 0)) != 200:
                            raise ValueError("MyMemory is unavailable or its daily quota is exhausted")
                        translations.append(html.unescape(data["responseData"]["translatedText"]))
                    return "\n".join(translations)
                urls = [url] if profile.get("endpoint") else [url, "https://translate.google.com/translate_a/single"]
                failure = None
                for client in ("dict-chrome-ex", "chrome-extension"):
                    for endpoint in urls:
                        try:
                            data = http.request(query(endpoint, client=client, sl=origin, tl=destination, dt="t", q=text))
                            return "".join(row[0] or "" for row in data[0])
                        except (ValueError, urllib.error.URLError) as ex:
                            failure = ex
                raise failure
            with ThreadPoolExecutor(max_workers=4) as pool:
                translated = list(pool.map(one, parts))
        elif provider in ("microsoft", "edge"):
            headers = {}
            if provider == "edge":
                if time.monotonic() >= self.edge_expiry:
                    self.edge_token = http.request("https://edge.microsoft.com/translate/auth", raw=True).strip()
                    self.edge_expiry = time.monotonic() + 7 * 60
                headers["Authorization"] = "Bearer " + self.edge_token
            else:
                url = url.rstrip("/")
                if not url.endswith("/translate"):
                    url += "/translate"
                headers["Ocp-Apim-Subscription-Key"] = key
                if profile.get("region"):
                    headers["Ocp-Apim-Subscription-Region"] = profile["region"]
            values = {"api-version": "3.0", "to": "zh-Hans" if target == "zh" else target}
            if source != "auto":
                values["from"] = "zh-Hans" if source == "zh" else source
            try:
                data = http.request(query(url, **values), [{"Text": part} for part in parts], headers)
            except Exception:
                if provider == "edge":
                    self.edge_expiry = 0
                raise
            translated = [row["translations"][0]["text"] for row in data]
            if source == "auto" and data:
                detected = data[0].get("detectedLanguage", {}).get("language", detected).split("-")[0].lower()
        elif provider == "zvo":
            mapping = {"zh": "chinese_simplified", "en": "english", "ja": "japanese", "ko": "korean",
                       "fr": "french", "de": "deutsch", "es": "spanish", "ru": "russian"}
            form = {"from": mapping.get(source, source), "to": mapping.get(target, target),
                    "lowercase": "0", "text": json.dumps(parts, ensure_ascii=False)}
            if key:
                form["key"] = key
            data = http.request(url, form, form=True)
            if data.get("result") != 1:
                raise ValueError("Zvo is unavailable or rate-limited")
            translated = data["text"]
        elif provider == "deepl":
            if not profile.get("endpoint") and not key.endswith(":fx"):
                url = "https://api.deepl.com"
            url = url.rstrip("/")
            if not url.endswith("/v2/translate"):
                url += "/v2/translate"
            translated = []
            for offset in range(0, len(parts), 50):
                batch = parts[offset:offset + 50]
                body = {"text": batch, "target_lang": target.upper()}
                if source != "auto":
                    body["source_lang"] = source.upper()
                data = http.request(url, body, {"Authorization": "DeepL-Auth-Key " + key})
                rows = data["translations"]
                if len(rows) != len(batch):
                    raise ValueError("DeepL returned incomplete text blocks")
                translated.extend(row["text"] for row in rows)
                if source == "auto" and rows:
                    detected = rows[0].get("detected_source_language", detected).lower()
        else:
            url = url.rstrip("/")
            if not url.endswith("/chat/completions"):
                url += "/chat/completions"
            model = profile.get("model") or {"deepseek": "deepseek-flash", "hunyuan": "hunyuan-turbos-latest"}.get(provider, "gpt-4o-mini")
            prompt = (f"Translate from {LANGUAGES.get(source, source)} into {LANGUAGES[target]}. "
                      "Input is a JSON array of independent screen text blocks. Return a JSON array "
                      "of translated strings with exactly the same length and order. Preserve "
                      "internal line breaks and identifiers. No markdown or explanations.")
            body = {"model": model, "temperature": 0, "messages": [
                {"role": "system", "content": prompt},
                {"role": "user", "content": json.dumps(parts, ensure_ascii=False)}]}
            data = http.request(url, body, {"Authorization": "Bearer " + key})
            content = data["choices"][0]["message"]["content"].strip()
            content = re.sub(r"^```(?:json)?\s*|\s*```$", "", content, flags=re.I)
            translated = json.loads(content)
        if not isinstance(translated, list) or len(translated) != len(parts) or any(not isinstance(p, str) or not p.strip() for p in translated):
            raise ValueError("The service returned incomplete text blocks. Try again.")
        translated = [original if re.fullmatch(r"\d+(?:[.,]\d+)*(?:[%+])?", original.strip()) else value
                      for original, value in zip(parts, translated)]
        return {"source": "\n".join(parts), "translated": "\n".join(translated),
                "segments": translated, "sourceLanguage": detected, "targetLanguage": target}

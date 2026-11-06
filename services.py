"""Translation routes and current-user encrypted, independent profiles."""
import base64
import ctypes
import datetime
import html
import hashlib
import http.cookiejar
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
NAMES = ("MyMemory", "Google", "Edge", "translate.js", "Microsoft", "DeepL",
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
        primary = selected(settings)
        profile = {**self.get(primary), "proxy": settings.get("proxy", "").strip(),
                   "fallbackEnabled": settings.get("fallbackEnabled", True),
                   "fallbackRecoveryMinutes": settings.get("fallbackRecoveryMinutes", 10)}
        profile["fallbackProfiles"] = [
            self.get(provider) for provider in ("zvo", "mymemory", "google", "edge", *PROVIDERS[4:])
            if provider != primary and (provider in PROVIDERS[:4] or self.stored.get(provider, {}).get("secret"))]
        return profile

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
        destination = urllib.parse.urlsplit(newurl)
        if destination.hostname in ("www.google.com", "translate.google.com") and destination.path.startswith("/sorry"):
            raise ValueError("HTTP 429")
        origin = urllib.parse.urlsplit(req.full_url)
        if (origin.hostname in ("www.bing.com", "cn.bing.com") and
                destination.hostname in ("www.bing.com", "cn.bing.com") and
                destination.scheme == "https" and destination.path == "/translator" and not req.data):
            return super().redirect_request(req, fp, code, msg, headers, newurl)
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
        self.opener = urllib.request.build_opener(handler, NoRedirect(), urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        self.deadline = time.monotonic() + budget

    def request(self, url, body=None, headers=None, form=False, raw=False):
        validate_url(url)
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Translation timed out")
        h = {"User-Agent": "WinShun-screenshot-translate/0.3.0", "Accept": "application/json,text/plain"}
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
        self.health_file = usage.file.parent / "service-health.json"
        self.health_lock = threading.RLock()
        self.health = {}
        try:
            value = json.loads(self.health_file.read_text(encoding="utf-8"))
            self.health = value.get("services", {})
            if not isinstance(self.health, dict):
                self.health = {}
        except (OSError, ValueError):
            pass

    def translate(self, profile, parts, source="auto", target="zh"):
        primary = profile["provider"]
        enabled = bool(profile.get("fallbackEnabled", False))
        candidates = [profile]
        if enabled:
            candidates += [{**p, "proxy": profile.get("proxy", "")} for p in profile.get("fallbackProfiles", [])]
        try:
            recovery = max(1, min(1440, float(profile.get("fallbackRecoveryMinutes", 10)))) * 60
        except (ValueError, TypeError):
            recovery = 600
        deadline, failure = time.monotonic() + (20 if enabled or primary == "edge" else 8), None
        attempted, seen = [], set()
        for candidate in candidates:
            provider = candidate["provider"]
            if provider in seen:
                continue
            seen.add(provider)
            # A changed endpoint, proxy or credential gets a fresh health record.
            identity = hashlib.sha256(json.dumps([provider, candidate.get("endpoint", ""),
                        candidate.get("proxy", ""), candidate.get("apiKey", "")]).encode()).hexdigest()
            with self.health_lock:
                entry = self.health.get(identity, {})
                if enabled and float(entry.get("failedAt", 0)) + recovery > time.time():
                    continue
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            attempted.append(provider)
            try:
                result = self._translate({**candidate, "_requestBudget": min(14 if provider == "edge" else 8, remaining)}, parts, source, target)
            except (ValueError, TimeoutError, urllib.error.URLError) as ex:
                failure = ex
                if not enabled:
                    raise
                with self.health_lock:
                    self.health[identity] = {"provider": provider, "failedAt": time.time(),
                                             "retryAt": time.time() + recovery}
                    self.save_health()
                continue
            with self.health_lock:
                self.health.pop(identity, None)
                self.save_health()
            result.update(provider=provider, preferredProvider=primary, fallback=provider != primary)
            return result
        if failure is not None:
            raise failure
        if enabled and not attempted:
            raise ValueError("所有可用渠道暂时失败，恢复时间到后会重试；修改渠道配置可立即重试")
        raise TimeoutError("Translation timed out")

    def save_health(self):
        # Only service names and recovery timestamps, never text or credentials.
        now = time.time()
        self.health = {key: value for key, value in self.health.items()
                       if isinstance(value, dict) and float(value.get("retryAt", 0)) > now - 86400}
        try:
            atomic_json(self.health_file, {"version": 1, "services": self.health})
        except OSError:
            pass

    def _translate(self, profile, parts, source="auto", target="zh"):
        provider, key = profile["provider"], profile.get("apiKey", "")
        if provider not in PROVIDERS:
            raise ValueError("Unknown translation provider")
        if provider not in PROVIDERS[:4] and not key:
            raise ValueError("API key is empty. Configure this service in Settings.")
        if provider == "doubao" and not profile.get("model"):
            raise ValueError("Doubao needs a model or inference endpoint ID")
        log_request(provider, len(parts))
        http = Http(profile.get("proxy", ""), profile.get("_requestBudget", 8))
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
                for client in ("dict-chrome-ex", "chrome-extension", "gtx"):
                    for endpoint in urls:
                        try:
                            data = http.request(query(endpoint, client=client, sl=origin, tl=destination, dt="t", q=text),
                                                headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                                                         "Accept": "application/json,text/plain,*/*",
                                                         "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8"})
                            return "".join(row[0] or "" for row in data[0])
                        except (ValueError, urllib.error.URLError) as ex:
                            failure = ex
                raise failure
            with ThreadPoolExecutor(max_workers=4) as pool:
                translated = list(pool.map(one, parts))
        elif provider in ("microsoft", "edge"):
            headers = {}
            if provider == "edge":
                browser_headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}
                if time.monotonic() >= self.edge_expiry:
                    try:
                        auth_http = Http(profile.get("proxy", ""), min(2.5, profile.get("_requestBudget", 8)))
                        self.edge_token = auth_http.request("https://edge.microsoft.com/translate/auth", headers=browser_headers, raw=True).strip()
                    except (ValueError, TimeoutError):
                        return self.bing_translate(http, parts, source, target, browser_headers)
                    if not self.edge_token:
                        raise ValueError("Edge returned an empty token")
                    self.edge_expiry = time.monotonic() + 7 * 60
                headers.update(browser_headers)
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

    def bing_translate(self, http, parts, source, target, headers):
        page_url = query("https://www.bing.com/translator", timestamp=str(int(time.time() * 1000)))
        headers = {**headers, "Cache-Control": "no-cache", "Referer": "https://www.bing.com/translator",
                   "Origin": "https://www.bing.com"}
        page = http.request(page_url, headers=headers, raw=True)
        helper = re.search(r"params_AbusePreventionHelper\s*=\s*(\[[^\]]+\])", page)
        ig = re.search(r'IG:"([^"]+)"', page)
        if not helper or not ig:
            raise ValueError("微软免费翻译入口暂不可用，请稍后重试")
        key, token, *unused = json.loads(helper.group(1))
        iid = re.search(r'id="tta_outGDCont"[^>]*data-iid="(translator[^"]+)"', page)
        if not iid:
            iid = re.search(r'data-iid="(translator[^"]+)"', page)
        translated = []
        for index, part in enumerate(parts):
            body = {"fromLang": "auto-detect" if source == "auto" else "zh-Hans" if source == "zh" else source,
                    "to": "zh-Hans" if target == "zh" else target, "text": part,
                    "key": key, "token": token}
            data = http.request(query("https://www.bing.com/ttranslatev3", isVertical="1",
                                      IG=ig.group(1), IID=(iid.group(1) if iid else "translator.5028") + f".{index + 1}"),
                                body, headers, form=True)
            if not isinstance(data, list) or not data or not data[0].get("translations"):
                raise ValueError("微软免费翻译返回不完整，请稍后重试")
            translated.append(data[0]["translations"][0]["text"])
        return {"source": "\n".join(parts), "translated": "\n".join(translated), "segments": translated,
                "sourceLanguage": detect("\n".join(parts)) if source == "auto" else source, "targetLanguage": target}


def log_request(provider, blocks):
    # Counts and channel names only; no OCR text, keys, proxy or request body.
    import sys
    print(f"[screenshot-translate] translate: provider={provider} blocks={blocks}", file=sys.stderr, flush=True)

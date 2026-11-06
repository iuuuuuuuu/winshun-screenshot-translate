"""Public synthetic requests; no saved profiles, credentials or user text."""
import json
from pathlib import Path
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from services import Http, query

proxy = "http://127.0.0.1:7890"
headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
           "Accept": "application/json,text/plain,*/*", "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8"}
for client in ("dict-chrome-ex", "chrome-extension", "gtx"):
    for endpoint in ("https://translate.googleapis.com/translate_a/single", "https://translate.google.com/translate_a/single"):
        try:
            data = Http(proxy).request(query(endpoint, client=client, sl="en", tl="zh-CN", dt="t", q="Hello world"), headers=headers)
            print(json.dumps({"provider": "google", "client": client, "endpoint": endpoint, "success": bool(data[0])}))
        except Exception as ex:
            print(json.dumps({"provider": "google", "client": client, "endpoint": endpoint, "error": str(ex)}))
for endpoint in ("https://edge.microsoft.com/translate/auth", "https://www.bing.com/translator", "https://cn.bing.com/translator"):
    try:
        text = Http(proxy).request(endpoint, headers=headers, raw=True)
        # Never print tokens or complete HTML.
        print(json.dumps({"endpoint": endpoint, "bytes": len(text), "hasBingKey": bool(re.search("params_AbusePreventionHelper", text))}))
    except Exception as ex:
        print(json.dumps({"endpoint": endpoint, "error": str(ex)}))

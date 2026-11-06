"""Inspect public Bing request metadata without printing authentication tokens."""
import json
from pathlib import Path
import re
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from services import Http, query

for host in ("cn.bing.com", "www.bing.com"):
    try:
        http = Http("http://127.0.0.1:7890", 20)
        headers = {"User-Agent": "Mozilla/5.0", "Referer": f"https://{host}/translator",
                   "Origin": f"https://{host}"}
        page = http.request(query(f"https://{host}/translator", t=int(time.time() * 1000)), headers=headers, raw=True)
        helper = json.loads(re.search(r"params_AbusePreventionHelper\s*=\s*(\[[^\]]+\])", page)[1])
        ig = re.search(r'IG:"([^"]+)"', page)[1]
        iid = re.search(r'id="tta_outGDCont"[^>]*data-iid="(translator[^"]+)"', page)
        if not iid:
            iid = re.search(r'data-iid="(translator[^"]+)"', page)
        print({"host": host, "keyAgeSeconds": round(time.time() - int(helper[0]) / 1000),
               "iid": iid[1] if iid else "none", "hasIG": bool(ig)}, flush=True)
        result = http.request(query(f"https://{host}/ttranslatev3", IG=ig,
                                    IID=(iid[1] if iid else "translator.5028") + ".1", isVertical=1),
                              {"fromLang": "en", "to": "zh-Hans", "text": "Hello world",
                               "key": helper[0], "token": helper[1]}, headers=headers, form=True)
        print(result, flush=True)
    except Exception as ex:
        print({"host": host, "error": str(ex)}, flush=True)

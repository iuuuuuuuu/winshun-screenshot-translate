import datetime
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import threading
import unittest
from unittest.mock import patch, Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from main import App, DEFAULTS, TriggerGate, VERSION
from services import ENDPOINTS, PROVIDERS, Http, Profiles, Translator, Usage, detect, selected, target_for, chunks_utf8
from spatial import group, refine


class CoreTests(unittest.TestCase):
    def test_explicit_recapture_waits_for_cancelled_job_without_replay(self):
        app = App(headless=True)
        release = threading.Event()
        done = Mock()
        app.start_shot = Mock()
        app.launch(lambda job: release.wait(2), done)
        job = app.job
        app.retry_shot()
        self.assertTrue(job.cancelled.is_set())
        app.start_shot.assert_not_called()
        release.set()
        job.thread.join(3)
        self.assertFalse(job.thread.is_alive())
        app.tick()
        app.start_shot.assert_called_once_with("region")
        done.assert_not_called()
        self.assertFalse(app.pending_retry)

    def test_google_rate_limit_retries_other_clients_like_original_script(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(Http, "request", side_effect=ValueError("HTTP 429")) as request:
                with self.assertRaisesRegex(ValueError, "HTTP 429"):
                    Translator(Usage(directory)).translate({"provider": "google"}, ["Hello"], "en", "zh")
                self.assertEqual(request.call_count, 6)
                self.assertTrue(all(call.kwargs["headers"]["User-Agent"].startswith("Mozilla/")
                                    for call in request.call_args_list))

    def test_ocr_refinement_preserves_real_prefixes_and_numeric_badges(self):
        def block(text, box):
            return {"text": text, "box": box, "glyphHeight": box[3]}
        original = [block("B2 Agents", [10, 5, 110, 18]), block("499", [160, 5, 25, 11])]
        enlarged = [block("Agents", [35, 5, 85, 18]), block("Pull requests 499", [35, 5, 150, 18])]
        self.assertEqual(refine(original, enlarged), original)
        icon = block("G2 Agents", [265, 69, 85, 20])
        clean = block("Agents", [296.5, 71.5, 54, 18])
        self.assertEqual(refine([icon], [clean]), [clean])
        # An overlapping fragment must not win over the best matching full label.
        typo = block("Screenshot translaton", [5, 5, 230, 20])
        correct = block("Screenshot translation", [5, 5, 230, 20])
        self.assertEqual(refine([typo], [block("transformation", [90, 5, 120, 20]), correct]), [correct])

    def test_combined_provider_setting_and_legacy_input(self):
        manifest = json.loads((Path(__file__).parent / "plugin-0.3.3.json").read_text(encoding="utf-8"))
        settings = {entry["key"]: entry for entry in manifest["settings"]}
        self.assertEqual(manifest["version"], VERSION)
        self.assertNotIn("aiProvider", settings)
        self.assertNotIn("aiProvider", DEFAULTS)
        self.assertEqual([option["value"] for option in settings["provider"]["options"]], list(PROVIDERS))
        for provider in PROVIDERS:
            self.assertEqual(selected({"provider": provider, "aiProvider": "hunyuan"}), provider)
        for provider in PROVIDERS[6:]:
            self.assertEqual(selected({"provider": "ai", "aiProvider": provider}), provider)
        self.assertEqual(selected({"provider": "ai", "aiProvider": "invalid"}), "openai")
        self.assertEqual(selected({"provider": "invalid"}), "mymemory")

    def test_dominant_language(self):
        self.assertEqual(detect("Pull requests Agents Security and quality \u4e2d"), "en")
        self.assertEqual(target_for("Pull requests \u4e2d"), "zh")
        self.assertEqual(detect("\u4f60\u597d\u4e16\u754c"), "zh")
        self.assertEqual(detect("\u3053\u3093\u306b\u3061\u306f"), "ja")
        self.assertEqual(detect("\ud55c\uad6d\uc5b4"), "ko")

    def test_trigger_gate_busy_stale_expired(self):
        now = [0.]
        gate = TriggerGate(lambda: now[0])
        old = gate.receive()
        self.assertTrue(gate.begin())
        for _ in range(300):
            self.assertEqual(gate.reason(gate.receive()), "busy")
        gate.end()
        self.assertEqual(gate.reason(old), "completed-operation")
        fresh = gate.receive()
        now[0] += 1.1
        self.assertEqual(gate.reason(fresh), "expired")
        self.assertEqual(gate.reason(gate.receive()), "")

    def test_spatial_labels_icons_paragraphs(self):
        words = [{"text": text, "box": box} for text, box in [
            ("Repository", [35, 5, 90, 18]), ("0", [5, 45, 18, 18]),
            ("Pull", [35, 45, 28, 18]), ("requests", [69, 45, 52, 18]),
            ("Actions", [185, 45, 55, 18]), ("Security", [310, 45, 62, 18]),
            ("and", [378, 45, 21, 18]), ("quality", [405, 45, 42, 18])]]
        blocks = group(words)
        self.assertEqual([b["text"] for b in blocks], ["Repository", "Pull requests", "Actions", "Security and quality"])
        self.assertEqual(blocks[1]["box"][0], 35)
        blocks = group([{"text": "First paragraph line", "box": [35, 5, 150, 18]},
                        {"text": "Second paragraph line", "box": [35, 31, 170, 18]}])
        self.assertEqual(blocks[0]["text"], "First paragraph line\nSecond paragraph line")

    @unittest.skipUnless(os.name == "nt", "Windows DPAPI")
    def test_profiles_compatible_encryption_and_isolation(self):
        with tempfile.TemporaryDirectory() as directory:
            store = Profiles(directory)
            store.save({"openai": {"apiKey": "fake-secret-a", "model": "model-a"},
                        "deepseek": {"apiKey": "fake-secret-b", "model": "model-b"}})
            loaded = Profiles(directory)
            settings = {"provider": "ai", "aiProvider": "deepseek", "proxy": "http://127.0.0.1:8080"}
            self.assertEqual(loaded.current(settings)["apiKey"], "fake-secret-b")
            self.assertEqual(loaded.current(settings)["proxy"], settings["proxy"])
            loaded.save({"openai": {"apiKey": "changed-a", "model": "new-a"}})
            self.assertEqual(loaded.current(settings)["apiKey"], "fake-secret-b")
            self.assertEqual(selected(settings), "deepseek")
            raw = store.file.read_text(encoding="utf-8")
            self.assertNotIn("fake-secret", raw)
            self.assertNotIn('"active"', raw)
            self.assertNotIn('"proxy"', raw)
            # .NET's ProtectedData uses the same DPAPI bytes and entropy.
            import subprocess
            command = ("Add-Type -AssemblyName System.Security; "
                       "$d=Get-Content -Raw -LiteralPath $env:TEST_PROFILES_FILE|ConvertFrom-Json; "
                       "$b=[Convert]::FromBase64String($d.profiles.deepseek.secret); "
                       "$e=[Text.Encoding]::UTF8.GetBytes('iuuuuuuuu.screenshot-translate/profiles/v1'); "
                       "$s=[Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect($b,$e,'CurrentUser')); "
                       "if($s -ne 'fake-secret-b'){exit 1}")
            executable = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
            check = subprocess.run([str(executable), "-NoProfile", "-NonInteractive", "-Command", command],
                                   env=dict(os.environ, TEST_PROFILES_FILE=str(store.file)),
                                   capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
            self.assertEqual(check.returncode, 0)
            self.assertEqual(loaded.get("openai")["apiKey"], "changed-a")

    def test_usage_unicode_and_day_reset(self):
        date = [datetime.date(2026, 11, 5)]
        with tempfile.TemporaryDirectory() as directory:
            usage = Usage(directory, lambda: date[0])
            usage.record("Hello")
            usage.record("\u4f60\u597d\U0001f600")
            self.assertEqual(usage.today(), 8)
            self.assertEqual(Usage(directory, lambda: date[0]).today(), 8)
            date[0] += datetime.timedelta(days=1)
            self.assertEqual(usage.today(), 0)

    def test_utf8_chunks(self):
        parts = chunks_utf8("\u4e2d" * 600 + "\U0001f600")
        self.assertEqual("".join(parts), "\u4e2d" * 600 + "\U0001f600")
        self.assertTrue(all(len(p.encode()) <= 500 for p in parts))

    def test_all_ten_routes_and_block_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            translator = Translator(Usage(directory))
            source = ["First\nSecond", "Another", "499"]
            for provider in ENDPOINTS:
                calls = []
                def request(http, url, body=None, headers=None, form=False, raw=False):
                    calls.append((url, body, headers or {}))
                    if raw:
                        return "fake-edge-token"
                    if provider == "google":
                        return [[["Translated\nLine"]], None, "en"]
                    if provider == "mymemory":
                        return {"responseStatus": 200, "responseData": {"translatedText": "Translated\nLine"}}
                    if provider in ("edge", "microsoft"):
                        return [{"translations": [{"text": "Translated\nLine"}]} for _ in body]
                    if provider == "deepl":
                        return {"translations": [{"text": "Translated\nLine"} for _ in body["text"]]}
                    if provider == "zvo":
                        return {"result": 1, "text": ["Translated\nLine"] * len(json.loads(body["text"]))}
                    return {"choices": [{"message": {"content": json.dumps(["Translated\nLine"] * 3)}}]}
                profile = {"provider": provider, "apiKey": "test-only", "model": "test-model", "proxy": ""}
                with patch.object(Http, "request", request):
                    result = translator.translate(profile, source, "en", "zh")
                self.assertEqual(len(result["segments"]), 3, provider)
                self.assertEqual(result["segments"][0], "Translated\nLine", provider)
                self.assertEqual(result["segments"][2], "499", provider)
                if provider not in ("mymemory", "google", "zvo"):
                    self.assertTrue(any(headers for _, _, headers in calls), provider)

    def test_deepl_batches_and_invalid_response(self):
        with tempfile.TemporaryDirectory() as directory:
            translator = Translator(Usage(directory))
            sizes = []
            def request(http, url, body, headers):
                sizes.append(len(body["text"]))
                return {"translations": [{"text": part} for part in body["text"]]}
            with patch.object(Http, "request", request):
                data = translator.translate({"provider": "deepl", "apiKey": "test-only"}, [f"Part {i}" for i in range(52)], "en", "zh")
            self.assertEqual(sizes, [50, 2])
            self.assertEqual(len(data["segments"]), 52)
            with patch.object(Http, "request", return_value={"result": 1, "text": ["short"]}):
                with self.assertRaisesRegex(ValueError, "incomplete"):
                    translator.translate({"provider": "zvo"}, ["a", "b"])

    def test_proxy_validation_and_shared_clearing(self):
        with patch.dict(os.environ, {}, clear=True):
            http = Http("http://127.0.0.1:8080")
            handler = next(h for h in http.opener.handlers if hasattr(h, "proxies"))
            self.assertEqual(handler.proxies["https"], "http://127.0.0.1:8080")
            for invalid in ("socks5://localhost:1", "http://user:pass@localhost:1"):
                with self.assertRaises(ValueError):
                    Http(invalid)
            self.assertIsNotNone(Http(""))

    def test_shutdown_and_pin_cancellation_are_scoped(self):
        from main import Job
        app = App(headless=True)
        app.job = Job()
        app.job.pin = object()
        app.cancel_pin(object())
        self.assertFalse(app.job.cancelled.is_set())
        job = app.job
        app.gate.begin()
        app.cancel_pin(job.pin)
        self.assertTrue(job.cancelled.is_set())
        self.assertIsNone(app.job)
        self.assertFalse(app.gate.busy)


if __name__ == "__main__":
    unittest.main()

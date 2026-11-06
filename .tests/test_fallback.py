import json
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from services import Translator, Usage, Profiles, Http


class FallbackTests(unittest.TestCase):
    def test_persistent_cooldown_recovery_and_unchanged_preference(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = {"provider": "google", "fallbackEnabled": True, "fallbackRecoveryMinutes": 5,
                       "fallbackProfiles": [{"provider": "zvo"}]}
            calls = []
            def request(candidate, parts, source, target):
                calls.append(candidate["provider"])
                if candidate["provider"] == "google" and len(calls) == 1:
                    raise ValueError("HTTP 429")
                return {"source": "Hello", "translated": "你好", "segments": ["你好"]}
            with patch("services.time.time", return_value=1000):
                translator = Translator(Usage(directory))
                with patch.object(translator, "_translate", request):
                    result = translator.translate(profile, ["Hello"])
                    self.assertTrue(result["fallback"])
                    self.assertEqual(result["provider"], "zvo")
            # New process/instance remembers the unavailable primary.
            with patch("services.time.time", return_value=1100):
                translator = Translator(Usage(directory))
                with patch.object(translator, "_translate", request):
                    result = translator.translate(profile, ["Hello"])
                    self.assertEqual(result["provider"], "zvo")
            with patch("services.time.time", return_value=1301):
                with patch.object(translator, "_translate", request):
                    result = translator.translate(profile, ["Hello"])
                    self.assertFalse(result["fallback"])
                    self.assertEqual(result["provider"], "google")
            self.assertEqual(calls, ["google", "zvo", "zvo", "google"])
            self.assertEqual(profile["provider"], "google")
            stored = translator.health_file.read_text()
            self.assertNotIn("Hello", stored)
            self.assertNotIn("你好", stored)

    def test_modified_configuration_and_interval_retry_immediately(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = {"provider": "google", "apiKey": "fake-secret", "fallbackEnabled": True,
                       "fallbackRecoveryMinutes": 10, "fallbackProfiles": [{"provider": "zvo"}]}
            translator = Translator(Usage(directory))
            def request(candidate, *args):
                if candidate["provider"] == "google":
                    raise ValueError("HTTP 429")
                return {"translated": "test"}
            with patch("services.time.time", return_value=1000), patch.object(translator, "_translate", request):
                translator.translate(profile, ["synthetic"])
            self.assertNotIn("fake-secret", translator.health_file.read_text())
            with patch("services.time.time", return_value=1100), patch.object(translator, "_translate", return_value={}) as call:
                translator.translate({**profile, "proxy": "http://localhost:1"}, ["synthetic"])
                self.assertEqual(call.call_args.args[0]["provider"], "google")
                translator.translate({**profile, "fallbackRecoveryMinutes": 1}, ["synthetic"])
                self.assertEqual(call.call_args.args[0]["provider"], "google")

    def test_disabled_fallback_all_failed_and_configured_candidates(self):
        with tempfile.TemporaryDirectory() as directory:
            profiles = Profiles(directory)
            profiles.save({"openai": {"apiKey": "fake-test-key"}})
            selected = profiles.current({"provider": "edge", "proxy": "http://localhost:1", "fallbackEnabled": True})
            ids = [p["provider"] for p in selected["fallbackProfiles"]]
            self.assertIn("openai", ids)
            self.assertNotIn("deepseek", ids)
            translator = Translator(Usage(directory))
            with patch.object(translator, "_translate", side_effect=ValueError("HTTP 429")) as request:
                with self.assertRaises(ValueError):
                    translator.translate({"provider": "google", "fallbackEnabled": False}, ["Hello"])
                self.assertEqual(request.call_count, 1)
                with self.assertRaises(ValueError):
                    translator.translate(selected, ["Hello"])
                count = request.call_count
                with self.assertRaisesRegex(ValueError, "恢复时间"):
                    translator.translate(selected, ["Hello"])
                self.assertEqual(request.call_count, count)

    def test_edge_auth_failure_uses_bing_and_keeps_blocks(self):
        with tempfile.TemporaryDirectory() as directory:
            def request(http, url, body=None, headers=None, form=False, raw=False):
                if "/translate/auth" in url:
                    raise ValueError("HTTP 404")
                if "/translator" in url:
                    return 'IG:"test-ig"; params_AbusePreventionHelper = [123,"fake-public-token",60000]; data-iid="translator.5028"'
                self.assertTrue(form)
                self.assertEqual(body["to"], "zh-Hans")
                return [{"translations": [{"text": "译文"}]}]
            with patch.object(Http, "request", request):
                result = Translator(Usage(directory)).translate({"provider": "edge"}, ["First", "Second"], "en", "zh")
            self.assertEqual(result["segments"], ["译文", "译文"])
            self.assertEqual(result["provider"], "edge")


if __name__ == "__main__":
    unittest.main()

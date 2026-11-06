import gc
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from desktop import Worker
from fixtures import image_fixture, memory_stats
from main import App
from main import UiWorkerApp
from services import Profiles, Usage
from ui import Pin, Settings


@unittest.skipUnless(os.name == "nt", "Windows UI and OCR")
class WindowsTests(unittest.TestCase):
    def test_real_ocr_worker_exits_and_bounds(self):
        image = image_fixture()
        worker = Worker()
        blocks = worker.recognize(image)
        self.assertIsNone(worker.process)
        text = "\n".join(b["text"] for b in blocks).lower()
        self.assertIn("hello", text)
        self.assertIn("translation", text)
        for block in blocks:
            x, y, width, height = block["box"]
            self.assertGreaterEqual(x, 0)
            self.assertGreaterEqual(y, 0)
            self.assertLessEqual(x + width, image.width)
            self.assertLessEqual(y + height, image.height)

    def test_hidden_windows_release_images_and_settings_isolate(self):
        app = App()
        with tempfile.TemporaryDirectory() as directory:
            app.profiles, app.usage = Profiles(directory), Usage(directory)
            app.profiles.save({"openai": {"apiKey": "fake-test", "model": "model-a"}})
            baseline = set(app.root.tk.call("image", "names"))
            for i in range(10):
                image = image_fixture(600 if i % 2 == 0 else 1920, 180 if i % 2 == 0 else 1080)
                pin = Pin(app, image)
                self.assertFalse(pin.window.winfo_viewable())
                pin.set_result([{"text": "Hello", "box": [24, 20, 90, 28], "glyphHeight": 28}],
                               {"source": "Hello", "translated": "\u4f60\u597d", "segments": ["\u4f60\u597d"],
                                "sourceLanguage": "en", "targetLanguage": "zh"})
                pin.compare.set(True)
                pin.redraw()
                self.assertGreater(float(pin.canvas.cget("scrollregion").split()[-1]), image.height)
                pin.close()
                self.assertIsNone(pin.image)
                self.assertIsNone(pin.photo)
                del pin, image
                app.root.update()
            gc.collect()
            self.assertEqual(set(app.root.tk.call("image", "names")), baseline)
            config = Settings(app)
            self.assertFalse(config.window.winfo_viewable())
            config.arrange_tabs(740)
            narrow_columns = config.tab_columns
            self.assertEqual(len(config.tab_buttons), 10)
            self.assertLess(narrow_columns, 10)
            for index, button in enumerate(config.tab_buttons):
                position = button.grid_info()
                self.assertEqual(position["row"], index // narrow_columns)
                self.assertEqual(position["column"], index % narrow_columns)
                self.assertLessEqual(button.winfo_reqwidth() + 4, (740 - 48) / narrow_columns)
            config.arrange_tabs(1500)
            self.assertEqual(config.tab_columns, 10)
            config.channel.set("openai")
            config.select()
            config.entries["model"].set("cancelled-draft")
            config.channel.set("deepseek")
            config.select()
            self.assertEqual(config.entries["apiKey"].get(), "")
            config.channel.set("openai")
            config.select()
            self.assertEqual(config.entries["model"].get(), "cancelled-draft")
            config.close()
            self.assertEqual(app.profiles.get("openai")["model"], "model-a")
            self.assertEqual(app.active_provider(), "mymemory")
        app.root.destroy()

    def test_ui_worker_auto_exit_and_cache_before_launcher(self):
        app = UiWorkerApp()
        sent = []
        app.send = sent.append
        app.cache["clip"] = (0, [{"id": "translation", "title": "test-only"}])
        app.tool("show_launcher", {"text": "jft just now"})
        self.assertEqual(sent[0]["method"], "ui/cache")
        self.assertEqual(sent[1]["method"], "winshun/tool")
        self.assertEqual(app.cache, {})
        app.started = True
        app.tick()
        self.assertFalse(app.running)
        self.assertTrue(sent[-1]["params"]["closing"])
        self.assertFalse(app.root.winfo_viewable())
        app.root.destroy()


if __name__ == "__main__":
    unittest.main()

"""Refresh the shared WinShun controls from the inspected host source."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
source = root / ".host/WinShun"
destination = root / "qml/WinShun"
destination.mkdir(parents=True, exist_ok=True)
for name in ("FlatButton.qml", "InputBox.qml", "ChoiceTabs.qml"):
    text = (source / "src/app/qml" / name).read_text(encoding="utf-8")
    (destination / name).write_text(
        "// From LingCore/WinShun v0.20.0 (MIT); see THIRD_PARTY_NOTICES.md.\n" + text,
        encoding="utf-8")
fonts = root / "assets"
fonts.mkdir(exist_ok=True)
shutil.copyfile(source / "third_party/phosphor/Phosphor.ttf", fonts / "Phosphor.ttf")
notices = "# Shared UI licenses\n\n"
for label, name in (("WinShun", "LICENSE"), ("Phosphor Icons", "third_party/phosphor/LICENSE")):
    notices += f"## {label}\n\n" + (source / name).read_text(encoding="utf-8") + "\n\n"
notices += (
    "FlatButton.qml and InputBox.qml are copied from LingCore/WinShun tag v0.20.0 "
    "(460f7be). ChoiceTabs.qml is the wrapping host control from the local host repair. "
    "Theme.qml uses the same host color and radius tokens. Glyph rendering adapts "
    "WinShun's Phosphor outline renderer for PySide. The screenshot and service pages "
    "are plugin-specific. No host settings data or service credentials are bundled.\n")
(root / "THIRD_PARTY_NOTICES.md").write_text(notices, encoding="utf-8")

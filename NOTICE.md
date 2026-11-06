# Attribution

WinShun is developed by LingCore: <https://github.com/LingCore/WinShun>.
Its source and plugin documentation are MIT licensed, copyright 2026 LingCore.
The MIT permission notice is included in LICENSE.

This independent plugin follows the documented JSON-RPC protocol and refers to:

- `docs/plugins.md` and `src/translate-plugin`: service request formats, endpoint defaults and error conventions.
- `src/app/platform/TextRecognizer.cpp`: CJK spacing rules, reimplemented in Python.
- `src/capture-plugin/qml/CaptureSettingsWindow.qml`, `src/app/qml/Theme.qml`,
  `ScopeTabs.qml`, `TitleBar.qml` and `SettingRow.qml`: visual conventions and dimensions.
- `src/capture` and `src/capture-plugin`: virtual-screen selection and pinning concepts.

These rules and designs are implemented with Python, Qt/QML and Windows APIs;
the short-lived PowerShell OCR worker uses system .NET/WinRT assemblies only.
The native 0.1.10 implementation is retained in .native/ for source history and
is excluded from community packages.
The package includes WinShun's MIT-licensed FlatButton, InputBox, wrapping choice
control and adapted theme tokens, plus the MIT-licensed Phosphor regular font.
Their complete notices and source provenance are in THIRD_PARTY_NOTICES.md.
PySide6-Essentials is an external dependency, licensed separately under its
upstream LGPL/commercial terms. No Qt DLLs or WinShun executables are bundled.

This is a community project, not an official WinShun plugin. The reserved
`winshun.` prefix is not used for its public plugin ID.

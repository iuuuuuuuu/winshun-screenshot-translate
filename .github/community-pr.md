## Screenshot Translate 0.3.3

Update the existing community entry for `iuuuuuuuu.screenshot-translate` from 0.2.1 to 0.3.3.

- Public source and release: https://github.com/iuuuuuuuu/winshun-screenshot-translate
- MIT license with retained LingCore attribution.
- Community Python entry (`broker.py`), PowerShell OCR script in UTF-8 BOM.
- No EXE, DLL, bytecode, downloaded executables or runtime compilation. The package includes MIT-licensed QML controls and the Phosphor font, with complete attribution.
- PySide6-Essentials is a new external UI dependency; installation is documented in README and release notes. The plugin does not download it.
- Official reusable community packaging workflow, versioned formal release.
- Per-service encrypted profiles; active provider and shared proxy remain host settings.

Permissions:

- `ui`: user-requested screenshot selection, translation/settings windows and host hotkeys.
- `clipboard:read` / `clipboard:write`: explicit text translation and copy actions.
- `network`: selected service requests; README lists all default endpoints and data sent.
- `files:write`: provider profiles, the local MyMemory submission estimate and service recovery metadata in `WS_DATA_DIR`.
- `processes`: short-lived reviewed Python UI script and system PowerShell OCR/UI Automation
  script. No external binary is downloaded or compiled. These workers exit when finished.

Screenshots are processed locally in memory; only recognized text goes to the chosen
translation endpoint. No diagnostic screenshots, personal configs, logs or secrets are
included in the package.

Verification:

- Official `tools/check.py`: zero errors and zero warnings.
- 28 Windows regressions cover protocol, all ten service request formats, shared
  proxy and DPAPI isolation, persistent fallback/recovery, native selection,
  private-desktop Qt rendering, outside-click input, hover/click/Esc closure,
  worker exit during blocked requests and OCR child cleanup.
- Additional GPU rendering checks cover configuration, top-level language menus,
  vertical comparison, real Windows OCR on a synthetic image and display geometry.
- Live free-service availability is variable; paid services were tested with
  synthetic protocol responses rather than real billable credentials.

This PR changes only the generated `plugins/iuuuuuuuu.screenshot-translate.json`.

## Screenshot Translate 0.2.0

First community index submission for `iuuuuuuuu.screenshot-translate`.

- Public source and release: https://github.com/iuuuuuuuu/winshun-screenshot-translate
- MIT license with retained LingCore attribution.
- Community Python entry (`broker.py`), PowerShell OCR script in UTF-8 BOM.
- No EXE, DLL, bytecode, third-party binaries, downloaded executables or runtime compilation.
- Official reusable community packaging workflow, versioned formal release.
- Per-service encrypted profiles; active provider and shared proxy remain host settings.

Permissions:

- `ui`: user-requested screenshot selection, translation/settings windows and host hotkeys.
- `clipboard:read` / `clipboard:write`: explicit text translation and copy actions.
- `network`: selected service requests; README lists all default endpoints and data sent.
- `files:write`: provider profiles and local MyMemory submission estimate in `WS_DATA_DIR`.
- `processes`: short-lived reviewed Python UI script and system PowerShell OCR/UI Automation
  script. No external binary is downloaded or compiled. These workers exit when finished.

Screenshots are processed locally in memory; only recognized text goes to the chosen
translation endpoint. No diagnostic screenshots, personal configs, logs or secrets are
included in the package.

Verification:

- Official `tools/check.py`: zero errors and zero warnings.
- 15 Windows background regressions: translation routing, proxy/profile isolation,
  DPAPI compatibility, numeric/newline preservation, 52-block DeepL batching,
  300 busy hotkeys, expiry, real OCR worker exit, broker/GUI lifecycle and hidden layout.
- Synthetic memory probe after 12 never-shown windows and 4 real OCR runs:
  resident working set 26.8 MB after worker exit and after 60 seconds idle.
  Not an overnight test or a claim about active-window memory peaks.
- Actual desktop selection, hotkey ergonomics, foreground behavior and appearance
  still require manual validation. This submission does not claim those tests ran.

This PR changes only the generated `plugins/iuuuuuuuu.screenshot-translate.json`.

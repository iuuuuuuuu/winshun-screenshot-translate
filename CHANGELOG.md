# Changelog

## 0.2.0

- Convert to a community-eligible Python entry and text-only package.
- Keep the resident broker free of tkinter, OCR and .NET runtime caches.
- Start GUI and system PowerShell OCR workers only on user requests; exit them
  after recognition or after all GUI windows close.
- Preserve public DPAPI profiles, ten provider routes, common proxy and host
  channel selection without uploading or migrating private development data.
- Preserve coordinate-based overlays, comparison, bottom toolbar and close actions.
- Drop busy/stale inputs across the broker and GUI worker boundary.
- Stop automatically retaining diagnostic screenshots.
- Use the official community checker/packer and add background regressions.

## 0.1.10

First public release of `iuuuuuuuu.screenshot-translate`.

- Preserve screenshot text-block positions, icons and numeric badges.
- Add source comparison, language selection, bottom toolbar, loading and errors.
- Support Esc and double-click to close without reopening the search launcher.
- Separate active-channel and shared-proxy settings from per-service configuration.
- Support MyMemory, Google, Edge, Zvo, DeepL, Microsoft and compatible model services.
- Encrypt provider keys with current-user Windows DPAPI.
- Improve small-text OCR candidates and dominant-language detection.
- Drop busy, completed-operation and expired triggers instead of replaying them.
- Publish native packages for manual archive installation, not the community index.

Windows OCR accuracy, free-service availability and actual desktop interactions
remain subject to the limitations documented in README.md.

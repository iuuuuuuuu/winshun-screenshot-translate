# 更新记录

## 0.4.0 — 2026-11-07

本版改为 C++ / Qt Quick 原生运行，自带 Qt 和 Visual C++ 运行库，无需安装 Python 或 PySide6。
公开包要求 Windows 10/11 x64、WinShun 0.22.0+ 和系统 OCR 语言包；按官方新规则作为未审核原生插件提交。

- 完成剪贴板翻译、划词翻译和鼠标指词 OCR 的词义小窗；支持直接编辑原文、固定窗口和跟随新选区。
- 接入 WinShun 自带的管理员插件授权，修复管理员 Codex 等软件读取不到选区的问题。
- 重试保留原目标窗口和位置，修复每点一次窗口就下移的问题。
- 修复固定小窗拖动后背景和操作控件消失；原文保留正文样式，聚焦只显示文字光标。
- 改进英文词内空格、漏字、短句和换行识别；混排时根据鼠标所在语言选择片段或整句。
- 优化多屏框选、拖动、加载动画和关闭响应；支持可配置的外部点击关闭与上下双语对照。
- 保留十个渠道、全部渠道测试、有记忆的自动降级及可配置恢复时间。
- 运行库随插件副本一起部署，管理员模式不加载可写宿主目录中的 Qt。
- 发布说明、安装与授权步骤、构建说明使用中文；安装包附源码提交、校验值和第三方许可。

验证覆盖普通/管理员权限、真实 Windows 与 Chromium 选区、编辑/固定/跟随、重试位置、截图几何、OCR 和关闭取消。
使用者确认管理员 Codex 划词恢复可用。免费服务可用性及复杂、小字号 OCR 仍受外部条件限制。

## 0.3.3 — 2026-11-06

The first public release since 0.2.1 includes the 0.2.2–0.3.2 UI, service and performance repairs below. Install the new PySide6-Essentials dependency before upgrading. Permissions and the resident Python entry remain unchanged.

- Disable hover tooltips on pin and settings close buttons so the popup cannot cover the close target; preserve accessible labels and keyboard actions.
- Run hover/click/Esc checks on a private desktop, install declared dependencies in CI, and support both single-screen CI and multi-screen geometry checks.

## 0.3.2 (local performance repair)

- Show region selection immediately from the low-memory broker using native per-monitor windows; defer loading Qt until a region is selected.
- Suppress Windows process-start cursor feedback for GUI and OCR subprocesses.
- Replace the GUI-thread mouse hook with an independent raw input sink, keeping outside-click dismissal responsive while Qt is busy.
- Hide pins before cleanup and release their trigger gate immediately when cancelled.
- Stop waiting for network threads or Python finalizers when the disposable worker closes; use a Windows process job to reap OCR children.
- Avoid repeatedly copying or rendering the same capture when toggling comparison and preferences.
- Verify native selection bounds, cross-monitor dragging, outside-click packets while Qt is blocked, quick closure during a blocked request, and child process cleanup on a private desktop.

## 0.3.1 (local repair)

- Cover each monitor with its own correctly scaled selection window; use physical cursor coordinates for cross-monitor dragging.
- Resolve the capture monitor before calculating or showing pin geometry, avoiding size jumps on monitors with different DPI.
- Stack comparison images vertically, translation first by default, with a configurable order.
- Use independent topmost language menus so every option is visible outside the narrow toolbar.
- Cache WinShun's Phosphor font outlines and avoid redundant QML state notifications.
- Add asynchronous testing of all ten service profiles with three concurrent requests, individual results and unconfigured-service skips.
- Close the pin on outside clicks by default; the behavior is configurable and keeps toolbar, menus and settings interactions open.

## 0.3.0 (local repair)

- Replace the Tk UI with a disposable Qt/QML worker using WinShun's shared buttons, fields, theme tokens and Phosphor outline icons.
- Restore animated recognition/translation feedback and a compact toolbar below the screenshot.
- Show labelled source and translation side by side when they fit, with a stacked layout for wider captures and synchronized scrolling.
- Keep all ten channels in one exclusive configuration strip and restore the translate.js name.
- Persist unavailable-service cooldowns; use free or configured alternatives and retry the preferred service after a configurable interval without changing host selection.
- Show the actual service in the toolbar after fallback.
- Restore Google's original browser headers and alternative-client retries after HTTP 429.
- Add a Microsoft Bing path when the Edge token endpoint fails. Current network validation still finds Bing HTTP 401; automatic fallback remains available.
- Validate real Windows OCR with Google failure followed by translate.js success through the configured proxy.
- Require PySide6-Essentials for the UI worker; the resident broker still loads no Qt/OCR runtime.
- Exit the worker event loop after the last window closes even when QML intercepts native close events.

## 0.2.3

- Keep the latest typed query while an earlier translation is being cancelled.
- Ignore the GUI worker's initial idle state until an operation has started.
- Release the trigger after a GUI startup failure and report unexpected exits.
- Preserve recognized screenshot blocks after a network error for source copy, language changes and retry.
- Add a per-service translation test using the shared proxy without changing the active service.
- Log startup capability, hotkey registration failures, channel names and error types without request text or keys.
- Recognize Google's rate-limit redirect and identify Edge authorization failures.
- Extend the host query deadline to cover proxy connection setup.
- Include a WinShun 0.20.0 host fix for reloading stopped eager plugins, alongside wrapping exclusive choices.

## 0.2.2

- Restore an independent toolbar below the image with language controls, comparison, service settings and close.
- Fit large captures to the current monitor's working area, clamp both windows while dragging, and show scrollbars only for overflow.
- Restore wheel opacity, labelled comparison, readable complete single-block translations and a selectable full-text view for crowded images.
- Restore compact configuration rows, wrapping exclusive tabs, masked/revealable keys and a fixed save/cancel footer.
- Preserve genuine OCR prefixes and numeric badges when refining small text.
- Wait for cancellation before an explicit recapture and stop Google client retries on HTTP 429.
- Keep the disposable UI/OCR worker architecture and public encrypted profile format.
- Include a separately tested WinShun 0.20.0 source patch in `.host/` for wrapping host choices. The plugin package cannot update the host's controls.

## 0.2.1

- Combine all ten translation services into the host's single provider setting.
- Remove the separate model-service selector; keep independent service profiles.
- Retain legacy `provider=ai` input compatibility when `aiProvider` is supplied.
- Wrap configuration tabs to keep every service visible at smaller window widths.
- Document the host's automatic menu fallback and how to identify the old runtime.

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

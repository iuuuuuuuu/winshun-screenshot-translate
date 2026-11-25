## 0.4.23

- 划词小图标改为出现在松开鼠标的位置旁，反向、跨行选择不再错误地贴到文字末尾。
- 读取期间继续移动鼠标不会带跑图标，保留多屏缩放换算与屏幕边缘避让。

## 0.4.22

- 修复划词弹窗在 125%、150%、175% 等缩放比例下底部或右侧出现黑线：窗口宽高对齐完整物理像素，保留浅色圆角边框。
- Firefox 获得焦点时提前准备选区读取接口，避免首次划词已经发生、辅助功能接口才启动导致选区漏报；不修改浏览器配置、选区或剪贴板。
- 预备读取在独立辅助进程内运行，卡住自动回收并退避重启；只保留最近一个 Firefox 窗口的接口，不读取页面正文。
- 小翻译图标与快捷键共用串行读取，快捷键优先取消图标任务并等待回收；手动重试重新获得自动补读次数。
- 新增独立 Firefox 主屏/副屏测试、五档缩放边缘检查及图标与热键并发回归。

## 0.4.21

- Google 改用批量翻译接口；Edge 改用直接翻译接口，移除失效的令牌获取流程。
- Google、Edge 同一配置串行限速，同时收到的相同请求复用结果，减少长文和连续点击造成的重复请求。
- 429 请求限流按 Retry-After 或逐次加长的短暂停顿恢复；真正额度耗尽、认证错误继续长时间冷却，暂停期间自动使用备用渠道。
- 新协议使用独立健康记录，避免旧接口留下的失败状态继续阻挡翻译；保留用户指定的主渠道和排序。
- 鼠标拖选、双击选词或 Shift 点击扩展选区后，末尾显示小翻译图标；点击才翻译，读取选区不联网、不改剪贴板。
- 图标不抢焦点，支持多行选区定位、屏幕边缘避让、滚动隐藏、过期隐藏和取消旧读取；可在设置首页关闭并保存。

## 0.4.20

- 自动权重优先使用最近成功的备用渠道，避免刚结束冷却但尚未恢复的渠道再次拖慢翻译。
- 所有备用渠道都失败时，对临时网络故障自动进行一次恢复检查，并限制重复检查频率；认证错误和限流仍保留长时间冷却。
- 冷却中的渠道仍可返回已缓存的译文；设置页显示实际失败原因与恢复检查时间。
- ECDICT 自动降级到逐条请求的渠道时，按文字分段数量分配等待时间，避免多段截图被误判为服务故障。
- MyMemory 的服务故障与额度耗尽分别处理，大段文本的分块请求使用有上限的并发。
- Google 优先使用 gtx 请求入口；启动日志版本与实际构建版本保持一致。

## 0.4.19

- 默认主渠道改为 ECDICT，并将其放在渠道列表首位；已有用户选择不会被覆盖。
- ECDICT 不支持的句子继续自动降级到可用翻译渠道。

## 0.4.18（本地候选版）

- 自动权重模式允许拖动备用渠道，拖动位置会转换成用户权重调整并参与实际降级排序。
- 始终显示高对比度六点拖拽标记；主渠道仍固定优先。
- 补充自动模式拖拽、排序和重启持久化回归。

## 0.4.17（本地候选版）

- 自动权重模式仍显示拖拽手柄，并提示切换到手动排序后才能调整备用顺序。
- 增加拖拽手柄在手动、自动模式下的显示与启用状态回归检查。

## 0.4.16（本地候选版）

- 修复手动模式下备选渠道拖动与页面滚动的冲突，拖动时显示落点。
- 标明置顶项是 Win顺选择的主渠道，自动权重模式会提示切换到手动排序。

## 0.4.15（本地候选版）

- 设置页点击保存后不再自动关闭；页脚显示“配置已保存”，并通过 WinShun 提示确认。用户可继续修改，再点右上角或取消按钮关闭。

## 0.4.14（本地候选版）

- 修复划词热键偶发首读为空：同一次热键内自动短暂补读两次，成功后直接翻译，不再要求用户再次按键。
- 确认存在权限限制时直接提示，不延迟报错；关闭、替换请求和新一轮热键会取消旧读取，旧结果不会覆盖新选区。

## 0.4.13（本地候选版）

- OCR 增加边缘留白、稀疏文字裁剪和低对比度增强，减少贴边数字、小字及浅灰文字漏识别。
- 按 Windows OCR 倾斜角还原文字框，保留原生行归属，修复同一行拆散、旋转文字顺序错误。
- 长截图扩大分块重叠范围，减少跨边界单词截断和重复；较小文字补充放大识别。
- 鼠标 OCR 增加低对比恢复路径；保留普通文字的快速识别和已有短句精读。
- 继续共用 WinShun Qt，保留翻译缓存、长文逐句处理和渠道故障退避。

## 0.4.12（本地候选版）

- 翻译提速：普通长文合并句子减少请求，后续批次最多两批并行，逐句对照保留句子顺序。
- 近期相同请求使用有界内存缓存；源/目标语言、渠道配置和释义模式分别隔离，手动诊断仍真实联网。
- 输入自动翻译等待缩短至 350 毫秒；Google 限流、认证失败及超时停止重复参数重试。
- 保留原有故障退避、部分结果、及时取消、共享 WinShun Qt 和数据格式。

## 0.4.11（本地候选版）

- 翻译面板取消 10,000 字限制，按句分段分批处理并显示进度，支持逐句原译对照、停止并保留已完成结果。
- 剪贴板长文取消整篇 25 秒硬超时，改为逐批超时并显示部分结果。
- 长截图分块 OCR 保留小字细节和坐标，深色低对比背景归一化，纯英文增加英文识别比较。
- 渠道失败至少冷却 1 小时，连续故障指数退避至最长 7 天；限流至少 6 小时，认证错误至少 24 小时。普通翻译尊重停用周期，手动渠道测试可提前恢复。
- 普通渠道每次尝试上限 4 秒，Edge 6 秒，AI 12 秒；同一批降级总等待上限 18 秒。

# 更新记录

## 0.4.10 — 2026-11-11

- 中文词语支持多英文候选，自动模式下中文短词直接进入汉英词典；有道汉英候选优先，离线 ECDICT 反查补充词义匹配和相关词组。
- 英文单词新增按词性、含义分组的同近义词，以及单独的同根词区域。
- 点击候选或近义词查详细释义，支持返回原查询；在线失败保留离线结果，尊重禁用渠道与共用代理。


## 0.4.9 — 2026-11-11

- 翻译面板增加自动、翻译、查词视图，共用原文；单词自动查词，句子自动翻译，手动选择后保持该视图。
- 英文句子可逐词查看词性释义、音标和词形变化；保留顺序，合并重复词并显示次数。点击单词定位原文，支持复制全部词义。
- 优先使用已下载且启用的 ECDICT，未收录的词由启用的有道在线补查；离线结果先显示，失败按词提示。输入或切换视图会取消旧请求，已完成结果可直接切回查看。
- 查词支持最多 60 个不同英文词，保留 10,000 字符上限；支持小窗滚动和深浅主题。

## 0.4.8 — 2026-11-11

- AI 翻译支持多个渠道、每个渠道多个模型；分别配置地址和密钥，支持启停与调整顺序。已有 AI 配置继续兼容，渠道密钥由 Windows 加密保存。
- 复用现有降级与自动权重设置；按实际渠道和模型记录失败、冷却与恢复。禁用的渠道或模型不发请求，测试结果包含每个启用模型。
- 所有翻译渠道支持启用、禁用；修改 AI 配置后清除过期测试结果，首页显示模型测试汇总。
- 新增独立翻译面板，关键词 jfy / 翻译面板或设置按钮打开；原文译文对照、自动检测语言、自动或手动翻译、Ctrl+Enter 提交及独立释义。
- 连续输入、输入法组词和关闭窗口会取消旧请求；词语释义优先使用启用的 ECDICT，句子可由 AI 解释。支持长文本滚动和 10,000 字符限制。
- 保留现有渠道标签、截图工具栏与长文本小窗，以及 WinShun 0.23.0 的普通及管理员共享 Qt。

## 0.4.7 — 2026-11-11

- 适配 WinShun 0.23.0 的管理员共享 Qt 契约：普通和管理员模式均复用宿主 Qt 6.12.x，保留管理员软件划词。
- 安装包移除重复的 Qt DLL、平台插件和 QML 运行库；字体与必要的非 Qt 依赖继续随包提供。ECDICT 保持一键下载。
- 启动器检查宿主运行库路径、版本和完整性；没有宿主环境或 Qt 不兼容时明确报错，不回退到旧运行库。
- 构建改用固定 SHA-256 的 Qt 6.12.0 开发包；保留长文本自适应、外部关闭、渠道队列及现有翻译交互。
- 最低 WinShun 版本提升到 0.23.0。旧宿主可继续使用 0.4.6。

## 0.4.6 — 2026-11-08

- 剪贴板、划词和鼠标 OCR 的词义小窗随原文、译文长度自动调整宽高；短文本恢复紧凑尺寸，截图的“完整译文”窗口使用同样规则。
- 最大宽高均为 720 逻辑像素，同时不超过当前屏幕可用宽高的 80%；超长内容在正文内滚动，标题和操作按钮保持可见。
- 展开时避开任务栏和屏幕边缘；跨屏后按所在屏幕重新限制尺寸。固定窗口只在扩展后越界时调整位置，缩回后保持当前位置，重试不再重新定位到鼠标处。
- 修复长文本滚动条出现在窗口左上角的问题，移到正文右侧；编辑长原文时保持输入光标可见。
- 保留失焦自动翻译、悬浮还原、固定跟随、点击外部关闭、实际渠道标记和 ECDICT 一键下载。

## 0.4.5 — 2026-11-08

0.4.1–0.4.3 仅用于构建验证；0.4.4 为预发布。本版修复 GitHub 附件带签名跳转地址的编码问题，ECDICT 词库独立提供，在设置中一键下载，插件安装包不再包含词库。

- 剪贴板、OCR、划词小窗默认点击外部关闭；只有图钉按钮切换固定状态。点击或拖动标题不再偷偷固定窗口，截图贴图的关闭设置也不再影响小窗。
- 精简剪贴板、鼠标 OCR 和划词小窗：删除底部翻译 / 还原按钮，原文末尾仅在有修改且悬浮时显示还原图标。编辑后失去焦点自动翻译，保留 Enter 提交和 Shift+Enter 换行；未修改、输入法组词和关闭窗口时不重复提交。
- 截图工具栏、完整译文、鼠标 OCR、剪贴板和划词小窗统一显示实际翻译渠道；自动切换后标注“已降级”并使用主题提醒色，悬浮查看恢复说明，主渠道成功后还原默认颜色。关键字翻译结果也带渠道名称。
- 优化闲置内存：全部窗口关闭且后台任务结束后，等待 20 秒销毁不用的词义窗 / QML 引擎、清理图片和界面缓存，再在后台将闲置驻留页交还 Windows。固定窗口、进行中的任务和睡眠刚恢复时暂停回收，保持热键可用。
- 设置新增首页，集中显示全部渠道的测试状态、耗时、权重和冷却时间。“测试全部渠道”移到首页，支持逐个重测和直接进入渠道配置。
- 降级队列支持手动拖动 / 上下调整和自动权重两种模式。主渠道优先，备用渠道按保存的顺序或近期成功率、响应速度依次尝试；配置、权重与失败记忆重启后保留。
- 测试结果也用于更新权重和冷却状态；未配置、词典未收录、主动取消不降低权重。保留原有恢复间隔，编辑配置后的旧测试结果不会覆盖新状态。
- 截图工具栏高 32px，缩小控件和间距；原文语言、交换、目标语言、对照、渠道名称、复制、重新截图、完整译文、配置、关闭全部直接显示，小选区也不隐藏功能。
- 修复小选区 `Hi` 等短英文识别为空：首次识别失败时补试英文识别器与放大留白；识别坐标还原到原图，保留原有鼠标短句识别流程。
- 新增搜狗网页翻译与词典、有道免密钥英汉查词及官方整句翻译。
- ECDICT 770,611 个离线词条单独随 GitHub Release 提供；设置页面一键下载，支持进度、取消、校验和失败重试。下载后离线使用并在升级时保留；未下载不影响其他渠道，也不记为服务故障。
- OpenAI、DeepSeek、豆包、混元统一为“AI 翻译”，配置服务 URL、密钥、模型名。自动迁移原选中 AI 渠道的配置，并保留旧配置备份。
- “测试全部渠道”覆盖新渠道；有道官方 API 校验应用 ID / 密钥组合，AI 校验模型名。更改模型、区域或应用 ID 后可立即重试。

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

# 截图翻译 / Screenshot Translate

WinShun 截图翻译插件，使用本机 Windows OCR 识别文字，再通过选定的服务翻译。译文按文字块原位置显示，保留截图中的图标和数字徽标；可切换上下对照。

插件 ID：`iuuuuuuuu.screenshot-translate`。这是独立社区项目，不是 WinShun 官方插件。

## 安装

1. 从 [Releases](https://github.com/iuuuuuuuu/winshun-screenshot-translate/releases) 下载 `<id>-<version>.wsplugin`，可用同一 Release 的 `.sha256` 文件核对 SHA-256。
2. WinShun 设置 → 插件 → 从压缩包安装，选择该文件。
3. 审阅权限并启用插件。基础设置选择翻译渠道、目标语言和共用代理；「设置」窗口只编辑每个渠道自己的密钥、地址、模型和区域。

需要 Windows 10 / 11、Python 3.10+（包含 tkinter）、Windows PowerShell 5.1、WinShun 0.19.0 或更新版本，以及至少一种已安装的 Windows OCR 语言包。标准 Windows Python 安装通常包含 tkinter；插件不自动安装依赖。英文 OCR 可选组件可改善英文识别；插件不会自动安装系统组件。

**社区版 0.2.1（低内存架构从 0.2.0 开始）：** 常驻入口为 `broker.py`，不加载 tkinter、OCR 或 .NET。用户发起操作后才启动本仓库的 Python 窗口脚本；Windows OCR 使用系统 PowerShell 执行随包的 `ocr.ps1`，识别结束就退出。译图和设置都关闭后，窗口进程也退出，整个界面运行时随进程释放。不会下载、编译或运行额外二进制；社区安装包仅包含可审阅脚本与文档，使用 [官方社区检查和打包工作流](https://github.com/LingCore/WinShun-plugins/blob/main/CONTRIBUTING.md)。收录需维护者审核，不等于已经收录。旧 `0.1.10` 原生 Release 保持不变。

公开版 `0.1.10` 升级为 `0.2.0` 保留同一个数据目录、DPAPI 加密标识和独立渠道配置；不必重新填写公开版密钥。新增 `processes` 权限需要用户重新同意。旧原生源码保存在仓库 `.native/`，不进入新包，不参与运行。

早期本地开发版使用过 `winshun.screenshot-translate`。公开版改用作者前缀，数据目录和密钥加密标识独立；不会自动读取或改写旧插件数据。安装公开版后请重新填写配置，并停用旧版以避免关键字和热键冲突。不要把旧版截图、配置或日志提交到本仓库。

### 确认加载的版本

安装包发布不等于本机已经更新。WinShun 已安装卡片应显示 `iuuuuuuuu.screenshot-translate`、版本 `0.2.1`。任务管理器若仍显示 `ScreenshotTranslate-0.1.10.exe`，运行的是旧原生版，不含新内存架构。

新版常驻的是执行 `broker.py` 的 Python 进程。截图期间按需出现 `main.py --ui-worker` 和系统 PowerShell；识别结束后 OCR 进程退出，译图与设置都关闭后 GUI 进程退出。窗口还开着时，100 MB 以上的工作进程并不等于空闲常驻占用。检查进程完整命令行，避免把别的 Python 应用算成此插件。

## 使用

| 入口 | 操作 |
|---|---|
| `jst` / `截图翻译` | Enter 框选；Ctrl+Enter 全屏；Shift+Enter 当前窗口 |
| `jft` / `剪贴板翻译` | 留空读取剪贴板，也可直接输入 `jft hello world` |
| `jsel` / `划词翻译` | 按已配置的划词热键读取当前窗口选中文字；默认关闭 |
| `Alt+Shift+T` | 直接框选截图 |
| `Alt+Shift+C` | 直接翻译剪贴板 |

截图完成后不唤起搜索框。识别和翻译的 loading、错误直接显示在截图区域，操作条在截图下方。Esc 或双击关闭，拖动移动，「对照」切换上下对照，语言选择可重新翻译，右键复制原文 / 译文。长文可滚动阅读；密集多块排版放不下的译文局部省略，复制仍保留完整文字。

截图、OCR 和翻译进行期间，重复热键不排队，旧触发不在结束后补执行；结束后的新按键立即响应。划词只使用 UI Automation，不模拟 Ctrl+C，也不安装鼠标 / 键盘钩子；部分应用不支持取词。

## 翻译渠道

只有一项「翻译渠道」，直接选择 MyMemory、Google、Edge、Zvo（translate.js）、微软、DeepL、OpenAI、DeepSeek、豆包或混元，不再分出「模型渠道」。配置窗口的横向标签在窄窗口自动换行，浏览标签和保存配置不改变启用渠道。

基础设置由 WinShun 绘制。当前宿主的 `choice` 控件仅在整组选项能放进标题旁边时平铺，放不下就自动变成菜单；插件清单没有强制平铺或换行的协议字段。合并渠道不代表宿主窄面板已支持十项平铺，这一部分需要 WinShun 的控件支持，不能通过配置窗口替代基础设置。

从两级渠道的公开版升级后，若此前使用「模型服务」，请在基础设置重新选择一次具体服务。各家保存的密钥、地址和模型不变；协议仍兼容包含旧 `aiProvider` 的输入，但该项不再显示。

- MyMemory 无需密钥。设置页显示本插件按本机日期统计的提交字符数，不是服务端余额，也不包含其他应用或共享 IP 的用量。匿名额度和服务限制以服务商为准。
- Google、Edge、Zvo 提供免密钥通道，可能限流或变更。Google 在部分网络需要代理；Edge / Zvo 已实现接入，但不保证在每个网络环境可用。
- DeepL、微软及模型服务需要自己的密钥；豆包还需要模型或接入点 ID。兼容模型地址可填 `/v1` 或完整 `/chat/completions`。
- 共用代理支持不含账号密码的 HTTP / HTTPS 地址，只影响插件网络请求，不修改 Windows 设置。留空依次检查 `OPENAPI_TRANSLATE_PROXY`、`HTTPS_PROXY`、`HTTP_PROXY`、`ALL_PROXY` 和系统代理。

## 权限与隐私

| 权限 | 用途 |
|---|---|
| `ui` | 框选、译图和设置窗口；宿主代注册热键；剪贴板结果可回到搜索框，截图不唤起搜索框 |
| `clipboard:read` | 用户请求剪贴板翻译时读取文字 |
| `clipboard:write` | 复制原文或译文 |
| `network` | 向所选翻译服务提交文字 |
| `files:write` | 在插件数据目录保存独立配置和 MyMemory 本地用量 |
| `processes` | 按需启动同仓库 Python 窗口脚本及系统 PowerShell OCR / UI Automation 脚本；结束后退出 |

不要求管理员权限，不声明 `elevated` 或监听网络权限；不下载执行程序、不编译二进制、不提供遥测。子进程入口固定为本插件脚本及已安装的 Python / 系统 PowerShell，不通过配置加载代码。

截图和 OCR 在本机完成。**识别出的文字会发送至用户选择的服务，不要翻译不希望交给该服务的敏感内容。** 原始截图不上传。默认地址如下；自定义接口则请求用户配置的地址：

| 服务 | 默认网络地址 |
|---|---|
| MyMemory | `https://api.mymemory.translated.net/get` |
| Google | `https://translate.googleapis.com/translate_a/single`；失败时可尝试 `https://translate.google.com/translate_a/single` |
| Edge | 临时授权 `https://edge.microsoft.com/translate/auth`；翻译 `https://api-edge.cognitive.microsofttranslator.com/translate` |
| Zvo | `https://api.translate.zvo.cn/translate.json` |
| 微软 | `https://api.cognitive.microsofttranslator.com/translate` |
| DeepL | `https://api.deepl.com/v2/translate` 或 `https://api-free.deepl.com/v2/translate` |
| OpenAI | `https://api.openai.com/v1/chat/completions` |
| DeepSeek | `https://api.deepseek.com/chat/completions` |
| 豆包 | `https://ark.cn-beijing.volces.com/api/v3/chat/completions` |
| 混元 | `https://api.hunyuan.cloud.tencent.com/v1/chat/completions` |

数据存放在宿主提供的 `WS_DATA_DIR`，通常是 `%LOCALAPPDATA%\WinShun\plugins\iuuuuuuuu.screenshot-translate.data`：

- `services.json`：独立渠道配置，API 密钥使用 Windows DPAPI 当前用户加密；地址、模型、区域不是秘密存储。
- `mymemory-usage.json`：本机日期的提交量估算。
- 截图只在本次工作进程内存中处理，社区版不自动保存截图或 OCR 原文。旧版 `shots/` 保持不变，不自动读取或删除；卸载时由宿主询问是否删除数据。
- stderr 诊断由宿主写入 `plugins.log`，记录版本、类型、尺寸、块数和排队时长，不记录识别文本或密钥。服务返回的错误信息仍可能在用户界面显示。

## 限制

Windows OCR 对极小文字、低对比度、图标和混合语言可能误识别，包括 `I/l` 混淆。多文字块保留相对坐标，不保证字体、字重和背景像素完全一致。渐变、图片背景、紧密排版、硬件加速和受保护窗口可能产生色差、残影或抓取失败。

HTTP 请求预算为八秒，OCR 工作进程限时二十秒。译图关闭时取消当前操作；进行中的 HTTP 最迟在请求预算结束后退出。常驻进程不保留位图或窗口运行库；所有窗口关闭后，工作进程退出。截图和设置打开时的瞬时总内存仍可能超过 100 MB，不能承诺全程 20 MB。截图最多 1600 万像素，大于 Windows OCR 尺寸限制时仅对识别输入缩小并把坐标映射回原图。

2026-11-06 本机离屏压力测试：连续创建并关闭 12 个合成译图（含 2560×1440），实际调用 4 次 Windows OCR；窗口工作进程退出后，常驻进程工作集 26.8 MB，私有内存 18.1 MB，60 秒后工作集仍为 26.8 MB。该数字不是其他机器或实际截图峰值的保证，也不是整夜测试。

启动工作进程需要冷启动时间。社区版是 Python / tkinter 实现，窗口外观不与官方 Qt 窗口逐像素相同；明暗主题、横向配置标签、底部操作条和置顶行为沿用原交互约定。

实际框选、置顶、跨屏拖动和热键手感需要人工验证。离屏测试不替代这些检查。

## 构建与测试

无编译步骤，无第三方 Python 包。Windows 后台测试：

```powershell
python -B -m unittest discover -s .tests -v
python -B .tests/broker_memory_probe.py 60
python path\to\WinShun-plugins\tools\check.py . --id iuuuuuuuu.screenshot-translate
python path\to\WinShun-plugins\tools\build.py . --out dist
```

测试仅使用合成图片、假凭据、本地 HTTP 和从未显示的窗口，不注册真实热键、不抓取桌面、不访问个人配置、不模拟输入。OCR 集成测试需要本机英文 OCR 组件；没有组件时不能把测试失败算作通过。`broker.py --input-test` 仅供测试，替换截图 / 取词动作；正常清单不会传入该参数。

发布前在 Windows 运行无桌面交互的协议回归，随后调用官方社区打包工作流。仅收录 Git 跟踪文件，点目录（`.native`、`.tests`、`.github`）不进包；EXE/DLL、字节码、密钥、个人截图和日志不进包。推送 `v<version>` 标签发布正式 Release，标签必须匹配清单；已发布附件不替换，修订时升版本。

## 许可

MIT，见 [LICENSE](LICENSE) 与 [NOTICE.md](NOTICE.md)。遵循 WinShun 的公开协议并参考其 MIT 授权的规则与设计；不重新分发 Qt、WinShun 程序或字体。

## English

An independent WinShun screenshot translator using local Windows OCR and per-service translation profiles. It preserves text-block positions, supports source comparison, closes with Esc or double-click, and drops busy or stale hotkey triggers rather than replaying them later.

Install the `.wsplugin` from this repository's Releases through WinShun's **Install from archive**, review permissions and enable it. Requires Windows, Python 3.10+ with tkinter, Windows PowerShell, WinShun 0.19.0+ and an installed OCR language. Version 0.2.0 uses only reviewable scripts and the official community packaging workflow; inclusion in the index still requires maintainer review.

Recognized text is sent only to the selected translation provider or custom endpoint; screenshots stay in the disposable worker's memory. The resident broker has no UI/OCR runtime; the GUI worker exits after its windows close, and the OCR worker exits before translation. Provider keys retain the public 0.1.10 current-user DPAPI format. Read the permissions, endpoint table and limitations above before use. This project is not affiliated with WinShun.

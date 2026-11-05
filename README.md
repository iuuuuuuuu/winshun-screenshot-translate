# 截图翻译 / Screenshot Translate

WinShun 截图翻译插件，使用本机 Windows OCR 识别文字，再通过选定的服务翻译。译文按文字块原位置显示，保留截图中的图标和数字徽标；可切换上下对照。

插件 ID：`iuuuuuuuu.screenshot-translate`。这是独立社区项目，不是 WinShun 官方插件。

## 安装

1. 从 [Releases](https://github.com/iuuuuuuuu/winshun-screenshot-translate/releases) 下载 `<id>-<version>.wsplugin`，可用同一 Release 的 `.sha256` 文件核对 SHA-256。
2. WinShun 设置 → 插件 → 从压缩包安装，选择该文件。
3. 审阅权限并启用插件。基础设置选择翻译渠道、目标语言和共用代理；「设置」窗口只编辑每个渠道自己的密钥、地址、模型和区域。

需要 Windows 10 / 11、.NET Framework 4.8、WinShun 0.19.0 或更新版本，以及至少一种已安装的 Windows OCR 语言包。英文语言的 OCR 可选组件可改善英文识别；插件不会自动安装系统组件。

**发布方式说明：** 本插件使用预编译 `.exe`，不加载常驻 PowerShell 或浏览器运行时。按照 [WinShun-plugins 投稿规则](https://github.com/LingCore/WinShun-plugins/blob/main/CONTRIBUTING.md#规矩)，社区插件库不分发原生程序，但允许作者自行发布供压缩包安装。因此本项目只在自己的 GitHub Release 分发，不提交插件库条目，也不使用官方原生白名单或社区脚本的打包工作流。

早期本地开发版使用过 `winshun.screenshot-translate`。公开版改用作者前缀，数据目录和密钥加密标识独立；不会自动读取或改写旧插件数据。安装公开版后请重新填写配置，并停用旧版以避免关键字和热键冲突。不要把旧版截图、配置或日志提交到本仓库。

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

第一行选择 MyMemory、Google、Edge、Zvo（translate.js）、微软、DeepL 或「模型服务」。选「模型服务」时，第二行选择 OpenAI、DeepSeek、豆包或混元。配置窗口的标签和保存不改变启用渠道。

- MyMemory 无需密钥。设置页显示本插件按本机日期统计的提交字符数，不是服务端余额，也不包含其他应用或共享 IP 的用量。匿名额度和服务限制以服务商为准。
- Google、Edge、Zvo 提供免密钥通道，可能限流或变更。Google 在部分网络需要代理；Edge / Zvo 已实现接入，但不保证在每个网络环境可用。
- DeepL、微软及模型服务需要自己的密钥；豆包还需要模型或接入点 ID。兼容模型地址可填 `/v1` 或完整 `/chat/completions`。
- 共用代理支持不含账号密码的 HTTP / HTTPS 地址，只影响插件网络请求，不修改 Windows 设置。留空依次检查 `OPENAPI_TRANSLATE_PROXY`、`HTTPS_PROXY`、`HTTP_PROXY`、`ALL_PROXY` 和系统代理。

## 权限与隐私

| 权限 | 用途 |
|---|---|
| `ui` | 框选、译图和设置窗口；请求宿主代注册热键及剪贴板结果搜索框 |
| `clipboard:read` | 用户请求剪贴板翻译时读取文字 |
| `clipboard:write` | 复制原文或译文 |
| `network` | 向所选翻译服务提交文字 |
| `files:write` | 在插件数据目录保存截图诊断、独立配置和 MyMemory 本地用量 |

不要求管理员权限，不声明 `elevated`、`processes` 或监听网络权限；正常运行不启动子进程、不下载执行程序、不提供遥测。

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
- `shots/`：保留最新 30 组截图 PNG 与诊断 JSON。截图可能包含敏感信息，可手动删除；卸载时由宿主询问是否删除数据。
- stderr 诊断由宿主写入 `plugins.log`，记录版本、类型、尺寸、块数和排队时长，不记录识别文本或密钥。服务返回的错误信息仍可能在用户界面显示。

## 限制

Windows OCR 对极小文字、低对比度、图标和混合语言可能误识别，包括 `I/l` 混淆。多文字块保留相对坐标，不保证字体、字重和背景像素完全一致。渐变、图片背景、紧密排版、硬件加速和受保护窗口可能产生色差、残影或抓取失败。

HTTP 请求预算为八秒。空闲时回收托管对象，关闭窗口释放位图；内存依赖截图尺寸、显示器数量和已加载系统 DLL，不能承诺全程 20 MB。后台空闲测量不能当作实际截图内存上限。

实际框选、置顶、跨屏拖动和热键手感需要人工验证。离屏测试不替代这些检查。

## 构建与测试

在 Windows 使用系统 .NET Framework 编译器和 WinRT 元数据，不需要下载 NuGet 包：

```powershell
.\build.ps1
.\ScreenshotTranslate.exe --self-test
.\tests\trigger-queue-test.ps1 -Plugin "$PWD\ScreenshotTranslate.exe"
.\tests\blocks-smoke.ps1 -Plugin "$PWD\ScreenshotTranslate.exe"
.\tests\shared-proxy-test.ps1 -Plugin "$PWD\ScreenshotTranslate.exe"
.\package.ps1
```

`--self-test` 需要可用的本机 OCR，会写合成测试图片，但不会显示窗口。协议测试使用独立进程、假凭据、本地 HTTP 和从未显示的窗口，不注册真实测试热键、不抓取桌面。`--input-test` 仅供测试，将截图 / 取词动作替换为诊断；正常清单不会传入该参数。

发布工作流在 Windows 编译并运行无桌面交互的协议回归，只按固定白名单打包源码、当前程序和文档。`.git`、工作流、测试产物、密钥、个人截图和日志不会进入安装包。推送 `v<version>` 标签发布正式 Release，标签必须匹配清单；已发布附件不替换，修订时升版本。

## 许可

MIT，见 [LICENSE](LICENSE) 与 [NOTICE.md](NOTICE.md)。遵循 WinShun 的公开协议并参考其 MIT 授权的规则与设计；不重新分发 Qt、WinShun 程序或字体。

## English

An independent WinShun screenshot translator using local Windows OCR and per-service translation profiles. It preserves text-block positions, supports source comparison, closes with Esc or double-click, and drops busy or stale hotkey triggers rather than replaying them later.

Install the `.wsplugin` from this repository's Releases through WinShun's **Install from archive**, review permissions and enable it. Requires Windows, .NET Framework 4.8, WinShun 0.19.0+ and an installed OCR language. This native community plugin is **not distributed through the WinShun plugin index** under its current submission rules.

Recognized text is sent only to the selected translation provider or custom endpoint; screenshots stay local. Local diagnostic screenshots are retained in the plugin data directory. Provider keys are encrypted with current-user Windows DPAPI. Read the permissions, endpoint table and limitations above before use. This project is not affiliated with WinShun.

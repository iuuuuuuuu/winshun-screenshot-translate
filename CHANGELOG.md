## 0.4.41

- 修复反向选词后，结果窗口按文本区域底部定位的问题。点击和悬停图标沿用松开鼠标的位置，快捷键沿用触发位置，读取完成和译文扩展不再改用文本尾部。
- 新选区不再继承上一次窗口拖动或重试留下的位置；屏幕边缘避让、固定窗口和 OCR 文字框定位保持原有规则。
- “悬停图标自动翻译”开关直接显示于设置首页顶部，无需展开图标设置；默认关闭、保存后生效，较窄窗口自动换行。
- 补充点击、悬停、快捷键、双屏边缘及折叠状态下开关可见的界面回归。

## 0.4.40

- 新增“悬停图标自动翻译”开关，位于设置首页的“图标设置”，默认关闭，保存后生效。
- 开启后，鼠标在小译图标上停留 350 毫秒自动翻译；点击仍然立即翻译。移出、图标隐藏、切换窗口或关闭设置会取消等待。
- 悬停与点击共用完整取词和上下文流程，触发前复核原窗口与当前选区；同一次图标展示只触发一次，避免悬停后点击重复请求。
- 增加设置持久化及实际 QML 鼠标悬停、取消、完整选区和去重回归。

## 0.4.39

- 修复 CodexHost / WebView2 等多进程软件中，正常划词被误报“检测到另一处复制”的问题：核实原窗口与复制子进程的关系，不再要求进程号完全相同。
- 快捷键、小译按钮和手动复制接续共用来源校验；仍拒绝无关进程，并检查进程创建时间，防止进程号复用造成误认。
- 保留原剪贴板格式还原、取消处理以及真正新复制优先的规则，增加实际子进程和无关进程回归测试。

## 0.4.38

- 新增可选的 AI 少量上下文，默认关闭；前后合计默认最多 600 字，可选 200 / 600 / 1000 字，保存后生效。
- 仅主动划词翻译尝试 AI 时读取，使用同一次请求，不增加整页摘要调用；长选区使用自身原文，非 AI、OCR、剪贴板和被动监测不额外读取。
- 附近文字通过 Windows UIA / Firefox IA2 或小文本框读取；独立进程限时、完整选区校验、Unicode 边界保护，失败照常翻译。
- 弹窗支持查看实际使用的附近文字；编辑原文、换选区、关闭或更改配置时清除，上下文不同的相同词语分别缓存，不保存附近文字到历史。
- 增加原生、AI 请求/缓存、界面取消与 Firefox 上下文回归，保留十万字完整取词检查。

## 0.4.37

- AI 渠道支持获取 `/models`、模型名称搜索、未添加筛选、多选添加，兼容根地址、版本路径和完整 chat/completions 地址；旧请求取消后不会填入其他渠道。
- 模型可独立启停、排序和配置失败冷却时间，显示当前状态、剩余时间及解除冷却入口；自定义冷却期间不提前自动探测。
- 翻译指令同时写入用户消息，兼容未转发系统指令的中转服务，避免把正常响应误判为翻译格式失败。
- 划词图标预先创建隐藏窗口，固定防抖从 160 毫秒降到 35 毫秒，减少不必要的语种细分；点击仍重新读取完整选区。
- 增加模型发现、取消和过期响应、搜索多选、冷却持久化和启停回归；普通界面测试保持只读，真实剪贴板测试继续由隔离发布环境验证。

## 0.4.36

- 划词热键和小译点击共用取词流程：原窗口复制命令、辅助功能选区、受控 Ctrl+Insert、手动复制接续，不再只给 Firefox 和 Flutter 各写一个入口。
- 本次复制结果优先于可能截断的接口文字；无法确认选区时不再使用旧图标快照，所有软件均可在 15 秒内手动复制接续。
- 共用剪贴板格式备份与还原；新复制优先。取消仍完成还原；快捷键受选区、焦点、输入桌面和按键状态检查，不主动切换窗口。
- 成功复制方式按进程和控件类型短期缓存，减少重复尝试；新增通用策略、原生文本框、长文及 Firefox 回归。
- 修复较慢机器上 Firefox 复制十万字长文被过短消息时限中断的问题；发布检查覆盖真实复制、剪贴板格式还原、取消和新复制优先。

## 0.4.35

- 修复 Firefox 跨网页组件拖选时只翻译用户名或半句话：快捷键和小译点击使用浏览器原生复制选区，完整读取实际选中文字。
- 原剪贴板按格式备份并还原；不覆盖期间的新复制，取消也完成还原。无法安全备份时提示手动复制，不退回已知可能截断的图标缓存。
- 不发送 Ctrl+C、不切换窗口焦点；被动图标检测和固定跟随保持只读。
- 新增真实鼠标跨 Shadow DOM 的正反向回归，证明旧读取失败、新路径保留整行，并验证小译、快捷键和剪贴板还原。

## 0.4.34

- Firefox 划词优先直接读取 IAccessible2 选区，按链接、组件各自的字符偏移拼接选中文字；快捷键和小译图标共用此路径。
- 过滤选中头像的对象占位符，保留真实文字、部分选区、段落换行和长文末尾；读取失败仍保留通用 UIA 回退。
- 从焦点直接定位网页，减少逐个检查浏览器工具栏造成的额外耗时。
- 增加 Firefox 原生与兼容模式选区回归。自动测试覆盖合成页面；用户原页面的偶发截断仍需实测确认。

## 0.4.33

- 点击划词“小译”图标时重新确认最终选区，避免浏览器尚未更新完选区时沿用早期半句；源程序清掉选区时仍保留已捕获的文字。
- 设置、独立翻译面板、历史与收藏统一自定义标题栏、圆角、拖动与边缘缩放，补齐最小化、最大化/还原和关闭按钮的悬停、按下反馈。
- 设置窗口取消置顶，切换到其他软件时可正常被遮住，重新打开也不强制置顶。
- 三个大窗口分别记忆调整后的尺寸和最大化状态，自动保存、重启恢复，适配当前屏幕可用区域；小翻译窗口继续按内容自动适应。
- 划词小窗底部的历史、原文朗读、译文朗读使用与辅助文字一致的字号和图标大小。
- 修正浅色主题下选中译文出现浅底白字的问题，统一可选择文本的选区颜色，并验证深色主题；修正小尺寸翻译面板的内容与底栏重叠。

## 0.4.32

- 接入 Win顺官方截图翻译协议（宿主 0.32.0+）：独立命令保留原行数、空行和长文，10 秒内给出结果或明确失败，支持取消和配置指引。
- AI 译文可流式显示，支持按渠道关闭流式；适配 DeepSeek、通义的关闭思考参数，参数不兼容时只回退一次并记住兼容方式；429 沿用冷却，不立即重复请求。断流或截断不作为完整译文缓存。
- 增加本地翻译历史与收藏：主动复制或收藏时记录，可关闭记录、搜索、删除及清理普通历史；最多 500 条普通记录，收藏单独保留。支持导出 CSV 和可导入 Anki 的 TSV。
- 翻译面板可选 PDF 接行，保留段落、列表、代码、表格与可见连字符；支持 en: / ja: 等单次目标语言前缀，可保存六组常用语言对。
- 独立命名模式支持拆解标识符，并生成 camelCase、PascalCase、snake_case、CONSTANT_CASE 和 kebab-case；中文含义先翻译成英文。
- 原文和译文支持 Windows SAPI 朗读、停止和缺少语音提示，后台工作不阻塞界面；截图工具栏也可朗读译文。

## 0.4.31

- 关闭按钮悬停显示红底白色图标，按下时颜色加深；其他工具按钮也补齐清晰的悬停和按下反馈。
- 翻译小窗点击关闭时立即取消请求并短暂淡出，遵循 Windows 动画开关；淡出中再次按快捷键可正常打开，不会被旧动画关闭。

## 0.4.30

- 截图翻译支持点击工具栏渠道名称，为本次截图手动选择渠道或 AI 模型，也可恢复自动；切换显示加载状态，失败保留上一份译文和截图，不修改全局默认。
- 为 Fluxdo 等未提供 Windows 选区接口的 Flutter 应用增加复制兼容：划词读取为空时提示在原窗口复制，15 秒内的新复制内容自动继续翻译；不模拟按键、不改写剪贴板，不使用旧内容或其他应用复制的内容。
- 设置页跟随 Win顺的软件 / D3D11 渲染模式，统一字体绘制并消除文字彩边；避免窗口呈现等待。
- 退出时先清理 Qt 窗口与渲染线程，避免显卡模式在进程退出阶段卡住。
- 预编译设置页、按需加载 AI 配置、按字段更新界面；渠道状态刷新复用原控件。关闭后短暂保留窗口以加快重开，取消仍丢弃未保存内容，空闲时释放。
- 截图 OCR 段落在发送翻译前合并排版换行，保留段落与列表边界，以及 1.25-bit、30B-A3B 等连字符；原文和截图坐标保持可追溯。
- 减少同字体短字形导致的段落误拆，并结合字形与上下文修复英文行尾短横线误认成汉字的情况。
- 增加 150% 缩放、设置页刷新与重开、取消草稿、换行请求内容及段落边界回归。

## 0.4.29

- 划词悬浮图标默认只在选中文字的主要语种与默认语种不同时显示，纯数字和符号默认不显示。
- 设置首页增加“显示条件”，可切换所有语种；默认语种可单独指定，或跟随目标语言（目标为自动时跟随界面语言）。
- 语言判断在本机后台完成，不请求翻译服务；长选区只抽样判断，实际翻译仍保留全文。保存立即生效并记住配置，快捷键不受过滤限制。

## 0.4.28

- 修复截图分块后远处搜索框干扰本地文字拼接，减少单词被拆开、时间词断裂和多列顺序错乱；保持倾斜文字的原生行顺序。
- 结合原图像素分离头像碎片、图标与提交数，补充彩色按钮图标识别；保留真实彩色标点、编号和数字角标。
- 对小字进行紧凑批量放大复核；字母修正需要另一尺寸复核一致，数字保持一致，版本号的小点与常见 v1 误读单独处理。
- 短译文优先利用相邻空白并适当缩小保持一行，擦除范围收紧到实际文字，减少不必要换行和按钮边缘被抹掉。
- 增加分栏、数字标点、头像、短标签边界回归；保留截图拖选和点击复制。

## 0.4.27

- 截图 OCR 结合图标列/工具栏布局与原图墨色过滤图形误识别，覆盖图标被读成汉字、0、O 或拆分符号的情况；保留真实文字前缀和数字角标。
- 译文字号根据原文字形高度与宽度估算，移除固定 14–28 像素限制，按原文字形顶部对齐；未发生变化的文字保留原图排版。
- 使用原图复测图标与菜单边界，并增加深浅色、拆分图标、数字角标和大小字号回归。

## 0.4.26

- 原文不超过三行时隐藏展开/收起箭头，点击原文仍可直接编辑；超过三行时继续显示固定在右侧的按钮。
- 自动降级按正常行为显示，划词弹窗、截图工具栏、完整译文及独立面板的渠道文字统一使用普通颜色；真正的请求失败仍保留错误提示。

## 0.4.25

- 修复按住鼠标跨页滚动选字后不显示翻译图标，去掉选区读取的 10 万字符截断。
- 优先读取网页文档选区，只查询末端字符的位置，减少长选区几何查询；从翻译弹窗再次按划词热键时保留原目标窗口，读取失败明确说明快捷键已响应。
- 切换渠道的加载进度、成功结果与失败原因显示在固定底栏，等待本次成功或失败后才允许再次切换；失败保留上一份译文。
- 原文与译文按实际翻译的句段双向高亮，选中时定位对应部分；编辑中保留未改动句段的对应关系，改动部分重新翻译后恢复。
- 截图译文直接使用排版得到的文字位置，支持拖选复制、点击文字行复制，空白处仍可拖动截图；完整译文窗增加复制选区或全文的按钮。
- 增加跨页长选区、浏览器焦点恢复、加载/失败、双向高亮和截图复制回归。失焦保留展开编辑状态，两侧不强制同步滚动。

## 0.4.24

- 翻译弹窗原文默认最多显示三行，超出部分省略，给译文留出更多空间。
- 点击原文展开并直接编辑；收起按钮固定在右侧，原文与译文独立滚动时仍然可见，收起时提交修改。
- 点击底部渠道名称可选择服务或具体 AI 模型，只影响当前弹窗；可切回自动策略，切换时保留上一份译文，并继续遵循限流和冷却。
- 弹窗及渠道菜单采用原生圆角，增加打开、展开和译文出现的轻量动画，遵循 Windows 动画设置。
- 增加弹窗交互与指定 AI 模型回归，保留多屏缩放无黑边和拖动渲染检查。

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

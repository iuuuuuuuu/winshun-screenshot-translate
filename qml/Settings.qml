pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import WinShun

Window {
    id: window
    visible: false
    width: 940; height: 700
    minimumWidth: 720; minimumHeight: 520
    title: backend.state.chinese ? "截图翻译设置 · Win顺" : "Screenshot Translate · WinShun"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: Theme.page
    property real cornerRadius: 8
    readonly property string windowSizeKey: "settings"
    property bool chinese: backend.state.chinese
    property string provider: backend.state.provider
    property bool home: backend.state.home || false
    property bool automatic: backend.state.routingMode === "auto"
    property bool iconOptionsOpen: false
    readonly property var iconLanguageCodes: ["auto", "zh", "en", "ja", "ko", "fr", "de", "es", "ru"]
    readonly property var iconLanguageLabels: chinese
        ? ["跟随设置", "中文", "英文", "日文", "韩文", "法文", "德文", "西班牙文", "俄文"]
        : ["Follow settings", "Chinese", "English", "Japanese", "Korean", "French", "German", "Spanish", "Russian"]
    readonly property bool dragging: queueRows.dragIndex >= 0
    property var queue: backend.state.queue || []
    function t(zh, en) { return chinese ? zh : en }
    function move(from, to) {
        to = Math.max(1, Math.min(queue.length - 1, to))
        if (from === to) return
        if (automatic) {
            const moved = queue[from]
            if (moved.preferred) return
            const neighbor = queue[to]
            // Keep the measured health/speed score, and persist only the small
            // user adjustment needed to place the row above/below its neighbor.
            const score = neighbor.effectiveWeight !== undefined ? neighbor.effectiveWeight : neighbor.weight
            const adjustment = Math.max(-10000, Math.min(10000,
                score + (to < from ? 1 : -1) - moved.weight))
            backend.action("adjustProviderWeight", {provider: moved.provider, adjustment: adjustment})
            return
        }
        backend.action("moveProvider", {provider: queue[from].provider,
            before: to < from ? queue[to].provider : to + 1 < queue.length ? queue[to + 1].provider : ""})
    }
    function statusText(row) {
        if (row.enabled === false) return t("已禁用", "Disabled")
        if (row.status === "running") return t("测试中", "Testing")
        if (row.status === "waiting") return t("等待中", "Queued")
        if (!row.configured) return row.provider === "ecdict" ? t("未下载", "Not downloaded") : t("待配置", "Configure")
        if (row.cooling && row.recoveryProbe) return row.probeSeconds > 0
            ? t("恢复检查 ", "Recovery in ") + Math.ceil(row.probeSeconds / 60) + t(" 分钟", " min")
            : t("可尝试恢复", "Recovery ready")
        if (row.cooling) return t("冷却 ", "Retry in ") + Math.ceil(row.retrySeconds / 60) + t(" 分钟", " min")
        if (row.status === "success") return t("可用", "Available")
        if (row.status === "failed") return t("待重试", "Retry ready")
        return t("未测试", "Untested")
    }
    Timer {
        interval: 5000; repeat: true; running: window.visible && window.home && !window.dragging && !backend.state.testing
        onTriggered: backend.action("refreshQueue", null)
    }
    onClosing: event => { event.accepted = false; backend.action("close", null) }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: backend.action("close", null)
        WindowFrame { id: chrome; hostWindow: window; buttonPrefix: "settings"; chinese: window.chinese }
        Column {
            id: heading
            x: 24; y: chrome.titleHeight + 10
            width: parent.width - 48; spacing: 6
            Text { text: window.t("截图翻译", "Screenshot Translate"); color: Theme.text; font.pixelSize: 24; font.weight: Font.Bold }
            Text {
                width: parent.width
                text: window.t("测试渠道、调整降级队列，或打开渠道标签配置服务。",
                               "Test services, arrange fallback, or open a service tab to configure it.")
                color: Theme.subtext; font.pixelSize: 12; wrapMode: Text.Wrap
            }
            Row {
                spacing: 12
                FlatButton { objectName: "open-panel"; text: window.t("打开独立翻译面板", "Open translation panel"); textPixelSize: 13; onClicked: backend.action("openPanel",null) }
                FlatButton { objectName: "open-history"; text: window.t("历史与收藏", "History and favorites"); textPixelSize: 13; onClicked: backend.action("openHistory",null) }
                FlatButton {
                    objectName: "selection-icon-toggle"
                    visible: window.home && backend.state.selectionIconEnabled !== undefined
                    text: window.t("划词翻译图标：", "Selection icon: ")
                        + (backend.state.selectionIconEnabled ? window.t("开", "On") : window.t("关", "Off"))
                    textPixelSize: 13
                    highlighted: !!backend.state.selectionIconEnabled
                    onClicked: backend.action("selectionIconEnabled", !backend.state.selectionIconEnabled)
                }
                FlatButton {
                    objectName: "selection-icon-options"
                    visible: window.home && backend.state.selectionIconForeignOnly !== undefined
                    enabled: !!backend.state.selectionIconEnabled
                    text: window.t("显示条件：", "Show for: ") + (backend.state.selectionIconForeignOnly
                        ? window.t("非默认语种", "Other languages") : window.t("所有语种", "All languages"))
                    textPixelSize: 13; highlighted: window.iconOptionsOpen
                    onClicked: { window.iconOptionsOpen = !window.iconOptionsOpen; scroll.contentY = 0 }
                }
            }
            Item { width: 1; height: 4 }
            ChoiceTabs {
                id: tabs
                objectName: "service-tabs"
                width: parent.width; labels: [window.t("首页", "Home")].concat(backend.state.names)
                current: window.home ? 0 : backend.state.current + 1
                onActivated: index => { scroll.contentY = 0; backend.action(index === 0 ? "home" : "select", index - 1) }
            }
        }
        Flickable {
            id: scroll
            objectName: "settings-scroll"
            anchors.top: heading.bottom; anchors.topMargin: 18
            anchors.left: parent.left; anchors.leftMargin: 24
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.bottom: footer.top; anchors.bottomMargin: 16
            clip: true
            interactive: !window.dragging
            contentWidth: width; contentHeight: sheet.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Column {
                id: sheet
                width: scroll.width; spacing: 14
              Column {
                objectName: "settings-home"
                visible: window.home
                width: scroll.width; spacing: 14
                Rectangle {
                    visible: window.iconOptionsOpen && !!backend.state.selectionIconEnabled
                    width: parent.width; height: iconOptions.implicitHeight + 32
                    radius: Theme.cardRadius; color: Theme.card; border.color: Theme.cardBorder
                    Column {
                        id: iconOptions
                        x: 16; y: 16; width: parent.width - 32; spacing: 10
                        Text { text: window.t("划词图标显示条件", "Selection icon visibility"); color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                        ChoiceTabs {
                            objectName: "selection-icon-modes"
                            width: parent.width
                            labels: [window.t("仅非默认语种（默认）", "Other languages (default)"), window.t("所有语种", "All languages")]
                            current: backend.state.selectionIconForeignOnly ? 0 : 1
                            onActivated: index => backend.action("selectionIconForeignOnly", index === 0)
                        }
                        Text { text: window.t("默认语种", "Default language"); color: Theme.text; font.pixelSize: 13 }
                        ChoiceTabs {
                            objectName: "selection-icon-languages"
                            width: parent.width; labels: window.iconLanguageLabels
                            current: Math.max(0, window.iconLanguageCodes.indexOf(backend.state.selectionIconDefaultLanguage))
                            enabled: !!backend.state.selectionIconForeignOnly; opacity: enabled ? 1 : 0.5
                            onActivated: index => backend.action("selectionIconDefaultLanguage", window.iconLanguageCodes[index])
                        }
                        Text {
                            width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: Theme.subtext
                            text: window.t("当前默认语种：", "Current default: ")
                                + window.iconLanguageLabels[Math.max(1, window.iconLanguageCodes.indexOf(backend.state.selectionIconResolvedLanguage))]
                                + window.t("。跟随设置时采用目标语言；目标为自动时采用界面语言。按主要语种判断；仅非默认语种模式下，纯数字和符号不显示图标。保存后生效，快捷键不受影响。",
                                    ". Follow settings uses the target language, or the interface language when target is automatic. Uses the main language. Other-languages mode also hides numbers and symbols. Save to apply. Hotkeys are unaffected.")
                        }
                    }
                }
                Rectangle {
                    width: parent.width; height: overview.implicitHeight + 32
                    radius: Theme.cardRadius; color: Theme.card; border.color: Theme.cardBorder
                    Column {
                        id: overview
                        x: 16; y: 16; width: parent.width - 32; spacing: 12
                        Item {
                            width: parent.width; height: 38
                            Column {
                                spacing: 4; width: parent.width - testAll.width - 16
                                Text { text: window.t("渠道概览", "Service overview"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: window.t("可尝试 ", "Ready ") + backend.state.readyCount + "   ·   " + window.t("冷却 ", "Cooling ") + backend.state.coolingCount
                                        + "   ·   " + window.t("待配置 ", "Unconfigured ") + backend.state.unconfiguredCount
                                    color: Theme.subtext; font.pixelSize: 12
                                }
                            }
                            FlatButton {
                                id: testAll
                                objectName: "test-all"
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                glyph: "\uE72C"; text: backend.state.testing ? window.t("正在测试…", "Testing…") : window.t("测试全部渠道", "Test all services")
                                highlighted: true; textPixelSize: 14; enabled: !backend.state.testing
                                onClicked: backend.action("testAll", null)
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.divider }
                        Row {
                            width: parent.width; spacing: 16
                            Text { anchors.verticalCenter: parent.verticalCenter; text: window.t("降级顺序", "Fallback order"); color: Theme.text; font.pixelSize: 14 }
                            ChoiceTabs {
                                objectName: "routing-modes"
                                labels: [window.t("手动排序", "Manual order"), window.t("自动权重", "Automatic weights")]
                                current: window.automatic ? 1 : 0
                                onActivated: index => backend.action("routingMode", index === 1 ? "auto" : "manual")
                            }
                        }
                        Text {
                            width: parent.width; wrapMode: Text.Wrap
                            text: !backend.state.fallbackEnabled
                                ? window.t("自动降级已关闭。可先编排队列，再到 Win顺插件面板开启“自动降级”。", "Fallback is off. Arrange your queue, then enable automatic fallback in WinShun.")
                                : (window.automatic
                                    ? window.t("主渠道固定优先；其余渠道按近期成功率和速度排序。拖动备选渠道可上调或下调它的权重。", "The preferred service stays first; backups are ranked by recent success and speed. Drag a backup up or down to boost or lower its weight.")
                                    : window.t("主渠道由 Win顺插件面板中的翻译渠道设置并固定置顶；失败后从下方备选渠道依序尝试，可拖动手柄或使用按钮排序。", "The preferred service is set in WinShun's plugin panel and stays first; drag or use the arrows to order backups."))
                                    + window.t(" 基础冷却 ", " Cooldown: ") + backend.state.recoveryMinutes + window.t(" 分钟，连续故障加倍，最长 7 天；保存后生效。", " min. Save to apply.")
                            color: Theme.subtext; font.pixelSize: 12
                        }
                    }
                }
                Column {
                    id: queueRows
                    objectName: "fallback-queue"
                    width: parent.width; spacing: 6
                    readonly property int rowHeight: 66
                    readonly property int rowStep: rowHeight + spacing
                    property int dragIndex: -1
                    property int dropIndex: -1
                    property real dragOffset: 0
                    property real pointerY: 0
                    property real startY: 0
                    property real startScroll: 0
                    property bool pastThreshold: false
                    function updateDrag() {
                        const delta = pointerY - startY + scroll.contentY - startScroll
                        if (!pastThreshold && Math.abs(delta) < Qt.styleHints.startDragDistance) return
                        pastThreshold = true
                        dragOffset = Math.max((1 - dragIndex) * rowStep,
                            Math.min((window.queue.length - dragIndex - 1) * rowStep, delta))
                        dropIndex = dragIndex + Math.round(dragOffset / rowStep)
                    }
                    function finishDrag(commit) {
                        const from = dragIndex
                        const to = dropIndex
                        dragIndex = -1
                        dropIndex = -1
                        dragOffset = 0
                        pastThreshold = false
                        if (commit && from >= 0 && to >= 0) window.move(from, to)
                    }
                    Timer {
                        interval: 24; repeat: true; running: window.dragging && queueRows.pastThreshold
                        onTriggered: {
                            const step = queueRows.pointerY < 30 ? -9 : queueRows.pointerY > scroll.height - 30 ? 9 : 0
                            if (!step) return
                            const maxScroll = Math.max(0, scroll.contentHeight - scroll.height)
                            scroll.contentY = Math.max(0, Math.min(maxScroll, scroll.contentY + step))
                            queueRows.updateDrag()
                        }
                    }
                    Repeater {
                        // Status/cooldown updates keep the existing controls.
                        // The provider at each position may change after a drag.
                        model: window.queue.length
                        Item {
                            id: queueRow
                            readonly property var modelData: window.queue[index]
                            required property int index
                            objectName: "queue-row-" + modelData.provider
                            width: queueRows.width; height: queueRows.rowHeight
                            readonly property bool held: queueRows.dragIndex === index
                            readonly property bool canDrag: !modelData.preferred && !backend.state.testing
                            z: held ? 2 : 0
                            Rectangle {
                                id: rowSurface
                                width: parent.width; height: parent.height
                                y: {
                                    const from = queueRows.dragIndex
                                    const to = queueRows.dropIndex
                                    if (queueRow.held) return queueRows.dragOffset
                                    if (from < 0) return 0
                                    if (queueRow.index > from && queueRow.index <= to) return -queueRows.rowStep
                                    if (queueRow.index < from && queueRow.index >= to) return queueRows.rowStep
                                    return 0
                                }
                                color: queueRow.held ? Theme.chipActive : Theme.card
                                radius: Theme.cardRadius
                                border.color: queueRow.held ? Theme.accent : Theme.cardBorder
                                Item {
                                    x: 0; width: 40; height: parent.height
                                    // Keep the handle visible in both manual and automatic modes.
                                    visible: true
                                    Glyph {
                                        visible: queueRow.modelData.preferred
                                        anchors.centerIn: parent; glyph: "\uE718"
                                        size: 15; color: Theme.accent
                                    }
                                    Column {
                                        visible: !queueRow.modelData.preferred
                                        anchors.centerIn: parent; spacing: 3
                                        opacity: queueRow.canDrag ? 1 : 0.45
                                        Repeater {
                                            model: 3
                                            Row {
                                                required property int index
                                                spacing: 3
                                                Repeater {
                                                    model: 2
                                                    Rectangle {
                                                        width: 3; height: 3; radius: 1.5
                                                        color: Theme.subtext
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    MouseArea {
                                        objectName: "queue-drag-" + queueRow.modelData.provider
                                        anchors.fill: parent
                                        enabled: queueRow.canDrag
                                        preventStealing: true
                                        scrollGestureEnabled: false
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        function updatePointer(mouse) {
                                            // Mapping the mouse event's local position takes the moved
                                            // row origin into account and still yields the real pointer
                                            // position in Flickable coordinates.
                                            queueRows.pointerY = mapToItem(scroll, mouse.x, mouse.y).y
                                        }
                                        onPressed: mouse => {
                                            scroll.cancelFlick()
                                            queueRows.dragOffset = 0
                                            updatePointer(mouse)
                                            queueRows.startY = queueRows.pointerY
                                            queueRows.startScroll = scroll.contentY
                                            queueRows.dropIndex = queueRow.index
                                            queueRows.dragIndex = queueRow.index
                                        }
                                        onPositionChanged: mouse => {
                                            if (!pressed) return
                                            updatePointer(mouse)
                                            queueRows.updateDrag()
                                        }
                                        onReleased: mouse => {
                                            updatePointer(mouse)
                                            queueRows.updateDrag()
                                            queueRows.finishDrag(true)
                                        }
                                        onCanceled: queueRows.finishDrag(false)
                                    }
                                }
                                Column {
                                    id: rowDescription
                                    x: 40; anchors.verticalCenter: parent.verticalCenter; spacing: 5
                                    width: parent.width - rowActions.width - 192
                                    Row {
                                        spacing: 8
                                        Text { text: queueRow.modelData.name; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                                        Text {
                                            text: queueRow.modelData.preferred ? window.t("主渠道", "Preferred") : window.t("备选 ", "Backup ") + queueRow.index
                                            color: queueRow.modelData.preferred ? Theme.accent : Theme.faint; font.pixelSize: 12
                                        }
                                    }
                                    Text {
                                        width: parent.width; elide: Text.ElideRight; textFormat: Text.PlainText
                                        text: ((queueRow.modelData.recoveryProbe ? window.t("无可用备用时自动恢复 · ", "Auto recovery if backups fail · ") : "") + (queueRow.modelData.detail || queueRow.modelData.problem
                                            || (queueRow.modelData.provider === "ecdict" ? window.t("离线英汉词典 · 未收录会继续尝试", "Offline dictionary · misses fall through")
                                                : queueRow.modelData.samples ? window.t("已记录最近表现", "Recent performance recorded") : window.t("测试后显示连接结果和耗时", "Test to see connectivity and latency")))).replace(/\s+/g, " ")
                                        color: Theme.subtext; font.pixelSize: 12
                                    }
                                }
                                Column {
                                    anchors.right: rowActions.left; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                                    width: 116; spacing: 5
                                    Text {
                                        anchors.right: parent.right; text: window.statusText(queueRow.modelData); font.pixelSize: 12
                                        color: queueRow.modelData.cooling ? Theme.danger : queueRow.modelData.status === "success" ? Theme.success : Theme.subtext
                                    }
                                    Text {
                                        anchors.right: parent.right; font.pixelSize: 11; color: Theme.faint
                                        text: (queueRow.modelData.elapsedMs !== undefined ? queueRow.modelData.elapsedMs + " ms · " : queueRow.modelData.latencyMs >= 0 ? queueRow.modelData.latencyMs + " ms · " : "")
                                            + window.t("权重 ", "Weight ") + (window.automatic && queueRow.modelData.effectiveWeight !== undefined
                                                ? queueRow.modelData.effectiveWeight : queueRow.modelData.weight)
                                            + (window.automatic && queueRow.modelData.weightAdjustment
                                                ? " (" + (queueRow.modelData.weightAdjustment > 0 ? "+" : "") + queueRow.modelData.weightAdjustment + ")" : "")
                                            + (!queueRow.modelData.samples ? window.t("（初始）", " (initial)") : "")
                                    }
                                }
                                Row {
                                    id: rowActions
                                    anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                    FlatButton {
                                        objectName: "queue-enabled-" + queueRow.modelData.provider
                                        text: queueRow.modelData.enabled === false ? window.t("禁用","Off") : window.t("启用","On")
                                        textPixelSize: 12; implicitHeight: 30; highlighted: queueRow.modelData.enabled !== false
                                        enabled: !backend.state.testing
                                        onClicked: backend.action("enabled",{provider:queueRow.modelData.provider,enabled:queueRow.modelData.enabled === false})
                                    }
                                    ToolButton {
                                        objectName: "queue-up-" + queueRow.modelData.provider
                                        visible: !window.automatic; glyph: "\uE70E"; tip: window.t("上移", "Move up")
                                        enabled: queueRow.index > 1 && !backend.state.testing
                                        onClicked: window.move(queueRow.index, queueRow.index - 1)
                                    }
                                    ToolButton {
                                        objectName: "queue-down-" + queueRow.modelData.provider
                                        visible: !window.automatic; glyph: "\uE70D"; tip: window.t("下移", "Move down")
                                        enabled: queueRow.index > 0 && queueRow.index < window.queue.length - 1 && !backend.state.testing
                                        onClicked: window.move(queueRow.index, queueRow.index + 1)
                                    }
                                    FlatButton {
                                        text: window.t("测试", "Test"); textPixelSize: 12; implicitHeight: 30; flat: true
                                        enabled: !backend.state.testing
                                        onClicked: backend.action("testService", queueRow.modelData.provider)
                                    }
                                    ToolButton {
                                        glyph: "\uE713"; tip: window.t("配置渠道", "Configure service")
                                        onClicked: { scroll.contentY = 0; backend.action("selectProvider", queueRow.modelData.provider) }
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    width: parent.width; wrapMode: Text.Wrap; font.pixelSize: 12; color: Theme.faint
                    text: window.t("测试使用 hello，不触发降级。未配置的渠道会跳过；词典未收录和主动取消不降低权重。主渠道、代理与恢复间隔在 Win顺插件面板中设置。",
                                   "Tests use hello without fallback. Unconfigured services are skipped; dictionary misses and cancellation do not reduce weights. Set the preferred service, proxy and recovery interval in WinShun.")
                }
              }
              Loader {
                active: !window.home && window.provider === "ai"
                visible: active; width: scroll.width
                sourceComponent: AiChannels { width: scroll.width }
              }
              Rectangle {
                id: card
                visible: !window.home && window.provider !== "ai"
                width: scroll.width; height: rows.implicitHeight
                radius: Theme.cardRadius; color: Theme.card
                border.width: 1; border.color: Theme.cardBorder
                Column {
                    id: rows
                    width: parent.width
                    ServiceRow {
                        visible: ["mymemory", "google", "edge", "sogou", "ecdict"].indexOf(window.provider) >= 0
                        title: window.t("API 密钥", "API key")
                        Text { text: window.t("无需密钥", "Not required"); color: Theme.subtext; font.pixelSize: 14 }
                    }
                    ServiceRow {
                        visible: window.provider === "mymemory"
                        title: window.t("本插件今日提交", "Submitted today")
                        description: window.t("按本机日期统计，非服务器余额", "Local date; not server balance")
                        Text { text: backend.state.today; color: Theme.text; font.pixelSize: 14 }
                    }
                    ServiceRow {
                        visible: ["mymemory", "google", "edge", "sogou", "ecdict"].indexOf(window.provider) < 0
                        title: ["zvo", "youdao"].indexOf(window.provider) >= 0 ? window.t("API 密钥（可选）", "API key (optional)") : window.t("API 密钥", "API key")
                        description: window.t("由 Windows 为当前用户加密保存", "Encrypted for the current Windows user")
                        InputBox {
                            id: key
                            objectName: "apiKey"
                            width: Math.max(180, scroll.width * .48 - 42)
                            text: backend.state.apiKey
                            echoMode: reveal.checked ? TextInput.Normal : TextInput.Password
                            placeholder: ["zvo", "youdao"].indexOf(window.provider) >= 0 ? window.t("可留空", "Optional") : window.t("输入 API 密钥", "Enter API key")
                            onEditingFinished: backend.action("apiKey", text)
                            onTextChanged: if (inputFocused) backend.action("apiKey", text)
                        }
                        ToolButton {
                            id: reveal; glyph: "\uE890"; tip: window.t("显示 / 隐藏密钥", "Show / hide key")
                            onClicked: checked = !checked
                        }
                    }
                    ServiceRow {
                        visible: window.provider !== "mymemory" && window.provider !== "ecdict"
                        title: window.t("服务地址", "Endpoint")
                        description: window.provider === "ai" ? window.t("兼容 OpenAI 的地址，支持完整 /chat/completions URL", "OpenAI-compatible base or full /chat/completions URL") : window.provider === "zvo" ? "translate.js / Zvo" : window.t("留空使用默认地址", "Empty uses the default endpoint")
                        InputBox {
                            objectName: "endpoint"
                            width: Math.max(220, scroll.width * .48)
                            text: backend.state.endpoint; placeholder: backend.state.endpointHint
                            onTextChanged: if (inputFocused) backend.action("endpoint", text)
                            onEditingFinished: backend.action("endpoint", text)
                        }
                    }
                    ServiceRow {
                        visible: ["ai", "openai", "deepseek", "doubao", "hunyuan"].indexOf(window.provider) >= 0
                        title: window.provider === "doubao" ? window.t("模型 / 接入点 ID", "Model / Endpoint ID") : window.t("模型", "Model")
                        description: window.provider === "ai" || window.provider === "doubao" ? window.t("填写已开通的模型或接入点", "Enter your model or inference endpoint") : window.t("留空使用默认模型", "Empty uses the default model")
                        InputBox {
                            objectName: "model"
                            width: Math.max(220, scroll.width * .48)
                            text: backend.state.model; placeholder: backend.state.modelHint
                            onTextChanged: if (inputFocused) backend.action("model", text)
                            onEditingFinished: backend.action("model", text)
                        }
                    }
                    ServiceRow {
                        visible: window.provider === "microsoft"
                        title: window.t("服务区域", "Region")
                        description: window.t("与 Azure 翻译资源的区域一致", "Match your Azure Translator resource")
                        InputBox {
                            objectName: "region"
                            width: Math.max(220, scroll.width * .48)
                            text: backend.state.region; placeholder: "eastasia"
                            onTextChanged: if (inputFocused) backend.action("region", text)
                            onEditingFinished: backend.action("region", text)
                        }
                    }
                    ServiceRow {
                        visible: window.provider === "youdao"
                        title: window.t("应用 ID（可选）", "App ID (optional)")
                        description: window.t("留空免费英汉查词；配合密钥启用官方整句翻译", "Free English-Chinese lookup; add App ID and key for official translation")
                        InputBox {
                            objectName: "appId"
                            width: Math.max(220, scroll.width * .48)
                            text: backend.state.appId || ""; placeholder: "App ID"
                            onTextChanged: if (inputFocused) backend.action("appId", text)
                            onEditingFinished: backend.action("appId", text)
                        }
                    }
                    ServiceRow {
                        visible: window.provider === "ecdict"
                        title: window.t("离线英汉词典", "Offline English-Chinese dictionary")
                        description: window.t("77 万词条 · 约 55 MB。从 GitHub 下载一次，之后离线使用；升级保留。未下载时自动降级会跳过此渠道。", "770,000 entries · about 55 MB. Download once from GitHub for offline use; kept across upgrades. Skipped by fallback until installed.")
                        Column {
                            spacing: 8; width: Math.max(180, scroll.width * .36)
                            Text {
                                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Theme.fontBody
                                color: Theme.subtext
                                text: backend.state.dictionaryDownloading
                                    ? window.t("正在下载 ", "Downloading ") + Math.floor(100 * (backend.state.dictionaryReceived || 0) / (backend.state.dictionarySize || 1)) + "%"
                                    : backend.state.dictionaryReady ? window.t("已安装 · 可离线使用", "Installed · available offline") : window.t("未下载", "Not downloaded")
                            }
                            Rectangle {
                                visible: backend.state.dictionaryDownloading || false
                                width: parent.width; height: 4; radius: 2; color: Theme.cardBorder
                                Rectangle {
                                    width: parent.width * Math.min(1, (backend.state.dictionaryReceived || 0) / (backend.state.dictionarySize || 1))
                                    height: parent.height; radius: 2; color: Theme.accent
                                }
                            }
                            FlatButton {
                                objectName: "dictionary-download"
                                visible: !backend.state.dictionaryDownloading
                                enabled: !backend.state.dictionaryReady
                                text: backend.state.dictionaryReady ? window.t("已下载", "Downloaded") : window.t("一键下载", "Download")
                                onClicked: backend.action("downloadDictionary", null)
                            }
                            FlatButton {
                                objectName: "dictionary-cancel"
                                visible: backend.state.dictionaryDownloading || false
                                text: window.t("取消下载", "Cancel download")
                                onClicked: backend.action("cancelDictionary", null)
                            }
                            Text {
                                width: parent.width; wrapMode: Text.Wrap; font.pixelSize: Theme.fontCaption
                                color: Theme.subtext; text: backend.state.dictionaryMessage || ""
                                visible: text.length > 0
                            }
                        }
                    }
                    ServiceRow {
                        visible: window.provider === "sogou"
                        title: window.t("搜狗翻译 / 词典", "Sogou translation / dictionary")
                        description: window.t("使用公开网页结果；遇到限流或验证时自动切换其他渠道。", "Uses public web results; automatic fallback when verification or rate limits occur.")
                    }
                    ServiceRow {
                        visible: window.provider === "edge"
                        title: window.t("免费接口", "Free endpoint")
                        description: window.t("Edge 取票失效时使用同属微软的 Bing 翻译入口", "Uses Microsoft Bing translation when Edge authentication is unavailable")
                    }
                }
              }
            }
        }
        Item {
            id: footer
            anchors.bottom: parent.bottom; width: parent.width; height: 66
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            Text {
                x: 24; y: 25; width: Math.max(0, parent.width - actions.width - 64)
                text: backend.state.message; color: Theme.subtext; font.pixelSize: 13
                elide: Text.ElideRight
            }
            Row {
                id: actions
                anchors.right: parent.right; anchors.rightMargin: 24
                anchors.bottom: parent.bottom; anchors.bottomMargin: 14; spacing: 8
                FlatButton {
                    visible: backend.state.batch && !window.home
                    flat: true; text: window.t("查看测试结果", "View results")
                    onClicked: { scroll.contentY = 0; backend.action("home", null) }
                }
                FlatButton {
                    objectName: "test-current"
                    visible: !window.home
                    text: window.t("测试当前", "Test current")
                    enabled: !backend.state.testing
                    onClicked: { key.focus = false; backend.action("test", null) }
                }
                FlatButton { text: window.t("取消", "Cancel"); onClicked: backend.action("close", null) }
                FlatButton {
                    text: window.t("保存", "Save"); highlighted: true
                    onClicked: { key.focus = false; backend.action("save", null) }
                }
            }
        }
    }
}

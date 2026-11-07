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
    flags: Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.page
    property bool chinese: backend.state.chinese
    property string provider: backend.state.provider
    property bool home: backend.state.home || false
    property bool automatic: backend.state.routingMode === "auto"
    property bool dragging: false
    property var queue: backend.state.queue || []
    function t(zh, en) { return chinese ? zh : en }
    function move(from, to) {
        to = Math.max(1, Math.min(queue.length - 1, to))
        if (from === to) return
        backend.action("moveProvider", {provider: queue[from].provider,
            before: to < from ? queue[to].provider : to + 1 < queue.length ? queue[to + 1].provider : ""})
    }
    function statusText(row) {
        if (row.status === "running") return t("测试中", "Testing")
        if (row.status === "waiting") return t("等待中", "Queued")
        if (!row.configured) return t("待配置", "Configure")
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
        Rectangle {
            anchors.fill: parent; color: "transparent"; radius: 8
            border.width: 1; border.color: Theme.cardBorder
        }
        Item {
            id: chrome
            width: parent.width; height: 36
            Text {
                x: 18; anchors.verticalCenter: parent.verticalCenter
                text: window.title; color: Theme.subtext; font.pixelSize: 12
            }
            MouseArea {
                anchors.fill: parent
                onPressed: window.startSystemMove()
                onDoubleClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
            }
            ToolButton {
                anchors.right: parent.right; anchors.rightMargin: 8
                glyph: "\uE8BB"; tip: window.t("关闭", "Close"); tipEnabled: false
                onClicked: backend.action("close", null)
            }
        }
        Column {
            id: heading
            x: 24; y: chrome.height + 10
            width: parent.width - 48; spacing: 6
            Text { text: window.t("截图翻译", "Screenshot Translate"); color: Theme.text; font.pixelSize: 24; font.weight: Font.Bold }
            Text {
                width: parent.width
                text: window.t("测试渠道、调整降级队列，或打开渠道标签配置服务。",
                               "Test services, arrange fallback, or open a service tab to configure it.")
                color: Theme.subtext; font.pixelSize: 12; wrapMode: Text.Wrap
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
            anchors.top: heading.bottom; anchors.topMargin: 18
            anchors.left: parent.left; anchors.leftMargin: 24
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.bottom: footer.top; anchors.bottomMargin: 16
            clip: true
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
                                    ? window.t("主渠道优先，其余按成功率和响应速度排序；测试与实际翻译都会更新权重。", "Preferred service first; backups are ranked by reliability and speed from tests and translations.")
                                    : window.t("主渠道优先，失败后从上往下尝试。拖动左侧手柄或使用上下按钮排序。", "Preferred service first, then backups from top to bottom. Drag a handle or use the arrows."))
                                    + window.t(" 冷却 ", " Cooldown: ") + backend.state.recoveryMinutes + window.t(" 分钟；保存后生效。", " min. Save to apply.")
                            color: Theme.subtext; font.pixelSize: 12
                        }
                    }
                }
                Column {
                    id: queueRows
                    objectName: "fallback-queue"
                    width: parent.width; spacing: 6
                    Repeater {
                        model: window.queue
                        Item {
                            id: queueRow
                            required property var modelData
                            required property int index
                            objectName: "queue-row-" + modelData.provider
                            width: queueRows.width; height: 66
                            z: dragHandle.pressed ? 2 : 0
                            Rectangle {
                                id: rowSurface
                                width: parent.width; height: parent.height
                                color: dragHandle.pressed ? Theme.chipActive : Theme.card
                                radius: Theme.cardRadius
                                border.color: dragHandle.pressed ? Theme.accent : Theme.cardBorder
                                Item {
                                    x: 6; width: 28; height: parent.height
                                    Glyph {
                                        anchors.centerIn: parent; glyph: queueRow.modelData.preferred ? "\uE718" : "\uE76F"
                                        size: 15; color: queueRow.modelData.preferred ? Theme.accent : Theme.faint
                                    }
                                    MouseArea {
                                        id: dragHandle
                                        objectName: "queue-drag-" + queueRow.modelData.provider
                                        anchors.fill: parent; preventStealing: true
                                        enabled: !window.automatic && !queueRow.modelData.preferred && !backend.state.testing
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        property real pointerY: 0
                                        property real startY: 0
                                        property real startScroll: 0
                                        function updatePosition() {
                                            rowSurface.y = Math.max((1 - queueRow.index) * 72,
                                                Math.min((window.queue.length - queueRow.index - 1) * 72, pointerY - startY + scroll.contentY - startScroll))
                                        }
                                        onPressed: mouse => {
                                            pointerY = mapToItem(scroll, mouse.x, mouse.y).y
                                            startY = pointerY; startScroll = scroll.contentY; window.dragging = true
                                        }
                                        onPositionChanged: mouse => {
                                            if (!pressed) return
                                            pointerY = mapToItem(scroll, mouse.x, mouse.y).y; updatePosition()
                                        }
                                        onReleased: {
                                            const from = queueRow.index
                                            const to = from + Math.round(rowSurface.y / 72)
                                            rowSurface.y = 0; window.dragging = false; window.move(from, to)
                                        }
                                        onCanceled: { rowSurface.y = 0; window.dragging = false }
                                        Timer {
                                            interval: 24; repeat: true; running: dragHandle.pressed
                                            onTriggered: {
                                                const step = dragHandle.pointerY < 30 ? -9 : dragHandle.pointerY > scroll.height - 30 ? 9 : 0
                                                if (!step) return
                                                scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, scroll.contentY + step))
                                                dragHandle.updatePosition()
                                            }
                                        }
                                    }
                                }
                                Column {
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
                                        text: (queueRow.modelData.detail || queueRow.modelData.problem
                                            || (queueRow.modelData.provider === "ecdict" ? window.t("离线英汉词典 · 未收录会继续尝试", "Offline dictionary · misses fall through")
                                                : queueRow.modelData.samples ? window.t("已记录最近表现", "Recent performance recorded") : window.t("测试后显示连接结果和耗时", "Test to see connectivity and latency"))).replace(/\s+/g, " ")
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
                                            + window.t("权重 ", "Weight ") + queueRow.modelData.weight
                                            + (!queueRow.modelData.samples ? window.t("（初始）", " (initial)") : "")
                                    }
                                }
                                Row {
                                    id: rowActions
                                    anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; spacing: 2
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
              Rectangle {
                id: card
                visible: !window.home
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
                        description: window.t("内置 77 万词条。支持单词及收录短语；长句 / 未收录词可自动切换其他渠道。", "770,000 entries. Words and known phrases; automatic fallback for sentences or unknown words.")
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
        MouseArea {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            width: 14; height: 14; cursorShape: Qt.SizeFDiagCursor
            onPressed: window.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }
    }
}

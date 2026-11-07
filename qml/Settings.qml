pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import WinShun

Window {
    id: window
    visible: false
    width: 940; height: 548
    minimumWidth: 640; minimumHeight: 420
    title: backend.state.chinese ? "截图翻译设置 · Win顺" : "Screenshot Translate · WinShun"
    flags: Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.page
    property bool chinese: backend.state.chinese
    property string provider: backend.state.provider
    function t(zh, en) { return chinese ? zh : en }
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
            x: 24; y: chrome.height + 16
            width: parent.width - 48; spacing: 8
            Text { text: window.t("截图翻译", "Screenshot Translate"); color: Theme.text; font.pixelSize: 28; font.weight: Font.Bold }
            Text {
                width: parent.width
                text: window.t("配置各渠道；启用渠道和共用代理在 Win顺 插件面板中设置。",
                               "Configure services here; choose the active service and shared proxy in WinShun.")
                color: Theme.subtext; font.pixelSize: 14; wrapMode: Text.Wrap
            }
            Item { width: 1; height: 4 }
            ChoiceTabs {
                id: tabs
                objectName: "service-tabs"
                width: parent.width; labels: backend.state.names; current: backend.state.current
                onActivated: index => backend.action("select", index)
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
              Rectangle {
                id: card
                visible: !backend.state.showTestResults
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
              Rectangle {
                width: scroll.width; height: results.implicitHeight + 20
                visible: backend.state.showTestResults && backend.state.testResults.length > 0
                radius: Theme.cardRadius; color: Theme.card
                border.width: 1; border.color: Theme.cardBorder
                Column {
                    id: results
                    x: 12; y: 10; width: parent.width - 24; spacing: 4
                    Repeater {
                        model: backend.state.testResults
                        Row {
                            required property var modelData
                            width: results.width; spacing: 12
                            Text { width: 90; text: parent.modelData.name; color: Theme.text; font.pixelSize: 14 }
                            Text {
                                width: results.width - 102
                                text: parent.modelData.detail; wrapMode: Text.Wrap
                                color: parent.modelData.status === "success" ? Theme.accent : Theme.subtext
                                font.pixelSize: 14
                            }
                        }
                    }
                }
              }
            }
        }
        Item {
            id: footer
            anchors.bottom: parent.bottom; width: parent.width; height: 98
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            Text {
                x: 24; y: 10; width: parent.width - 170
                text: backend.state.message; color: Theme.subtext; font.pixelSize: 13
                elide: Text.ElideRight
            }
            FlatButton {
                visible: backend.state.batch && !backend.state.showTestResults
                anchors.right: parent.right; anchors.rightMargin: 24; y: 3
                implicitHeight: 28; flat: true; text: window.t("查看测试结果", "View results")
                onClicked: backend.action("showTests", null)
            }
            Row {
                id: actions
                anchors.right: parent.right; anchors.rightMargin: 24
                anchors.bottom: parent.bottom; anchors.bottomMargin: 14; spacing: 8
                FlatButton {
                    text: window.t("测试当前", "Test current")
                    enabled: !backend.state.testing
                    onClicked: { key.focus = false; backend.action("test", null) }
                }
                FlatButton {
                    objectName: "test-all"
                    glyph: "\uE72C"; text: window.t("测试全部渠道", "Test all services")
                    enabled: !backend.state.testing
                    onClicked: { key.focus = false; backend.action("testAll", null) }
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

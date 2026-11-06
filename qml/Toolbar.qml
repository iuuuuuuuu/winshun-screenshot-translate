pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import WinShun

Window {
    id: bar
    width: controls.implicitWidth + 20
    height: 46
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.card
    property bool chinese: backend.state.chinese
    function t(zh, en) { return chinese ? zh : en }
    Rectangle { anchors.fill: parent; color: "transparent"; radius: 8; border.width: 1; border.color: Theme.controlBorder }
    Item {
        anchors.fill: parent; focus: true
        Keys.onEscapePressed: backend.action("close", null)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) {
                backend.action("copy", !!(event.modifiers & Qt.ShiftModifier))
                event.accepted = true
            }
        }
        Row {
            id: controls
            x: 10; anchors.verticalCenter: parent.verticalCenter; spacing: 4
            FlatButton {
                id: source
                implicitHeight: 32; implicitWidth: 84; flat: true
                text: backend.state.sourceLabels[backend.state.codes.indexOf(backend.state.source)] || bar.t("自动", "Auto")
                trailingGlyph: "\uE70D"
                enabled: backend.state.ready && !backend.state.loading
                onClicked: sourceMenu.open()
                LanguageMenu {
                    id: sourceMenu
                    objectName: "source-menu"
                    hostWindow: bar; anchorItem: source
                    labels: backend.state.sourceLabels; codes: backend.state.codes
                    current: backend.state.source
                    onChosen: code => backend.action("source", code)
                }
            }
            ToolButton {
                glyph: "\uE8AB"; tip: bar.t("交换语言", "Swap languages")
                enabled: !backend.state.loading && backend.state.translatedText.length > 0
                onClicked: backend.action("swap", null)
            }
            FlatButton {
                id: target
                implicitHeight: 32; implicitWidth: 84; flat: true
                text: backend.state.sourceLabels[backend.state.codes.indexOf(backend.state.target)] || bar.t("中文", "Chinese")
                trailingGlyph: "\uE70D"
                enabled: backend.state.ready && !backend.state.loading
                onClicked: targetMenu.open()
                LanguageMenu {
                    id: targetMenu
                    objectName: "target-menu"
                    hostWindow: bar; anchorItem: target
                    labels: backend.state.sourceLabels.slice(1); codes: backend.state.codes.slice(1)
                    current: backend.state.target
                    onChosen: code => backend.action("target", code)
                }
            }
            Item {
                width: 13; height: 32
                Rectangle { anchors.centerIn: parent; width: 1; height: 18; color: Theme.divider }
            }
            ToolButton {
                glyph: "compare"; text: bar.t("对照", "Compare")
                checked: backend.state.comparison
                tip: bar.t("原文 / 译文对照", "Compare source and translation")
                onClicked: backend.action("compare", null)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: backend.state.provider
                color: Theme.subtext; font.pixelSize: 12
                width: Math.min(82, implicitWidth); elide: Text.ElideRight
                HoverHandler { id: serviceHover }
                ToolTip.visible: serviceHover.hovered
                ToolTip.text: backend.state.providerNote
            }
            ToolButton {
                glyph: "\uE8C8"; tip: bar.t("复制译文 · Ctrl+C", "Copy translation · Ctrl+C")
                enabled: backend.state.translatedText.length > 0
                onClicked: backend.action("copy", false)
            }
            ToolButton {
                glyph: "capture"; tip: bar.t("重新截图", "Capture again")
                onClicked: backend.action("retry", null)
            }
            ToolButton {
                glyph: "text"; checked: backend.state.fullText
                tip: bar.t("完整译文", "Full translation")
                enabled: backend.state.translatedText.length > 0
                onClicked: backend.action("text", null)
            }
            ToolButton { glyph: "\uE713"; tip: bar.t("渠道配置", "Service settings"); onClicked: backend.action("settings", null) }
            ToolButton { glyph: "\uE8BB"; tip: bar.t("关闭 · Esc", "Close · Esc"); tipEnabled: false; onClicked: backend.action("close", null) }
        }
    }
}

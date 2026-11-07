pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import WinShun

Window {
    id: bar
    width: controls.implicitWidth + 8
    height: 32
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.card
    property bool chinese: backend.state.chinese
    function t(zh, en) { return chinese ? zh : en }
    component BarButton: ToolButton {
        implicitWidth: text.length ? (bar.chinese ? 49 : 68) : 26; implicitHeight: 26
        textPixelSize: 11
    }
    onVisibleChanged: if (!visible) { sourceMenu.hide(); targetMenu.hide() }
    Rectangle { anchors.fill: parent; color: "transparent"; radius: 5; border.width: 1; border.color: Theme.controlBorder }
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
            x: 4; anchors.verticalCenter: parent.verticalCenter; spacing: 1
            FlatButton {
                id: source
                objectName: "toolbar-source"
                implicitHeight: 26; implicitWidth: 60; flat: true
                textPixelSize: 11; maximumTextWidth: 36
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
            BarButton {
                objectName: "toolbar-swap"; glyph: "\uE8AB"; tip: bar.t("交换语言", "Swap languages")
                enabled: !backend.state.loading && backend.state.translatedText.length > 0
                onClicked: backend.action("swap", null)
            }
            FlatButton {
                id: target
                objectName: "toolbar-target"
                implicitHeight: 26; implicitWidth: 60; flat: true
                textPixelSize: 11; maximumTextWidth: 36
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
                width: 5; height: 26
                Rectangle { anchors.centerIn: parent; width: 1; height: 14; color: Theme.divider }
            }
            BarButton {
                objectName: "toolbar-compare"; glyph: "compare"; text: bar.t("对照", "Compare")
                checked: backend.state.comparison
                tip: bar.t("原文 / 译文对照", "Compare source and translation")
                onClicked: backend.action("compare", null)
            }
            Text {
                id: serviceLabel
                anchors.verticalCenter: parent.verticalCenter
                objectName: "toolbar-provider"
                text: backend.state.provider
                color: backend.state.providerFallback ? Theme.caution : Theme.subtext; font.pixelSize: 11
                width: implicitWidth
                HoverHandler { id: serviceHover }
                ServiceTip {
                    objectName: "provider-tip"
                    anchorItem: serviceLabel; hovered: serviceHover.hovered
                    text: backend.state.providerNote || ""
                }
            }
            BarButton {
                objectName: "toolbar-copy"; glyph: "\uE8C8"; tip: bar.t("复制译文 · Ctrl+C", "Copy translation · Ctrl+C")
                enabled: backend.state.translatedText.length > 0
                onClicked: backend.action("copy", false)
            }
            BarButton {
                objectName: "toolbar-retry"; glyph: "capture"; tip: bar.t("重新截图", "Capture again")
                onClicked: backend.action("retry", null)
            }
            BarButton {
                objectName: "toolbar-text"; glyph: "text"; checked: backend.state.fullText
                tip: bar.t("完整译文", "Full translation")
                enabled: backend.state.translatedText.length > 0
                onClicked: backend.action("text", null)
            }
            BarButton { objectName: "toolbar-settings"; glyph: "\uE713"; tip: bar.t("渠道配置", "Service settings"); onClicked: backend.action("settings", null) }
            BarButton { objectName: "toolbar-close"; glyph: "\uE8BB"; tip: bar.t("关闭 · Esc", "Close · Esc"); tipEnabled: false; onClicked: backend.action("close", null) }
        }
    }
}

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
    property rect availableArea: Qt.rect(0, 0, 1280, 900)
    function dismissProviderMenu() { if (providerLoader.item) providerLoader.item.hide() }
    function openProviderMenu() {
        if (!backend.state.ready || backend.state.loading) return
        if (!providerLoader.item) providerLoader.setSource("qrc:/ui/qml/ProviderMenu.qml",
            {hostWindow: bar, anchorItem: serviceControl, objectName: "pin-provider-menu"})
        if (providerLoader.item) providerLoader.item.visible ? providerLoader.item.hide() : providerLoader.item.open()
    }
    Loader { id: providerLoader }
    Connections { target: providerLoader.item; function onChosen(id) { backend.action("provider", id) } }
    function t(zh, en) { return chinese ? zh : en }
    component BarButton: ToolButton {
        implicitWidth: text.length ? (bar.chinese ? 49 : 68) : 26; implicitHeight: 26
        textPixelSize: 11
    }
    onVisibleChanged: if (!visible) { sourceMenu.hide(); targetMenu.hide(); dismissProviderMenu() }
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
            Item {
                id: serviceControl; objectName: "toolbar-provider-button"
                anchors.verticalCenter: parent.verticalCenter
                width: serviceLabel.width + 18; height: 26
                Text {
                    id: serviceLabel; objectName: "toolbar-provider"
                    anchors.verticalCenter: parent.verticalCenter
                    text: backend.state.providerSwitching ? bar.t("正在切换 · ", "Switching · ") + backend.state.providerChoiceLabel
                        : backend.state.providerSwitchError ? bar.t("切换失败 · ", "Failed · ") + backend.state.providerChoiceLabel : backend.state.provider
                    color: Theme.subtext; font.pixelSize: 11
                    width: Math.min(160, implicitWidth); elide: Text.ElideRight
                }
                Glyph { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; size: 10; glyph: "\uE70D"; color: Theme.subtext; visible: !backend.state.providerSwitching }
                Canvas {
                    id: spinner; objectName: "pin-provider-spinner"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    width: 12; height: 12; visible: !!backend.state.providerSwitching; property real angle: 0
                    onAngleChanged: requestPaint()
                    onPaint: { const ctx=getContext("2d");ctx.reset();ctx.strokeStyle=Theme.accent;ctx.lineWidth=2;ctx.beginPath();ctx.arc(6,6,4,angle,angle+Math.PI*1.4);ctx.stroke() }
                    NumberAnimation on angle { from: 0; to: Math.PI*2; duration: 900; loops: Animation.Infinite; running: spinner.visible && bar.visible }
                }
                HoverHandler { id: serviceHover }
                TapHandler { enabled: !!backend.state.ready && !backend.state.loading; onTapped: bar.openProviderMenu() }
                ServiceTip {
                    objectName: "provider-tip"
                    anchorItem: serviceLabel; hovered: serviceHover.hovered
                    text: backend.state.providerSwitchError ? backend.state.status : backend.state.providerNote || bar.t("点击切换本次翻译渠道", "Click to change service")
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

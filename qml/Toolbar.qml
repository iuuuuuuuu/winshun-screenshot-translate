pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import WinShun

Window {
    id: bar
    width: controls.implicitWidth + 12
    height: 36
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.card
    property bool chinese: backend.state.chinese
    readonly property bool compact: (backend.state.captureWidth || 0) < 380
    function t(zh, en) { return chinese ? zh : en }
    function language(code) {
        return backend.state.sourceLabels[backend.state.codes.indexOf(code)] || t("自动", "Auto")
    }
    component BarButton: ToolButton {
        implicitWidth: 28; implicitHeight: 28
    }
    component LanguageButton: FlatButton {
        implicitWidth: 64; implicitHeight: 28; flat: true
        textPixelSize: 12; maximumTextWidth: 38
        trailingGlyph: "\uE70D"
        enabled: backend.state.ready && !backend.state.loading
    }
    Rectangle { anchors.fill: parent; color: "transparent"; radius: 6; border.width: 1; border.color: Theme.controlBorder }
    onVisibleChanged: if (!visible) { moreMenu.hide(); sourceMenu.hide(); targetMenu.hide() }
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
            x: 6; anchors.verticalCenter: parent.verticalCenter; spacing: 2
            LanguageButton {
                id: source
                objectName: "toolbar-source"
                visible: !bar.compact
                text: bar.language(backend.state.source)
                onClicked: sourceMenu.open()
            }
            BarButton {
                visible: !bar.compact
                glyph: "\uE8AB"; tip: bar.t("交换语言", "Swap languages")
                enabled: !backend.state.loading && backend.state.translatedText.length > 0
                onClicked: backend.action("swap", null)
            }
            LanguageButton {
                id: target
                objectName: "toolbar-target"
                text: bar.language(backend.state.target)
                onClicked: targetMenu.open()
            }
            Item {
                width: 5; height: 28
                Rectangle { anchors.centerIn: parent; width: 1; height: 16; color: Theme.divider }
            }
            BarButton {
                objectName: "toolbar-compare"
                glyph: "compare"; checked: backend.state.comparison
                tip: bar.t("上下双语对照", "Compare source and translation")
                onClicked: backend.action("compare", null)
            }
            BarButton {
                glyph: "\uE8C8"; tip: bar.t("复制译文 · Ctrl+C", "Copy translation · Ctrl+C")
                enabled: backend.state.translatedText.length > 0
                onClicked: backend.action("copy", false)
            }
            BarButton {
                visible: !bar.compact
                glyph: "text"; checked: backend.state.fullText
                tip: bar.t("完整译文", "Full translation")
                enabled: backend.state.translatedText.length > 0
                onClicked: backend.action("text", null)
            }
            BarButton {
                id: more
                objectName: "toolbar-more"
                glyph: "\uE712"; tip: bar.t("更多操作", "More actions"); checked: moreMenu.visible
                onClicked: moreMenu.visible ? moreMenu.hide() : moreMenu.open()
            }
            BarButton {
                objectName: "toolbar-close"
                glyph: "\uE8BB"; tip: bar.t("关闭 · Esc", "Close · Esc"); tipEnabled: false
                onClicked: backend.action("close", null)
            }
        }
    }
    LanguageMenu {
        id: sourceMenu
        objectName: "source-menu"
        hostWindow: bar; anchorItem: bar.compact ? more : source
        labels: backend.state.sourceLabels; codes: backend.state.codes
        current: backend.state.source
        onChosen: code => backend.action("source", code)
    }
    LanguageMenu {
        id: targetMenu
        objectName: "target-menu"
        hostWindow: bar; anchorItem: target
        labels: backend.state.sourceLabels.slice(1); codes: backend.state.codes.slice(1)
        current: backend.state.target
        onChosen: code => backend.action("target", code)
    }
    Window {
        id: moreMenu
        objectName: "toolbar-more-menu"
        visible: false
        transientParent: bar; screen: bar.screen
        flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
        color: Theme.card
        width: bar.chinese ? 202 : 242; height: entries.implicitHeight + 12
        property bool receivedFocus: false
        function open() {
            const at = more.mapToGlobal(more.width, more.height + 6)
            x = Math.max(backend.state.menuLeft + 4, Math.min(at.x - width, backend.state.menuRight - width - 4))
            y = at.y + height <= backend.state.menuBottom - 4 ? at.y : more.mapToGlobal(0, 0).y - height - 6
            y = Math.max(backend.state.menuTop + 4, y)
            receivedFocus = false
            show(); requestActivate(); menuKeys.forceActiveFocus()
        }
        function perform(action, value) { hide(); backend.action(action, value) }
        onActiveChanged: { if (active) receivedFocus = true; else if (receivedFocus && visible) hide() }
        onClosing: event => { event.accepted = false; hide() }
        Rectangle { anchors.fill: parent; color: "transparent"; radius: 6; border.width: 1; border.color: Theme.controlBorder }
        Item { id: menuKeys; anchors.fill: parent; focus: true; Keys.onEscapePressed: moreMenu.hide() }
        Column {
            id: entries
            x: 6; y: 6; width: parent.width - 12; spacing: 2
            Text {
                width: parent.width; height: 26; leftPadding: 8; rightPadding: 8
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                color: Theme.subtext; font.pixelSize: 12
                text: backend.state.providerNote || bar.t("翻译操作", "Translation actions")
            }
            FlatButton {
                objectName: "toolbar-menu-source"
                width: parent.width; height: 30; flat: true; textPixelSize: 12
                text: bar.t("原文语言：", "Source: ") + bar.language(backend.state.source)
                trailingGlyph: "\uE70D"; enabled: source.enabled
                onClicked: { moreMenu.hide(); sourceMenu.open() }
            }
            FlatButton {
                width: parent.width; height: 30; flat: true; textPixelSize: 12
                glyph: "\uE8AB"; text: bar.t("交换语言", "Swap languages")
                enabled: !backend.state.loading && backend.state.translatedText.length > 0
                onClicked: moreMenu.perform("swap", null)
            }
            FlatButton {
                width: parent.width; height: 30; flat: true; textPixelSize: 12
                glyph: "text"; text: bar.t("完整译文", "Full translation")
                enabled: backend.state.translatedText.length > 0
                onClicked: moreMenu.perform("text", null)
            }
            FlatButton {
                width: parent.width; height: 30; flat: true; textPixelSize: 12
                glyph: "\uE8C8"; text: bar.t("复制原文", "Copy source")
                enabled: backend.state.sourceText.length > 0
                onClicked: moreMenu.perform("copy", true)
            }
            FlatButton {
                width: parent.width; height: 30; flat: true; textPixelSize: 12
                glyph: "capture"; text: bar.t("重新截图", "Capture again")
                onClicked: moreMenu.perform("retry", null)
            }
            FlatButton {
                objectName: "toolbar-menu-settings"
                width: parent.width; height: 30; flat: true; textPixelSize: 12
                glyph: "\uE713"; text: bar.t("渠道配置", "Service settings")
                onClicked: moreMenu.perform("settings", null)
            }
        }
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import WinShun

// A separate, non-activating tooltip also fits above the compact 32px toolbar.
Window {
    id: tip
    required property Item anchorItem
    property bool hovered: false
    property string text: ""
    readonly property var hostWindow: anchorItem.Window.window
    readonly property bool canShow: hovered && text.length > 0 && !!hostWindow && hostWindow.visible
    visible: false
    transientParent: hostWindow
    screen: hostWindow ? hostWindow.screen : null
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: Theme.menuBackground
    width: Math.min(360, measure.implicitWidth + 24)
    height: message.implicitHeight + 20
    function dismiss() { delay.stop(); hide() }
    function refresh() { dismiss(); if (canShow) delay.restart() }
    function open() {
        if (!canShow) return
        const p = anchorItem.mapToGlobal(0, 0)
        x = Math.max(Screen.virtualX + 4, Math.min(p.x + (anchorItem.width - width) / 2, Screen.virtualX + Screen.width - width - 4))
        y = p.y - height - 6 >= Screen.virtualY + 4 ? p.y - height - 6 : p.y + anchorItem.height + 6
        y = Math.max(Screen.virtualY + 4, Math.min(y, Screen.virtualY + Screen.height - height - 4))
        show()
    }
    onCanShowChanged: refresh()
    onTextChanged: refresh()
    Timer { id: delay; interval: 450; onTriggered: tip.open() }
    Connections {
        target: tip.hostWindow
        function onXChanged() { tip.refresh() }
        function onYChanged() { tip.refresh() }
        function onVisibleChanged() { if (!tip.hostWindow.visible) tip.dismiss() }
    }
    Text { id: measure; visible: false; text: tip.text; textFormat: Text.PlainText; font.pixelSize: 12 }
    Rectangle { anchors.fill: parent; color: "transparent"; radius: Theme.cardRadius; border.width: 1; border.color: Theme.controlBorder }
    Text {
        id: message
        x: 12; y: 10; width: parent.width - 24
        text: tip.text; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: Theme.text; font.pixelSize: 12
    }
}

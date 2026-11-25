import QtQuick
import WinShun

AdaptiveWindow {
    id: menu
    required property var hostWindow
    required property Item anchorItem
    property real cornerRadius: 10
    signal chosen(string id)
    visible: false; transientParent: hostWindow; screen: hostWindow.screen
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: Theme.card
    preferredWidth: 330; contentHeight: Math.min(400, entries.implicitHeight + 76)
    availableArea: hostWindow.availableArea
    function open() {
        backend.action("providerMenu", true)
        const work = hostWindow.availableArea
        const at = anchorItem.mapToGlobal(0, 0)
        x = Math.max(work.x + 4, Math.min(at.x, work.x + work.width - width - 4))
        y = at.y - height - 5 >= work.y + 4 ? at.y - height - 5 : at.y + anchorItem.height + 5
        y = Math.max(work.y + 4, Math.min(y, work.y + work.height - height - 4))
        show()
    }
    onClosing: event => { event.accepted = false; hide() }
    Connections {
        target: menu.hostWindow
        function onVisibleChanged() { if (!menu.hostWindow.visible) menu.hide() }
        function onXChanged() { menu.hide() }
        function onYChanged() { menu.hide() }
    }
    Rectangle { anchors.fill: parent; color: Theme.card; radius: menu.cornerRadius; border.color: Theme.controlBorder; border.width: 1 }
    Text { x: 14; y: 12; text: backend.state.chinese ? "切换本次翻译渠道" : "Service for this translation"; color: Theme.subtext; font.pixelSize: 12 }
    Flickable {
        id: scroll; objectName: "word-provider-scroll"
        x: 6; y: 36; width: parent.width - 12; height: parent.height - 77
        contentWidth: width; contentHeight: entries.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        Column {
            id: entries; width: scroll.width; spacing: 2
            Repeater {
                model: backend.state.providerOptions || []
                Rectangle {
                    required property var modelData
                    objectName: "word-provider-option-" + (modelData.id || "auto")
                    width: entries.width - 7; height: modelData.detail ? 52 : 34; radius: 6
                    color: modelData.id === (backend.state.providerChoice || "") ? Theme.chipActive : rowMouse.containsMouse ? Theme.hover : "transparent"
                    opacity: modelData.selectable ? 1 : 0.5
                    Text { x: 10; y: modelData.detail ? 7 : 8; width: parent.width - 40; text: modelData.name; font.pixelSize: 13; color: Theme.text; elide: Text.ElideRight }
                    Text { x: 10; y: 29; width: parent.width - 20; visible: !!modelData.detail; text: modelData.detail || ""; font.pixelSize: 10; color: Theme.faint; elide: Text.ElideRight }
                    Glyph { anchors.right: parent.right; anchors.rightMargin: 10; y: 9; glyph: "\uE73E"; size: 12; color: Theme.accent; visible: modelData.id === (backend.state.providerChoice || "") }
                    MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; enabled: !!modelData.selectable; cursorShape: Qt.PointingHandCursor; onClicked: { menu.hide(); menu.chosen(modelData.id) } }
                }
            }
        }
    }
    ListScrollBar { flickable: scroll; anchors.right: scroll.right; anchors.top: scroll.top; anchors.bottom: scroll.bottom }
    FlatButton { x: 8; y: parent.height - 35; width: parent.width - 16; height: 28; flat: true; textPixelSize: 12; text: backend.state.chinese ? "管理翻译渠道…" : "Manage services…"; onClicked: { menu.hide(); backend.action("providerSettings", null) } }
}

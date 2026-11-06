pragma ComponentBehavior: Bound
import QtQuick
import WinShun

Window {
    id: menu
    property var hostWindow
    property Item anchorItem
    property var labels: []
    property var codes: []
    property string current
    property int cursor: 0
    property bool receivedFocus: false
    signal chosen(string code)
    visible: false
    transientParent: hostWindow
    screen: hostWindow.screen
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.card
    width: 150; height: entries.implicitHeight + 12
    function open() {
        let point = anchorItem.mapToGlobal(0, anchorItem.height + 6)
        x = Math.max(backend.state.menuLeft + 4, Math.min(point.x, backend.state.menuRight - width - 4))
        y = point.y + height <= backend.state.menuBottom - 4
            ? point.y : anchorItem.mapToGlobal(0, 0).y - height - 6
        y = Math.max(backend.state.menuTop + 4, y)
        cursor = Math.max(0, codes.indexOf(current))
        receivedFocus = false
        show()
        requestActivate()
        keys.forceActiveFocus()
    }
    function choose(index) { hide(); chosen(codes[index]) }
    onActiveChanged: {
        if (active) receivedFocus = true
        else if (receivedFocus && visible) hide()
    }
    onClosing: event => { event.accepted = false; hide() }
    onVisibleChanged: backend.action("menu", [objectName, visible])
    Rectangle { anchors.fill: parent; color: "transparent"; radius: Theme.cardRadius; border.width: 1; border.color: Theme.controlBorder }
    Item {
        id: keys
        anchors.fill: parent; focus: true
        Keys.onEscapePressed: menu.hide()
        Keys.onUpPressed: menu.cursor = (menu.cursor + menu.codes.length - 1) % menu.codes.length
        Keys.onDownPressed: menu.cursor = (menu.cursor + 1) % menu.codes.length
        Keys.onReturnPressed: menu.choose(menu.cursor)
        Keys.onEnterPressed: menu.choose(menu.cursor)
        Column {
            id: entries
            x: 6; y: 6; width: parent.width - 12; spacing: 2
            Repeater {
                model: menu.labels
                FlatButton {
                    required property int index
                    required property string modelData
                    width: entries.width; height: 32
                    text: modelData; flat: true
                    highlighted: index === menu.cursor
                    glyph: menu.codes[index] === menu.current ? "\uE73E" : ""
                    onClicked: menu.choose(index)
                }
            }
        }
    }
}

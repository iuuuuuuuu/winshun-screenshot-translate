import QtQuick
import WinShun

Window {
    id: mask
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: "#33000000"
    Item {
        anchors.fill: parent; focus: true
        Keys.onEscapePressed: backend.action("close", null)
        Rectangle {
            id: selection
            visible: backend.state.selecting
            x: backend.state.selectionX; y: backend.state.selectionY
            width: backend.state.selectionWidth; height: backend.state.selectionHeight
            color: "#110067C0"; border.width: 2; border.color: "#60CDFF"
        }
        MouseArea {
            id: pointer
            anchors.fill: parent; cursorShape: Qt.CrossCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: mouse => {
                if (mouse.button === Qt.RightButton) backend.action("close", null)
                else backend.action("begin", null)
            }
            onPositionChanged: if (pressed) backend.action("motion", null)
            onReleased: mouse => {
                if (mouse.button === Qt.LeftButton)
                    backend.action("end", null)
            }
        }
    }
}

import QtQuick
import WinShun

Window {
    id: icon
    visible: false
    width: 32; height: 32
    title: backend.state.chinese ? "翻译选中文字" : "Translate selection"
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    Rectangle {
        anchors.fill: parent
        radius: 7
        color: pointer.containsMouse ? "#005A9E" : "#0067C0"
        border.color: "#80FFFFFF"
        Text {
            anchors.centerIn: parent
            text: "译"; color: "white"; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        MouseArea {
            id: pointer
            objectName: "translate-selection-icon"
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: backend.action("translate", null)
            Accessible.role: Accessible.Button
            Accessible.name: icon.title
            Accessible.onPressAction: backend.action("translate", null)
        }
        HoverTip { target: pointer.containsMouse ? pointer : null; delay: 450; text: icon.title }
    }
}

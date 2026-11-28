import QtQuick
import WinShun

Window {
    id: icon
    readonly property bool hoverTranslate: !!backend.state.hoverTranslate
    property bool activated: false
    function translate(hovered) {
        if (!visible || activated || (hovered && (!hoverTranslate || !pointer.containsMouse || pointer.pressed))) return
        activated = true
        hoverDelay.stop()
        backend.action(hovered ? "hoverTranslate" : "translate", null)
    }
    onVisibleChanged: { hoverDelay.stop(); activated = false }
    onHoverTranslateChanged: hoverDelay.stop()
    Timer {
        id: hoverDelay; objectName: "selection-hover-delay"
        interval: 350; repeat: false
        onTriggered: icon.translate(true)
    }
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
            onEntered: if (icon.visible && icon.hoverTranslate && !icon.activated && !pressed) hoverDelay.restart()
            onExited: hoverDelay.stop()
            onPressed: hoverDelay.stop()
            onCanceled: hoverDelay.stop()
            onClicked: icon.translate(false)
            Accessible.role: Accessible.Button
            Accessible.name: icon.title
            Accessible.onPressAction: icon.translate(false)
        }
        HoverTip { target: pointer.containsMouse ? pointer : null; delay: 450; text: icon.title }
    }
}

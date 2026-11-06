import QtQuick
import WinShun
Window {
    width: Math.min(420, Math.max(220, label.implicitWidth + 76))
    height: Math.max(58, label.implicitHeight + 28)
    visible: false
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: Theme.card
    Rectangle { anchors.fill: parent; color: "transparent"; radius: 6; border.width: 1; border.color: Theme.cardBorder }
    Canvas {
        id: spinner
        x: 16; anchors.verticalCenter: parent.verticalCenter; width: 24; height: 24
        visible: backend.state.loading
        property real angle: 0
        onPaint: {
            var ctx=getContext("2d");ctx.reset();ctx.lineWidth=2;ctx.lineCap="round"
            ctx.strokeStyle=Theme.track;ctx.beginPath();ctx.arc(12,12,9,0,Math.PI*2);ctx.stroke()
            ctx.strokeStyle=Theme.accent;ctx.beginPath();ctx.arc(12,12,9,angle,angle+Math.PI*1.4);ctx.stroke()
        }
        onAngleChanged: requestPaint()
        NumberAnimation on angle { from: 0; to: Math.PI*2; duration: 1050; loops: Animation.Infinite; running: spinner.visible }
    }
    Text {
        id: label; x: backend.state.loading ? 52 : 16; y: 14
        width: Math.min(344, implicitWidth); text: backend.state.status
        font.pixelSize: 14; color: Theme.text; wrapMode: Text.Wrap
    }
}

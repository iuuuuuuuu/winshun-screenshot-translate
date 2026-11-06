import QtQuick
import WinShun
Window {
    id: statusWindow
    property real captureWidth: Math.max(1, backend.state.captureWidth || 1)
    property real captureHeight: Math.max(1, backend.state.captureHeight || 1)
    property bool compact: captureWidth < 220 || captureHeight < 60
    property bool iconOnly: backend.state.loading && (captureWidth < 72 || captureHeight < 18)
    property real textSize: compact ? Math.max(10, Math.min(12, captureHeight / 3)) : 12
    property real spinnerSize: compact ? 12 : 16
    property real padding: compact ? 4 : 8
    property real leading: backend.state.loading ? spinnerSize + 6 : 0
    property real maximumWidth: backend.state.loading ? Math.max(64, Math.min(220, captureWidth)) : 240
    width: iconOnly ? 24 : Math.ceil(Math.min(maximumWidth, label.implicitWidth + leading + padding * 2))
    height: iconOnly ? 24 : Math.ceil(Math.max(compact ? 24 : 32, label.implicitHeight + padding * 2))
    visible: false
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    Rectangle { anchors.fill: parent; color: Theme.card; radius: 6; border.width: 1; border.color: Theme.cardBorder }
    Canvas {
        id: spinner
        x: statusWindow.iconOnly ? (statusWindow.width - width) / 2 : statusWindow.padding
        anchors.verticalCenter: parent.verticalCenter
        width: statusWindow.spinnerSize; height: width
        visible: backend.state.loading
        property real angle: 0
        onPaint: {
            var ctx=getContext("2d");ctx.reset();ctx.lineWidth=1.5;ctx.lineCap="round"
            var center=width/2, radius=center-2
            ctx.strokeStyle=Theme.track;ctx.beginPath();ctx.arc(center,center,radius,0,Math.PI*2);ctx.stroke()
            ctx.strokeStyle=Theme.accent;ctx.beginPath();ctx.arc(center,center,radius,angle,angle+Math.PI*1.4);ctx.stroke()
        }
        onWidthChanged: requestPaint()
        onAngleChanged: requestPaint()
        NumberAnimation on angle { from: 0; to: Math.PI*2; duration: 1050; loops: Animation.Infinite; running: spinner.visible }
    }
    Text {
        id: label
        objectName: "status-label"
        x: statusWindow.padding + statusWindow.leading
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(1, statusWindow.width - x - statusWindow.padding)
        visible: !statusWindow.iconOnly
        text: backend.state.loading && statusWindow.compact
            ? (backend.state.loadingStage === "recognize"
                ? (backend.state.chinese ? "识别中" : "Reading")
                : (backend.state.chinese ? "翻译中" : "Translating"))
            : backend.state.status
        font.pixelSize: statusWindow.textSize; color: Theme.text
        wrapMode: backend.state.loading ? Text.NoWrap : Text.Wrap
        elide: backend.state.loading ? Text.ElideRight : Text.ElideNone
    }
}

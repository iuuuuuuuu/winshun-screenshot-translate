import QtQuick
import WinShun

Item {
    id: frame
    required property Window hostWindow
    property string buttonPrefix: "window"
    property bool chinese: true
    readonly property int titleHeight: 36
    readonly property bool maximized: hostWindow.visibility === Window.Maximized
    anchors.fill: parent
    z: 100
    Rectangle {
        anchors.fill: parent; color: "transparent"; radius: frame.maximized ? 0 : 8
        border.width: frame.maximized ? 0 : 1; border.color: Theme.cardBorder
    }
    Item {
        width: parent.width; height: frame.titleHeight
        Text {
            x: 18; anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, buttons.x - x - 12)
            text: frame.hostWindow.title; color: Theme.subtext
            font.pixelSize: 12; elide: Text.ElideRight
        }
        MouseArea {
            objectName: frame.buttonPrefix + "-title-drag"
            width: buttons.x; height: parent.height
            onPressed: frame.hostWindow.startSystemMove()
            onDoubleClicked: frame.maximized ? frame.hostWindow.showNormal() : frame.hostWindow.showMaximized()
        }
        Row {
            id: buttons
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            ToolButton {
                objectName: frame.buttonPrefix + "-minimize"
                tip: frame.chinese ? "最小化" : "Minimize"; tipEnabled: false
                onClicked: frame.hostWindow.showMinimized()
                Rectangle { anchors.centerIn: parent; width: 10; height: 1; color: parent.foreground }
            }
            ToolButton {
                objectName: frame.buttonPrefix + "-maximize"
                tip: frame.maximized ? (frame.chinese ? "还原" : "Restore") : (frame.chinese ? "最大化" : "Maximize")
                tipEnabled: false
                onClicked: frame.maximized ? frame.hostWindow.showNormal() : frame.hostWindow.showMaximized()
                Rectangle {
                    anchors.centerIn: parent; width: 10; height: 10; radius: 1
                    color: "transparent"; border.color: parent.foreground
                    Rectangle { visible: frame.maximized; x: -3; y: 3; width: 9; height: 9; radius: 1; color: Theme.page; border.color: parent.border.color }
                }
            }
            ToolButton {
                objectName: frame.buttonPrefix + "-close"; closeButton: true
                glyph: "\uE8BB"; tip: frame.chinese ? "关闭" : "Close"; tipEnabled: false
                onClicked: frame.hostWindow.close()
            }
        }
    }
    Repeater {
        model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
            Qt.LeftEdge | Qt.TopEdge, Qt.RightEdge | Qt.TopEdge,
            Qt.LeftEdge | Qt.BottomEdge, Qt.RightEdge | Qt.BottomEdge]
        MouseArea {
            required property int modelData
            readonly property bool edgeLeft: (modelData & Qt.LeftEdge) !== 0
            readonly property bool edgeRight: (modelData & Qt.RightEdge) !== 0
            readonly property bool edgeTop: (modelData & Qt.TopEdge) !== 0
            readonly property bool edgeBottom: (modelData & Qt.BottomEdge) !== 0
            readonly property bool corner: (edgeLeft || edgeRight) && (edgeTop || edgeBottom)
            objectName: frame.buttonPrefix + "-resize-" + modelData
            enabled: !frame.maximized && frame.hostWindow.visibility !== Window.FullScreen
            x: edgeLeft ? 0 : edgeRight ? frame.width - width : 8
            y: edgeTop ? 0 : edgeBottom ? frame.height - height : 8
            width: edgeLeft || edgeRight ? (corner ? 8 : 5) : frame.width - 16
            height: edgeTop || edgeBottom ? (corner ? 8 : 5) : frame.height - 16
            cursorShape: corner ? (edgeLeft === edgeTop ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: frame.hostWindow.startSystemResize(modelData)
        }
    }
}

import QtQuick
import WinShun

Item {
    id: row
    property string title
    property string description
    default property alias control: trailing.data
    implicitHeight: Math.max(labels.implicitHeight, trailing.implicitHeight) + 32
    width: parent.width
    Rectangle {
        visible: row.y > 0
        x: 1; width: parent.width - 2; height: 1
        color: Theme.cardBorder
    }
    Column {
        id: labels
        x: 18; anchors.verticalCenter: parent.verticalCenter
        width: Math.max(130, parent.width - trailing.width - 58)
        spacing: 4
        Text {
            text: row.title; color: Theme.text; font.pixelSize: Theme.fontBody
            width: parent.width; wrapMode: Text.Wrap
        }
        Text {
            text: row.description; visible: text.length > 0; color: Theme.subtext
            font.pixelSize: Theme.fontCaption
            width: parent.width; wrapMode: Text.Wrap
        }
    }
    Row {
        id: trailing
        anchors.right: parent.right; anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
    }
}

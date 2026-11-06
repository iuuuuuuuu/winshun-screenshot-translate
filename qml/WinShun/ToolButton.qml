import QtQuick
import QtQuick.Controls
import WinShun

FlatButton {
    id: button
    property string tip
    property bool tipEnabled: true
    property bool checked: false
    implicitWidth: text.length ? 74 : 32
    implicitHeight: 32
    flat: !checked
    color: checked ? Theme.track : pointer.hovered ? Theme.hover : "transparent"
    border.width: 0
    HoverHandler { id: pointer }
    ToolTip.visible: tipEnabled && enabled && pointer.hovered && tip.length > 0
    ToolTip.delay: 450
    ToolTip.text: tip
    Accessible.name: tip.length ? tip : text
}

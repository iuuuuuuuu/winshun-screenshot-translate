import QtQuick
import QtQuick.Controls
import WinShun

FlatButton {
    id: button
    property string tip
    property bool tipEnabled: true
    property bool checked: false
    property bool closeButton: false
    implicitWidth: text.length ? 74 : 32
    implicitHeight: 32
    flat: !checked
    color: closeButton && (pressed || hovered) ? (pressed ? "#B82028" : "#C42B1C")
        : pressed ? (Theme.dark ? "#4B4B4B" : "#D8D8D8") : hovered ? Theme.menuHover : checked ? Theme.track : "transparent"
    foreground: closeButton && (pressed || hovered) ? "#FFFFFF" : Theme.text
    border.width: 0
    HoverHandler { id: pointer }
    ToolTip.visible: tipEnabled && enabled && pointer.hovered && tip.length > 0
    ToolTip.delay: 450
    ToolTip.text: tip
    Accessible.name: tip.length ? tip : text
}

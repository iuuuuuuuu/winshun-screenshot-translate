import QtQuick
import WinShun
Window {
    id: menu
    width: 188; height: entries.implicitHeight + 12
    visible: false
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Theme.card
    Item { anchors.fill: parent; focus: true; Keys.onEscapePressed: menu.hide() }
    property bool focusedOnce: false
    onVisibleChanged: if(visible) focusedOnce=false
    onActiveChanged: {if(active) focusedOnce=true; else if(focusedOnce) hide()}
    Rectangle { anchors.fill: parent; color: "transparent"; border.width: 1; border.color: Theme.cardBorder; radius: Theme.cardRadius }
    function perform(name, value) { hide(); backend.action(name,value) }
    Column {
        id: entries; x: 6; y: 6; width: parent.width-12; spacing: 2
        FlatButton { width: parent.width; flat: true; text: backend.state.chinese ? "复制译文" : "Copy translation"; onClicked: menu.perform("copy",false) }
        FlatButton { width: parent.width; flat: true; text: backend.state.chinese ? "复制原文" : "Copy source"; onClicked: menu.perform("copy",true) }
        FlatButton { width: parent.width; flat: true; text: backend.state.chinese ? "上下对照" : "Compare"; onClicked: menu.perform("compare",null) }
        FlatButton { width: parent.width; flat: true; text: backend.state.chinese ? "完整译文" : "Full translation"; onClicked: menu.perform("text",null) }
        FlatButton { width: parent.width; flat: true; text: backend.state.chinese ? "保存图片" : "Save image"; onClicked: menu.perform("saveImage",null) }
        FlatButton { width: parent.width; flat: true; text: backend.state.chinese ? "重新截图" : "Capture again"; onClicked: menu.perform("retry",null) }
        FlatButton { width: parent.width; flat: true; text: backend.state.chinese ? "关闭" : "Close"; onClicked: menu.perform("close",null) }
    }
}

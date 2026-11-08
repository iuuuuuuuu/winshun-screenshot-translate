import QtQuick
import WinShun
AdaptiveWindow {
    id: textWindow
    preferredWidth: 46 + Math.sqrt(1.5 * measure.contentHeight * measure.width)
    contentHeight: textArea.contentHeight + 72
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.card
    Text {
        id: measure; visible: false; width: 278
        text: textArea.text.slice(0, 4096); textFormat: Text.PlainText
        font: textArea.font; wrapMode: Text.Wrap
    }
    Item { anchors.fill: parent; focus: true; Keys.onEscapePressed: backend.action("close", null) }
    Rectangle { anchors.fill: parent; color: "transparent"; border.width: 1; border.color: Theme.cardBorder }
    onClosing: event => {event.accepted=false; hide()}
    Text { id: heading; x: 18; y: 14; text: backend.state.chinese ? "完整译文" : "Translation"; font.pixelSize: 18; color: Theme.text }
    Text {
        id: serviceLabel
        objectName: "text-provider"
        z: 1
        anchors.left: heading.right; anchors.leftMargin: 16; anchors.verticalCenter: heading.verticalCenter
        text: backend.state.provider || ""; font.pixelSize: 12
        color: backend.state.providerFallback ? Theme.caution : Theme.subtext
        HoverHandler { id: serviceHover }
        ServiceTip {
            objectName: "provider-tip"
            anchorItem: serviceLabel; hovered: serviceHover.hovered
            text: backend.state.providerNote || ""
        }
    }
    ToolButton { anchors.right: parent.right; y: 8; glyph: "\uE8BB"; tipEnabled: false; onClicked: textWindow.hide() }
    MouseArea { x: 0; y: 0; width: parent.width-48; height: 44; onPressed: textWindow.startSystemMove() }
    Flickable {
        id: scroll
        objectName: "text-scroll"
        x: 18; y: 54; width: parent.width-36; height: parent.height-72
        contentWidth: width; contentHeight: textArea.height
        clip: true; boundsBehavior: Flickable.StopAtBounds
        TextEdit {
            id: textArea
            objectName: "text-content"
            width: scroll.width - 10; height: Math.max(scroll.height, contentHeight)
            text: backend.state.translatedText; readOnly: true; selectByMouse: true
            textFormat: TextEdit.PlainText
            color: Theme.text; font.family: "Segoe UI"; font.pixelSize: 16; wrapMode: TextEdit.Wrap
            selectionColor: Theme.textSelection
        }
    }
    ListScrollBar {
        objectName: "text-scrollbar"
        flickable: scroll; anchors.right: scroll.right; anchors.top: scroll.top; anchors.bottom: scroll.bottom
    }
}

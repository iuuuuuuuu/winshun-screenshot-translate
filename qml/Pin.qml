pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import WinShun

Window {
    id: pin
    width: backend.state.viewWidth || 320; height: backend.state.viewHeight || 120
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    color: Theme.card
    property bool compare: backend.state.comparison
    property bool translationFirst: backend.state.translationFirst
    property bool chinese: backend.state.chinese
    function t(zh, en) { return chinese ? zh : en }
    onClosing: event => { event.accepted = false; backend.action("close", null) }
    Item {
        anchors.fill: parent; focus: true
        Keys.onEscapePressed: backend.action("close", null)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) {
                backend.action("copy", !!(event.modifiers & Qt.ShiftModifier))
                event.accepted = true
            }
        }
        Flickable {
            id: scroll
            anchors.fill: parent; anchors.margins: 1
            contentWidth: backend.state.contentWidth || 1
            contentHeight: backend.state.contentHeight || 1
            clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Item {
                id: scene
                width: scroll.contentWidth; height: scroll.contentHeight
                Text {
                    x: 12; visible: pin.compare
                    y: pin.translationFirst ? (backend.state.resultHeight || 1) + 36 : 6
                    text: pin.t("原文", "Source"); color: Theme.subtext; font.pixelSize: 12
                }
                Image {
                    id: original
                    objectName: "source-image"
                    visible: pin.compare
                    x: 0; y: pin.translationFirst ? (backend.state.resultHeight || 1) + 60 : 30
                    width: backend.state.sourceWidth || 1; height: backend.state.sourceHeight || 1
                    source: "image://shot/source"; cache: false; smooth: true
                }
                Text {
                    visible: pin.compare
                    x: translated.x + 12
                    y: pin.translationFirst ? 6 : (backend.state.sourceHeight || 1) + 36
                    text: pin.t("译文", "Translation"); color: Theme.subtext; font.pixelSize: 12
                }
                Image {
                    id: translated
                    objectName: "translation-image"
                    x: 0
                    y: pin.compare ? (pin.translationFirst ? 30 : (backend.state.sourceHeight || 1) + 60) : 0
                    width: backend.state.resultWidth || 1; height: backend.state.resultHeight || 1
                    source: "image://shot/translated?" + (backend.state.version || 0)
                    cache: false; smooth: true
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property point last
                    onPressed: mouse => {
                        last = Qt.point(mouse.x, mouse.y)
                        if (mouse.button === Qt.RightButton) context.popup()
                    }
                    onPositionChanged: mouse => {
                        if (pressed && pressedButtons === Qt.LeftButton)
                            backend.action("move", [mouse.x - last.x, mouse.y - last.y])
                    }
                    onDoubleClicked: backend.action("close", null)
                    onWheel: wheel => {
                        if (wheel.modifiers & Qt.ControlModifier || !scroll.ScrollBar.vertical.visible) {
                            backend.action("opacity", wheel.angleDelta.y > 0 ? 1 : -1)
                            wheel.accepted = true
                        } else wheel.accepted = false
                    }
                }
            }
        }
        Rectangle {
            anchors.fill: parent; color: "transparent"; radius: 6
            border.width: 1; border.color: Theme.controlBorder
        }
        Rectangle {
            visible: backend.state.status.length > 0 && !backend.state.loading
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: message.implicitHeight + 20; color: Theme.card
            Text {
                id: message
                x: 12; y: 10; width: parent.width - 24
                text: backend.state.status; color: Theme.subtext; font.pixelSize: 14; wrapMode: Text.Wrap
            }
        }
        Rectangle {
            visible: backend.state.loading
            anchors.centerIn: parent
            width: busyRow.implicitWidth + 28; height: 42
            radius: 8; color: Theme.card; border.width: 1; border.color: Theme.controlBorder
            Row {
                id: busyRow
                anchors.centerIn: parent; spacing: 10
                Glyph {
                    id: spinner
                    glyph: "\uE72C"; size: 18; color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                    RotationAnimator on rotation {
                        from: 0; to: 360; duration: 900
                        loops: Animation.Infinite; running: backend.state.loading
                    }
                }
                Text {
                    text: backend.state.status; color: Theme.text; font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        Rectangle {
            visible: backend.state.fullText
            anchors.fill: parent; color: Theme.card
            Flickable {
                anchors.fill: parent; anchors.margins: 16
                contentWidth: width; contentHeight: full.implicitHeight
                clip: true
                ScrollBar.vertical: ScrollBar { }
                TextEdit {
                    id: full
                    width: parent.width; text: backend.state.translatedText
                    color: Theme.text; font.pixelSize: 16
                    wrapMode: TextEdit.Wrap; readOnly: true; selectByMouse: true
                    selectionColor: Theme.textSelection
                }
            }
        }
        Menu {
            id: context
            popupType: Popup.Window
            MenuItem { text: pin.t("复制译文", "Copy translation"); enabled: backend.state.translatedText.length > 0; onTriggered: backend.action("copy", false) }
            MenuItem { text: pin.t("复制原文", "Copy source"); enabled: backend.state.ready; onTriggered: backend.action("copy", true) }
            MenuItem { text: pin.t("重新翻译", "Translate again"); enabled: backend.state.ready && !backend.state.loading; onTriggered: backend.action("target", backend.state.target) }
            MenuItem { text: pin.t("重新截图", "Capture again"); onTriggered: backend.action("retry", null) }
            MenuItem { text: pin.t("完整译文", "Full translation"); enabled: backend.state.translatedText.length > 0; onTriggered: backend.action("text", null) }
            MenuSeparator { }
            MenuItem { text: pin.t("关闭", "Close"); onTriggered: backend.action("close", null) }
        }
    }
}

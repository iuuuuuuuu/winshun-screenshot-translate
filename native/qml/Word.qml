import QtQuick
import WinShun

Window {
    id: wordWindow
    readonly property bool editing: !!backend.state.editing
    width: 324
    height: 44 + Math.min(300, Math.max(62, body.implicitHeight)) + (editing ? 72 : 30)
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | (editing ? 0 : Qt.WindowDoesNotAcceptFocus)
    color: "transparent"
    function beginEditing() {
        editor.text = backend.state.sourceText || ""
        Qt.callLater(function() {
            if (!wordWindow.editing || !wordWindow.visible) return
            wordWindow.requestActivate()
            editor.forceActiveFocus(Qt.MouseFocusReason)
            editor.cursorPosition = editor.length
        })
    }
    function submitEditing() {
        if (editor.text.trim().length && !editor.inputMethodComposing)
            backend.action("submitEdit", editor.text)
    }
    onClosing: event => { event.accepted = false; backend.action("close", null) }
    Rectangle {
        anchors.fill: parent
        radius: Theme.largeCardRadius
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1
    }
    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: backend.action(wordWindow.editing ? "cancelEdit" : "close", null)
    }
    MouseArea {
        x: 1; y: 1; width: parent.width - 141; height: 40
        onPressed: { if (!backend.state.pinned) backend.action("pin", null); wordWindow.startSystemMove() }
    }
    Text {
        x: 14; y: 13
        text: backend.state.mode === "ocr" ? (backend.state.chinese ? "鼠标指词" : "Pointer word")
            : backend.state.mode === "jsel" ? (backend.state.chinese ? "划词翻译" : "Selection")
            : (backend.state.chinese ? "剪贴板翻译" : "Clipboard")
        color: Theme.subtext; font.pixelSize: 12
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 6; y: 5; spacing: 1
        ToolButton {
            objectName: "word-edit"
            glyph: "\uE70F"; checked: wordWindow.editing; enabled: !!backend.state.sourceText
            tip: backend.state.chinese ? "修改原文" : "Edit source"
            onClicked: backend.action(wordWindow.editing ? "cancelEdit" : "edit", null)
        }
        ToolButton {
            objectName: "word-pin"
            glyph: "\uE3E2"
            checked: backend.state.pinned
            tip: backend.state.chinese ? (checked ? "取消固定" : "固定并跟随新选区") : "Pin and follow selections"
            onClicked: backend.action("pin", null)
        }
        ToolButton {
            objectName: "word-copy"
            glyph: "\uE8C8"; enabled: !wordWindow.editing && !!backend.state.translatedText
            tip: backend.state.chinese ? "复制释义" : "Copy translation"
            onClicked: backend.action("copy", null)
        }
        ToolButton {
            objectName: "word-close"
            glyph: "\uE8BB"; tip: backend.state.chinese ? "关闭" : "Close"
            onClicked: backend.action("close", null)
        }
    }
    Flickable {
        id: scroll
        objectName: "word-scroll"
        x: 14; y: 44; width: parent.width - 28; height: parent.height - (wordWindow.editing ? 116 : 74)
        contentWidth: width; contentHeight: body.implicitHeight
        clip: true; boundsBehavior: Flickable.StopAtBounds
        onContentHeightChanged: if (!wordWindow.editing) contentY = 0
        Column {
            id: body
            width: scroll.width; spacing: 10
            TextEdit {
                objectName: "word-source"
                width: parent.width; height: visible ? contentHeight : 0
                visible: !wordWindow.editing && !!backend.state.sourceText
                text: backend.state.sourceText || ""
                textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                font.family: "Segoe UI"
                font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.text
                selectionColor: Theme.textSelection
            }
            Rectangle {
                width: parent.width; height: visible ? Math.max(68, editor.contentHeight + 20) : 0
                visible: wordWindow.editing
                radius: Theme.controlRadius; color: Theme.inputFocused
                border.width: 1; border.color: Theme.controlBorder
                TextEdit {
                    id: editor; objectName: "word-editor"
                    x: 10; y: 9; width: parent.width - 20; height: Math.max(48, contentHeight)
                    readOnly: !wordWindow.editing
                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                    selectByMouse: true; selectByKeyboard: true
                    font.family: "Segoe UI"; font.pixelSize: 15; color: Theme.text
                    selectionColor: Theme.textSelection; selectedTextColor: Theme.text
                    onCursorRectangleChanged: {
                        if (!wordWindow.editing) return
                        var at = mapToItem(body, cursorRectangle.x, cursorRectangle.y)
                        if (at.y < scroll.contentY) scroll.contentY = at.y
                        else if (at.y + cursorRectangle.height > scroll.contentY + scroll.height)
                            scroll.contentY = at.y + cursorRectangle.height - scroll.height
                    }
                    Keys.onEscapePressed: backend.action("cancelEdit", null)
                    Keys.onReturnPressed: event => {
                        if (event.modifiers & Qt.ShiftModifier || inputMethodComposing) event.accepted = false
                        else wordWindow.submitEditing()
                    }
                    Keys.onEnterPressed: event => {
                        if (event.modifiers & Qt.ShiftModifier || inputMethodComposing) event.accepted = false
                        else wordWindow.submitEditing()
                    }
                }
                Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 2; radius: 1; color: Theme.accent }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.divider; visible: !wordWindow.editing && !!backend.state.sourceText }
            TextEdit {
                objectName: "word-meaning"
                width: parent.width; height: visible ? contentHeight : 0
                visible: !wordWindow.editing && !!backend.state.translatedText
                text: backend.state.translatedText || ""
                textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                font.family: "Segoe UI"
                font.pixelSize: 15; color: Theme.text
                selectionColor: Theme.textSelection
            }
            Row {
                width: parent.width; spacing: 8
                visible: !wordWindow.editing && (backend.state.loading || !!backend.state.status)
                Canvas {
                    id: spinner
                    width: 14; height: 14; y: 2; visible: backend.state.loading
                    property real angle: 0
                    onPaint: {
                        var c = getContext("2d"); c.reset(); c.lineWidth = 1.5; c.lineCap = "round"
                        c.strokeStyle = Theme.track; c.beginPath(); c.arc(7,7,5,0,Math.PI*2); c.stroke()
                        c.strokeStyle = Theme.accent; c.beginPath(); c.arc(7,7,5,angle,angle+Math.PI*1.4); c.stroke()
                    }
                    onAngleChanged: requestPaint()
                    NumberAnimation on angle { from: 0; to: Math.PI*2; duration: 1000; loops: Animation.Infinite; running: spinner.visible && wordWindow.visible }
                }
                Text {
                    width: parent.width - (spinner.visible ? 22 : 0)
                    text: backend.state.status || ""
                    wrapMode: Text.Wrap; font.pixelSize: 12
                    color: backend.state.loading ? Theme.subtext : Theme.danger
                }
            }
            FlatButton {
                visible: !wordWindow.editing && !backend.state.loading && !!backend.state.status
                text: backend.state.chinese ? "重试" : "Retry"
                height: 28
                onClicked: backend.action("retry", null)
            }
        }
        ListScrollBar { flickable: scroll; parent: scroll.parent; anchors.right: scroll.right; anchors.top: scroll.top; anchors.bottom: scroll.bottom }
    }
    Row {
        x: 14; y: parent.height - 65; spacing: 8; visible: wordWindow.editing
        FlatButton {
            objectName: "word-submit-edit"; height: 30; highlighted: true
            enabled: editor.text.trim().length > 0 && !editor.inputMethodComposing
            text: backend.state.chinese ? "重新翻译" : "Translate"
            onClicked: wordWindow.submitEditing()
        }
        FlatButton {
            objectName: "word-cancel-edit"; height: 30
            text: backend.state.chinese ? "取消" : "Cancel"
            onClicked: backend.action("cancelEdit", null)
        }
    }
    Text {
        x: 14; anchors.bottom: parent.bottom; anchors.bottomMargin: 9; width: parent.width - 28
        text: wordWindow.editing ? (backend.state.chinese ? "Enter 重新翻译 · Shift+Enter 换行" : "Enter to translate · Shift+Enter for a new line")
            : backend.state.pinned ? (backend.state.chinese ? "已固定 · 选中新文字自动更新" : "Pinned · follows new selections")
            : (backend.state.provider || (backend.state.chinese ? "点击外部关闭 · 可固定" : "Click outside to close"))
        font.pixelSize: 10; color: Theme.faint; elide: Text.ElideRight
    }
}

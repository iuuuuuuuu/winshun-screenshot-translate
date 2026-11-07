import QtQuick
import WinShun

Window {
    id: wordWindow
    readonly property bool editing: !!backend.state.editing
    readonly property string sourceText: backend.state.sourceText || ""
    readonly property bool sourceDirty: editor.text !== sourceText
    property bool syncingSource: false
    property bool acceptsInput: false
    property bool resetting: false
    width: 324
    height: 44 + Math.min(300, Math.max(62, body.implicitHeight)) + 48
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | (acceptsInput ? 0 : Qt.WindowDoesNotAcceptFocus)
    // Use an opaque backing store for the whole card so native moves and caret
    // updates cannot leave its unchanged controls in a translucent dirty region.
    color: Theme.card
    function syncSource() {
        if (!editor || editing) return
        syncingSource = true
        if (editor.text !== sourceText) editor.text = sourceText
        syncingSource = false
    }
    function resetInteraction() {
        resetting = true
        blurSubmit.stop()
        acceptsInput = false
        editor.focus = false
        syncSource()
        scroll.contentY = 0
        resetting = false
    }
    function focusEditor() {
        acceptsInput = true
        Qt.callLater(function() {
            if (!wordWindow.acceptsInput || !wordWindow.visible) return
            wordWindow.requestActivate()
            editor.forceActiveFocus(Qt.MouseFocusReason)
        })
    }
    function submitEditing() {
        blurSubmit.stop()
        if (sourceDirty && editor.text.trim().length && !editor.inputMethodComposing)
            backend.action("submitEdit", editor.text)
    }
    function scheduleBlur() {
        if (visible && !resetting && sourceDirty) blurSubmit.restart()
    }
    function blurEditor() { focusSink.forceActiveFocus(Qt.MouseFocusReason) }
    function restoreSource() { blurSubmit.stop(); backend.action("cancelEdit", null) }
    function updateDraftState() {
        if (!syncingSource && visible)
            backend.action(editor.text !== sourceText || editor.inputMethodComposing ? "edit" : "cancelEdit", null)
    }
    onSourceTextChanged: syncSource()
    onEditingChanged: if (!editing) syncSource()
    onVisibleChanged: if (!visible) resetInteraction()
    onActiveChanged: if (!active) scheduleBlur()
    Component.onCompleted: syncSource()
    onClosing: event => { event.accepted = false; backend.action("close", null) }
    Timer {
        id: blurSubmit
        interval: 150
        onTriggered: {
            if (wordWindow.visible && !wordWindow.resetting && (!editor.activeFocus || !wordWindow.active))
                wordWindow.submitEditing()
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: Theme.largeCardRadius
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1
    }
    Item {
        id: focusSink
        anchors.fill: parent
        focus: true
        MouseArea { anchors.fill: parent; onPressed: wordWindow.blurEditor() }
        Keys.onEscapePressed: backend.action(wordWindow.editing ? "cancelEdit" : "close", null)
    }
    MouseArea {
        x: 1; y: 1; width: parent.width - 107; height: 40
        onPressed: { wordWindow.blurEditor(); if (!backend.state.pinned) backend.action("pin", null); wordWindow.startSystemMove() }
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
            objectName: "word-pin"
            glyph: "\uE3E2"
            checked: backend.state.pinned
            tip: backend.state.chinese ? (checked ? "取消固定" : "固定并跟随新选区") : "Pin and follow selections"
            onClicked: { wordWindow.blurEditor(); backend.action("pin", null) }
        }
        ToolButton {
            objectName: "word-copy"
            glyph: "\uE8C8"; enabled: !!backend.state.translatedText
            tip: backend.state.chinese ? "复制释义" : "Copy translation"
            onClicked: { wordWindow.blurEditor(); backend.action("copy", null) }
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
        x: 14; y: 44; width: parent.width - 28; height: parent.height - 92
        contentWidth: width; contentHeight: body.implicitHeight
        clip: true; boundsBehavior: Flickable.StopAtBounds
        onContentHeightChanged: if (!wordWindow.editing) contentY = 0
        Column {
            id: body
            width: scroll.width; spacing: 10
            Item {
                id: sourceArea
                width: parent.width; height: editor.height + 10
                HoverHandler { id: sourceHover }
                TextEdit {
                    id: editor; objectName: "word-editor"
                    width: parent.width - 30; height: Math.max(22, contentHeight)
                    readOnly: false
                    enabled: !!wordWindow.sourceText || !backend.state.loading
                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                    selectByMouse: true; selectByKeyboard: true
                    font.family: "Segoe UI"; font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.text
                    selectionColor: Theme.textSelection; selectedTextColor: Theme.text
                    onTextChanged: wordWindow.updateDraftState()
                    onInputMethodComposingChanged: {
                        wordWindow.updateDraftState()
                        if (!inputMethodComposing && !activeFocus) wordWindow.scheduleBlur()
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            blurSubmit.stop()
                            if (!wordWindow.acceptsInput) wordWindow.focusEditor()
                        } else wordWindow.scheduleBlur()
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onPressedChanged: if (pressed) wordWindow.focusEditor()
                    }
                    onCursorRectangleChanged: {
                        if (!activeFocus) return
                        var at = mapToItem(body, cursorRectangle.x, cursorRectangle.y)
                        if (at.y < scroll.contentY) scroll.contentY = at.y
                        else if (at.y + cursorRectangle.height > scroll.contentY + scroll.height)
                            scroll.contentY = at.y + cursorRectangle.height - scroll.height
                    }
                    Keys.onEscapePressed: backend.action(wordWindow.editing ? "cancelEdit" : "close", null)
                    Keys.onReturnPressed: event => {
                        if (event.modifiers & Qt.ShiftModifier || inputMethodComposing) event.accepted = false
                        else wordWindow.submitEditing()
                    }
                    Keys.onEnterPressed: event => {
                        if (event.modifiers & Qt.ShiftModifier || inputMethodComposing) event.accepted = false
                        else wordWindow.submitEditing()
                    }
                }
                Text {
                    width: parent.width - 30
                    visible: !editor.text && !editor.activeFocus
                    text: backend.state.chinese ? "原文 · 可直接编辑" : "Source · edit directly"
                    font.family: "Segoe UI"; font.pixelSize: 17; color: Theme.faint
                }
                ToolButton {
                    objectName: "word-restore"
                    anchors.right: parent.right; y: -2; width: 26; height: 26
                    glyph: "\uE7A7"
                    visible: wordWindow.sourceDirty
                    // Keep the hover hit area while hidden so entering the icon
                    // cannot hide it between parent/child hover notifications.
                    opacity: sourceHover.hovered || restoreHover.hovered ? (enabled ? 1 : 0.45) : 0
                    keyboardActivation: false
                    HoverHandler { id: restoreHover; blocking: false }
                    enabled: wordWindow.sourceDirty && !editor.inputMethodComposing
                    tip: backend.state.chinese ? "还原原文" : "Restore source"
                    onClicked: wordWindow.restoreSource()
                }
            }
            TextEdit {
                objectName: "word-meaning"
                width: parent.width; height: visible ? contentHeight : 0
                visible: !!backend.state.translatedText
                text: backend.state.translatedText || ""
                textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                font.family: "Segoe UI"
                font.pixelSize: 15; color: Theme.text
                selectionColor: Theme.textSelection
            }
            Row {
                width: parent.width; spacing: 8
                visible: backend.state.loading || !!backend.state.status
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
                objectName: "word-retry"
                visible: !wordWindow.editing && !backend.state.loading && !!backend.state.status
                text: backend.state.chinese ? "重试" : "Retry"
                height: 28
                onClicked: backend.action("retry", null)
            }
        }
        ListScrollBar { flickable: scroll; parent: scroll.parent; anchors.right: scroll.right; anchors.top: scroll.top; anchors.bottom: scroll.bottom }
    }
    Text {
        id: serviceLabel
        objectName: "word-provider"
        x: 14; y: parent.height - 43; width: parent.width - 28
        height: 18; verticalAlignment: Text.AlignVCenter
        text: backend.state.provider || (backend.state.loading
            ? (backend.state.chinese ? "等待翻译渠道" : "Awaiting service")
            : (backend.state.chinese ? "暂无翻译结果" : "No translation"))
        textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: 11
        color: backend.state.provider ? (backend.state.providerFallback ? Theme.caution : Theme.subtext) : Theme.faint
        TapHandler { onTapped: wordWindow.blurEditor() }
        HoverHandler { id: serviceHover }
        ServiceTip {
            objectName: "provider-tip"
            anchorItem: serviceLabel; hovered: serviceHover.hovered
            text: backend.state.providerNote || ""
        }
    }
    Text {
        x: 14; anchors.bottom: parent.bottom; anchors.bottomMargin: 9; width: parent.width - 28
        text: wordWindow.editing ? (backend.state.chinese ? "移开焦点自动翻译 · Enter 立即翻译" : "Leave source to translate · Enter to translate now")
            : backend.state.pinned ? (backend.state.chinese ? "已固定 · 选中新文字自动更新" : "Pinned · follows new selections")
            : (backend.state.chinese ? "原文可编辑 · 移开焦点自动翻译" : "Edit source · leave it to translate")
        font.pixelSize: 10; color: Theme.faint; elide: Text.ElideRight
    }
}

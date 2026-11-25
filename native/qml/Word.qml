import QtQuick
import WinShun

AdaptiveWindow {
    id: wordWindow
    property real cornerRadius: 12
    readonly property bool editing: !!backend.state.editing
    readonly property string sourceText: backend.state.sourceText || ""
    readonly property bool sourceDirty: editor.text !== sourceText
    readonly property bool animations: backend.state.animations !== false
    property bool sourceExpanded: false
    property bool syncingSource: false
    property bool acceptsInput: false
    property bool resetting: false
    readonly property real expandedSourceLimit: Math.max(60, Math.min(180, heightLimit * 0.32))
    preferredWidth: 28 + Math.sqrt(1.5 * (Math.min(sourceMeasure.contentHeight, 66) * sourceMeasure.width
                                       + meaningMeasure.contentHeight * meaningMeasure.width))
    contentHeight: sourceArea.height + body.implicitHeight + 120
    visible: false
    flags: Qt.Tool | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | (acceptsInput ? 0 : Qt.WindowDoesNotAcceptFocus)
    // Keep the opaque backing store; App applies the same rounded window mask.
    color: Theme.card
    Text {
        id: sourceMeasure; visible: false; width: 266
        text: editor.text.slice(0, 4096); textFormat: Text.PlainText
        font: editor.font; wrapMode: Text.Wrap
    }
    Text {
        id: meaningMeasure; visible: false; width: 286
        text: meaning.text.slice(0, 4096); textFormat: Text.PlainText
        font: meaning.font; wrapMode: Text.Wrap
    }
    function keepCursorVisible() {
        if (!editor.activeFocus || !sourceExpanded) return
        var at = editor.cursorRectangle
        var nextY = sourceScroll.contentY
        if (at.y < nextY) nextY = at.y
        else if (at.y + at.height > nextY + sourceScroll.height) nextY = at.y + at.height - sourceScroll.height
        sourceScroll.contentY = Math.max(0, Math.min(nextY, sourceScroll.contentHeight - sourceScroll.height))
    }
    function syncSource() {
        if (!editor || editing) return
        syncingSource = true
        if (editor.text !== sourceText) editor.text = sourceText
        syncingSource = false
    }
    function resetInteraction() {
        resetting = true
        blurSubmit.stop(); providerMenu.hide()
        acceptsInput = false; editor.focus = false; sourceExpanded = false
        syncSource(); scroll.contentY = 0; sourceScroll.contentY = 0
        resetting = false
    }
    function focusEditor() {
        providerMenu.hide()
        sourceExpanded = true; acceptsInput = true
        Qt.callLater(function() {
            if (!wordWindow.acceptsInput || !wordWindow.visible) return
            wordWindow.requestActivate(); editor.forceActiveFocus(Qt.MouseFocusReason)
            wordWindow.keepCursorVisible()
        })
    }
    function submitEditing() {
        blurSubmit.stop()
        if (sourceDirty && editor.text.trim().length && !editor.inputMethodComposing)
            backend.action("submitEdit", editor.text)
    }
    function collapseSource() {
        Qt.inputMethod.commit()
        Qt.callLater(function() {
            if (editor.inputMethodComposing) return
            wordWindow.submitEditing(); wordWindow.blurEditor()
            sourceExpanded = false; sourceScroll.contentY = 0
        })
    }
    function scheduleBlur() { if (visible && !resetting && sourceDirty) blurSubmit.restart() }
    function blurEditor() { focusSink.forceActiveFocus(Qt.MouseFocusReason) }
    function prepareMove() { providerMenu.hide(); blurEditor(); backend.action("move", null) }
    function restoreSource() { blurSubmit.stop(); backend.action("cancelEdit", null) }
    function dismissProviderMenu() { providerMenu.hide() }
    function chooseProvider(id) {
        blurSubmit.stop(); Qt.inputMethod.commit()
        Qt.callLater(function() {
            if (editor.inputMethodComposing) return
            backend.action("provider", {id: id, text: editor.text})
        })
    }
    function updateDraftState() {
        if (!syncingSource && visible)
            backend.action(editor.text !== sourceText || editor.inputMethodComposing ? "edit" : "cancelEdit", null)
    }
    onSourceTextChanged: syncSource()
    onEditingChanged: if (!editing) syncSource()
    onVisibleChanged: {
        opening.stop()
        if (!visible) { opacity = 1; resetInteraction() }
        else if (animations) opening.restart()
    }
    onActiveChanged: if (!active) scheduleBlur()
    Component.onCompleted: syncSource()
    onClosing: event => { event.accepted = false; backend.action("close", null) }
    NumberAnimation { id: opening; target: wordWindow; property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
    Timer {
        id: blurSubmit; interval: 150
        onTriggered: {
            if (wordWindow.visible && !wordWindow.resetting && (!editor.activeFocus || !wordWindow.active)) wordWindow.submitEditing()
        }
    }
    Rectangle { anchors.fill: parent; radius: wordWindow.cornerRadius; color: Theme.card; border.color: Theme.cardBorder; border.width: 1 }
    Item {
        id: focusSink; anchors.fill: parent; focus: true
        MouseArea { anchors.fill: parent; onPressed: { providerMenu.hide(); wordWindow.blurEditor() } }
        Keys.onEscapePressed: {
            if (providerMenu.visible) providerMenu.hide()
            else backend.action(wordWindow.editing ? "cancelEdit" : "close", null)
        }
    }
    MouseArea {
        x: 1; y: 1; width: parent.width - 107; height: 40
        onPressed: { wordWindow.prepareMove(); wordWindow.startSystemMove() }
    }
    Text {
        x: 16; y: 13
        text: backend.state.mode === "ocr" ? (backend.state.chinese ? "鼠标指词" : "Pointer word")
            : backend.state.mode === "jsel" ? (backend.state.chinese ? "划词翻译" : "Selection")
            : (backend.state.chinese ? "剪贴板翻译" : "Clipboard")
        color: Theme.subtext; font.pixelSize: 12
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 6; y: 5; spacing: 1
        ToolButton {
            objectName: "word-pin"; glyph: "\uE3E2"; checked: backend.state.pinned
            tip: backend.state.chinese ? (checked ? "取消固定" : "固定并跟随新选区") : "Pin and follow selections"
            onClicked: { wordWindow.blurEditor(); backend.action("pin", null) }
        }
        ToolButton {
            objectName: "word-copy"; glyph: "\uE8C8"; enabled: !!backend.state.translatedText
            tip: backend.state.chinese ? "复制译文" : "Copy translation"
            onClicked: { wordWindow.blurEditor(); backend.action("copy", null) }
        }
        ToolButton { objectName: "word-close"; glyph: "\uE8BB"; tip: backend.state.chinese ? "关闭" : "Close"; onClicked: backend.action("close", null) }
    }
    Item {
        id: sourceArea; objectName: "word-source-area"
        x: 16; y: 46; width: parent.width - 32
        height: wordWindow.sourceExpanded ? Math.min(wordWindow.expandedSourceLimit, Math.max(24, editor.contentHeight)) : Math.max(24, preview.implicitHeight)
        Behavior on height { enabled: wordWindow.animations && wordWindow.visible && !wordWindow.resetting; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        HoverHandler { id: sourceHover }
        Text {
            id: preview; objectName: "word-source-preview"
            width: parent.width - 32; visible: !wordWindow.sourceExpanded
            text: editor.text || (backend.state.chinese ? "原文 · 点击编辑" : "Source · click to edit")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
            font.family: "Segoe UI"; font.pixelSize: 14; color: Theme.subtext
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.IBeamCursor
                onClicked: wordWindow.focusEditor()
                Accessible.role: Accessible.Button
                Accessible.name: backend.state.chinese ? "展开并编辑原文" : "Expand and edit source"
            }
        }
        Flickable {
            id: sourceScroll; objectName: "word-source-scroll"
            width: parent.width - (wordWindow.sourceDirty ? 60 : 32); height: parent.height; visible: wordWindow.sourceExpanded
            contentWidth: width; contentHeight: editor.height; clip: true; boundsBehavior: Flickable.StopAtBounds
            onHeightChanged: Qt.callLater(wordWindow.keepCursorVisible)
            TextEdit {
                id: editor; objectName: "word-editor"
                width: sourceScroll.width - 8; height: Math.max(24, contentHeight)
                enabled: !!wordWindow.sourceText || !backend.state.loading
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; selectByMouse: true; selectByKeyboard: true
                font.family: "Segoe UI"; font.pixelSize: 14; color: Theme.subtext
                selectionColor: Theme.textSelection; selectedTextColor: Theme.text
                onTextChanged: wordWindow.updateDraftState()
                onInputMethodComposingChanged: { wordWindow.updateDraftState(); if (!inputMethodComposing && !activeFocus) wordWindow.scheduleBlur() }
                onActiveFocusChanged: {
                    if (activeFocus) { blurSubmit.stop(); if (!wordWindow.acceptsInput) wordWindow.focusEditor() }
                    else wordWindow.scheduleBlur()
                }
                TapHandler { acceptedButtons: Qt.LeftButton; onPressedChanged: if (pressed) wordWindow.focusEditor() }
                onCursorRectangleChanged: Qt.callLater(wordWindow.keepCursorVisible)
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
        }
        ListScrollBar { flickable: sourceScroll; visible: wordWindow.sourceExpanded && sourceScroll.contentHeight > sourceScroll.height; anchors.right: sourceScroll.right; anchors.top: sourceScroll.top; anchors.bottom: sourceScroll.bottom }
        // Outside both scroll views: always reachable, even at the end of long text.
        ToolButton {
            objectName: "word-source-toggle"; anchors.right: parent.right; y: -3; width: 26; height: 26
            glyph: wordWindow.sourceExpanded ? "\uE70E" : "\uE70D"
            tip: backend.state.chinese ? (wordWindow.sourceExpanded ? "收起原文" : "展开并编辑原文") : (wordWindow.sourceExpanded ? "Collapse source" : "Expand and edit source")
            onClicked: wordWindow.sourceExpanded ? wordWindow.collapseSource() : wordWindow.focusEditor()
        }
        ToolButton {
            objectName: "word-restore"; anchors.right: parent.right; anchors.rightMargin: 28; y: -3; width: 26; height: 26
            glyph: "\uE7A7"; visible: wordWindow.sourceExpanded && wordWindow.sourceDirty
            opacity: sourceHover.hovered || restoreHover.hovered ? (enabled ? 1 : 0.45) : 0
            keyboardActivation: false; HoverHandler { id: restoreHover; blocking: false }
            enabled: wordWindow.sourceDirty && !editor.inputMethodComposing
            tip: backend.state.chinese ? "还原原文" : "Restore source"
            onClicked: wordWindow.restoreSource()
        }
    }
    Rectangle { x: 16; y: sourceArea.y + sourceArea.height + 10; width: parent.width - 32; height: 1; color: Theme.divider }
    Flickable {
        id: scroll; objectName: "word-scroll"
        x: 16; y: sourceArea.y + sourceArea.height + 23; width: parent.width - 32; height: Math.max(28, parent.height - y - 51)
        contentWidth: width; contentHeight: body.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        onContentHeightChanged: if (!wordWindow.editing) contentY = 0
        Column {
            id: body; width: scroll.width; spacing: 10
            TextEdit {
                id: meaning; objectName: "word-meaning"
                width: parent.width - 8; height: visible ? contentHeight : 0; visible: !!backend.state.translatedText
                text: backend.state.translatedText || ""; textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                font.family: "Segoe UI"; font.pixelSize: 16; color: Theme.text; selectionColor: Theme.textSelection
                onTextChanged: { reveal.stop(); if (text && wordWindow.animations && wordWindow.visible) reveal.restart(); else opacity = 1 }
                NumberAnimation { id: reveal; target: meaning; property: "opacity"; from: 0.35; to: 1; duration: 120; easing.type: Easing.OutCubic }
            }
            Row {
                width: parent.width; spacing: 8; visible: backend.state.loading || !!backend.state.status
                Canvas {
                    id: spinner; width: 14; height: 14; y: 2; visible: backend.state.loading
                    property real angle: 0
                    onPaint: {
                        var c = getContext("2d"); c.reset(); c.lineWidth = 1.5; c.lineCap = "round"
                        c.strokeStyle = Theme.track; c.beginPath(); c.arc(7,7,5,0,Math.PI*2); c.stroke()
                        c.strokeStyle = Theme.accent; c.beginPath(); c.arc(7,7,5,angle,angle+Math.PI*1.4); c.stroke()
                    }
                    onAngleChanged: requestPaint()
                    NumberAnimation on angle { from: 0; to: Math.PI*2; duration: 1000; loops: Animation.Infinite; running: spinner.visible && wordWindow.visible }
                }
                Text { width: parent.width - (spinner.visible ? 22 : 0); text: backend.state.status || ""; wrapMode: Text.Wrap; font.pixelSize: 12; color: backend.state.loading ? Theme.subtext : Theme.danger }
            }
            FlatButton {
                objectName: "word-retry"; visible: !wordWindow.editing && !backend.state.loading && !!backend.state.status
                text: backend.state.chinese ? "重试" : "Retry"; height: 28; onClicked: backend.action("retry", null)
            }
        }
    }
    ListScrollBar { objectName: "word-scrollbar"; flickable: scroll; anchors.right: scroll.right; anchors.top: scroll.top; anchors.bottom: scroll.bottom }
    Rectangle { x: 1; y: parent.height - 43; width: parent.width - 2; height: 1; color: Theme.divider }
    Rectangle {
        id: providerButton; objectName: "word-provider-button"
        x: 12; y: parent.height - 36; width: parent.width - 24; height: 28; radius: 7
        color: providerHover.hovered ? Theme.controlHover : "transparent"
        border.width: providerHover.hovered || providerMenu.visible ? 1 : 0; border.color: Theme.controlBorder
        Text {
            id: serviceLabel; objectName: "word-provider"
            x: 8; width: parent.width - 34; height: parent.height; verticalAlignment: Text.AlignVCenter
            text: backend.state.provider || backend.state.providerChoiceLabel || (backend.state.chinese ? "选择翻译渠道" : "Choose service")
            textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 12
            color: backend.state.providerFallback ? Theme.caution : Theme.subtext
            HoverHandler { id: serviceHover }
        }
        Glyph { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; glyph: providerMenu.visible ? "\uE70E" : "\uE70D"; size: 10; color: Theme.subtext }
        HoverHandler { id: providerHover }
        TapHandler { onTapped: { wordWindow.blurEditor(); providerMenu.visible ? providerMenu.hide() : providerMenu.open() } }
        ServiceTip { objectName: "provider-tip"; anchorItem: serviceLabel; hovered: serviceHover.hovered && !providerMenu.visible; text: backend.state.providerNote || "" }
    }
    ProviderMenu {
        id: providerMenu; objectName: "word-provider-menu"; hostWindow: wordWindow; anchorItem: providerButton
        onChosen: id => wordWindow.chooseProvider(id)
    }
}

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
    property bool mirroringSelection: false
    property string selectionSide: ""
    property string pendingSelectionSide: ""
    readonly property bool switchingProvider: !!backend.state.providerSwitching
    readonly property bool switchError: !!backend.state.providerSwitchError
    readonly property real footerHeight: switchingProvider || switchError ? 97 : 77
    readonly property real expandedSourceLimit: Math.max(60, Math.min(180, heightLimit * 0.32))
    preferredWidth: 28 + Math.sqrt(1.5 * (Math.min(sourceMeasure.contentHeight, 66) * sourceMeasure.width
                                       + meaningMeasure.contentHeight * meaningMeasure.width))
    contentHeight: sourceArea.height + body.implicitHeight + 77 + footerHeight
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
    function scrollToSelection(item, flick, start, end) {
        var first = item.positionToRectangle(start), last = item.positionToRectangle(end)
        var top = first.y, bottom = last.y + last.height
        if (top < flick.contentY || bottom > flick.contentY + flick.height)
            flick.contentY = Math.max(0, Math.min(top - 8, flick.contentHeight - flick.height))
    }
    function matchingRows() {
        var base = backend.state.alignmentSource || "", current = editor.text
        var rows = backend.state.alignmentRows || []
        if (base === current) return rows
        var prefix = 0, suffix = 0, limit = Math.min(base.length, current.length)
        while (prefix < limit && base[prefix] === current[prefix]) ++prefix
        while (suffix < limit - prefix && base[base.length - suffix - 1] === current[current.length - suffix - 1]) ++suffix
        var mapped = []
        for (var i = 0; i < rows.length; ++i) {
            var r = rows[i], shift = 0
            if (base !== current && r.sourceEnd > prefix) {
                if (r.sourceStart < base.length - suffix) continue
                shift = current.length - base.length
            }
            mapped.push({sourceStart: r.sourceStart + shift, sourceEnd: r.sourceEnd + shift,
                         targetStart: r.targetStart, targetEnd: r.targetEnd})
        }
        return mapped
    }
    function mirrorSelection(side) {
        if (mirroringSelection || syncingSource || resetting) return
        var origin = side === "source" ? editor : meaning
        if (!origin.activeFocus) return
        selectionSide = side
        var rows = matchingRows(), first = -1, last = -1
        for (var i = 0; i < rows.length; ++i) {
            var r = rows[i], a = side === "source" ? r.sourceStart : r.targetStart
            var b = side === "source" ? r.sourceEnd : r.targetEnd
            if (a >= origin.selectionEnd) break
            if (origin.selectionStart < b && origin.selectionEnd > a) {
                if (first < 0) first = side === "source" ? r.targetStart : r.sourceStart
                last = side === "source" ? r.targetEnd : r.sourceEnd
            }
        }
        mirroringSelection = true
        var peer = side === "source" ? meaning : editor
        if (first >= 0) {
            if (side === "target") sourceExpanded = true
            peer.select(first, last)
            scrollToSelection(peer, side === "source" ? scroll : sourceScroll, first, last)
        } else peer.deselect()
        mirroringSelection = false
    }
    function selectionChanged(side) {
        if (!mirroringSelection && (side === "source" ? editor : meaning).activeFocus) {
            pendingSelectionSide = side; Qt.callLater(flushSelection)
        }
    }
    function flushSelection() { mirrorSelection(pendingSelectionSide) }
    function focusMeaning() {
        acceptsInput = true; providerMenu.hide()
        wordWindow.requestActivate(); meaning.forceActiveFocus(Qt.MouseFocusReason)
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
        acceptsInput = false; editor.focus = false; sourceExpanded = false; selectionSide = ""
        mirroringSelection = true; editor.deselect(); meaning.deselect(); mirroringSelection = false
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
    function prepareClose() { opening.stop(); blurSubmit.stop(); providerMenu.hide(); resetting = true }
    function chooseProvider(id) {
        if (switchingProvider) return
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
    onClosing: event => { event.accepted = false; backend.action("close", true) }
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
        x: 1; y: 1; width: parent.width - 140; height: 40
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
        ToolButton { objectName: "word-favorite"; glyph: "\uE734"; tip: backend.state.chinese ? "收藏译文" : "Favorite translation"; enabled: !!backend.state.translatedText && !backend.state.loading; onClicked: backend.action("favorite",null) }
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
        ToolButton { objectName: "word-close"; closeButton: true; glyph: "\uE8BB"; tip: backend.state.chinese ? "关闭" : "Close"; onClicked: backend.action("close", true) }
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
                Accessible.name: backend.state.chinese ? (preview.truncated ? "展开并编辑原文" : "编辑原文") : (preview.truncated ? "Expand and edit source" : "Edit source")
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
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; selectByMouse: true; selectByKeyboard: true; persistentSelection: true
                font.family: "Segoe UI"; font.pixelSize: 14; color: Theme.subtext
                selectionColor: Theme.textSelection; selectedTextColor: Theme.text
                onTextChanged: wordWindow.updateDraftState()
                onSelectionStartChanged: wordWindow.selectionChanged("source")
                onSelectionEndChanged: wordWindow.selectionChanged("source")
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
            id: sourceToggle
            objectName: "word-source-toggle"; anchors.right: parent.right; y: -3; width: 26; height: 26
            visible: preview.truncated
            glyph: wordWindow.sourceExpanded ? "\uE70E" : "\uE70D"
            tip: backend.state.chinese ? (wordWindow.sourceExpanded ? "收起原文" : "展开并编辑原文") : (wordWindow.sourceExpanded ? "Collapse source" : "Expand and edit source")
            onClicked: wordWindow.sourceExpanded ? wordWindow.collapseSource() : wordWindow.focusEditor()
        }
        ToolButton {
            objectName: "word-restore"; anchors.right: parent.right; anchors.rightMargin: sourceToggle.visible ? 28 : 0; y: -3; width: 26; height: 26
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
        x: 16; y: sourceArea.y + sourceArea.height + 23; width: parent.width - 32; height: Math.max(28, parent.height - y - wordWindow.footerHeight - 8)
        contentWidth: width; contentHeight: body.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        onContentHeightChanged: if (!wordWindow.editing) contentY = 0
        Column {
            id: body; width: scroll.width; spacing: 10
            TextEdit {
                id: meaning; objectName: "word-meaning"
                width: parent.width - 8; height: visible ? contentHeight : 0; visible: !!backend.state.translatedText
                text: backend.state.translatedText || ""; textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true; selectByKeyboard: true; persistentSelection: true; wrapMode: TextEdit.Wrap
                TapHandler { acceptedButtons: Qt.LeftButton; onPressedChanged: if (pressed) wordWindow.focusMeaning() }
                onSelectionStartChanged: wordWindow.selectionChanged("target")
                onSelectionEndChanged: wordWindow.selectionChanged("target")
                font.family: "Segoe UI"; font.pixelSize: 16; color: Theme.text; selectionColor: Theme.textSelection; selectedTextColor: Theme.text
                onTextChanged: { reveal.stop(); if (text && !backend.state.loading && wordWindow.animations && wordWindow.visible) reveal.restart(); else opacity = 1 }
                NumberAnimation { id: reveal; target: meaning; property: "opacity"; from: 0.35; to: 1; duration: 120; easing.type: Easing.OutCubic }
            }
            Row {
                width: parent.width; spacing: 8; visible: !wordWindow.switchingProvider && !wordWindow.switchError && (backend.state.loading || !!backend.state.status)
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
                Text { width: parent.width - (spinner.visible ? 22 : 0); text: backend.state.status || ""; wrapMode: Text.Wrap; font.pixelSize: 12; color: backend.state.loading || backend.state.awaitingSelectionCopy ? Theme.subtext : Theme.danger }
            }
            FlatButton {
                objectName: "word-retry"; visible: !wordWindow.editing && !backend.state.loading && !!backend.state.status
                text: backend.state.chinese ? "重试" : "Retry"; height: 28; onClicked: backend.action("retry", null)
            }
            Text { width: parent.width; visible: !!backend.state.notice; text: backend.state.notice || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.subtext; font.pixelSize: 12 }
        }
    }
    ListScrollBar { objectName: "word-scrollbar"; flickable: scroll; anchors.right: scroll.right; anchors.top: scroll.top; anchors.bottom: scroll.bottom }
    Rectangle { x: 1; y: parent.height - wordWindow.footerHeight; width: parent.width - 2; height: 1; color: Theme.divider }
    Rectangle {
        id: providerButton; objectName: "word-provider-button"
        x: 12; y: parent.height - wordWindow.footerHeight + 7; width: parent.width - 24; height: wordWindow.footerHeight - 49; radius: 7
        color: providerHover.hovered ? Theme.controlHover : "transparent"
        border.width: providerHover.hovered || providerMenu.visible ? 1 : 0; border.color: Theme.controlBorder
        Text {
            id: serviceLabel; objectName: "word-provider"
            x: 8; width: parent.width - 34; height: 28; verticalAlignment: Text.AlignVCenter
            text: wordWindow.switchingProvider ? (backend.state.chinese ? "正在切换 · " : "Switching · ") + backend.state.providerChoiceLabel
                : wordWindow.switchError ? (backend.state.chinese ? "切换失败 · " : "Switch failed · ") + backend.state.providerChoiceLabel
                : backend.state.provider || backend.state.providerChoiceLabel || (backend.state.chinese ? "选择翻译渠道" : "Choose service")
            textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 12
            color: wordWindow.switchError ? Theme.danger : Theme.subtext
            HoverHandler { id: serviceHover }
        }
        Text { objectName: "word-provider-progress"; x: 8; y: 27; width: parent.width - 16; height: 18; visible: wordWindow.switchingProvider || wordWindow.switchError
            text: backend.state.status || ""; font.pixelSize: 11; elide: Text.ElideRight; color: wordWindow.switchError ? Theme.danger : Theme.subtext }
        Glyph { anchors.right: parent.right; anchors.rightMargin: 8; y: 9; visible: !wordWindow.switchingProvider; glyph: providerMenu.visible ? "\uE70E" : "\uE70D"; size: 10; color: Theme.subtext }
        Canvas {
            id: providerSpinner; objectName: "word-provider-spinner"; width: 16; height: 16; anchors.right: parent.right; anchors.rightMargin: 8; y: 6
            visible: wordWindow.switchingProvider; property real angle: 0
            onPaint: { var c = getContext("2d"); c.reset(); c.lineWidth = 1.6; c.strokeStyle = Theme.accent; c.beginPath(); c.arc(8,8,5,angle,angle+Math.PI*1.4); c.stroke() }
            onAngleChanged: requestPaint()
            NumberAnimation on angle { from: 0; to: Math.PI*2; duration: 900; loops: Animation.Infinite; running: providerSpinner.visible && wordWindow.visible }
        }
        HoverHandler { id: providerHover }
        TapHandler { enabled: !wordWindow.switchingProvider; onTapped: { wordWindow.blurEditor(); providerMenu.visible ? providerMenu.hide() : providerMenu.open() } }
        ServiceTip { objectName: "provider-tip"; anchorItem: serviceLabel; hovered: serviceHover.hovered && !providerMenu.visible; text: backend.state.providerNote || "" }
    }
    Row {
        x: 12; y: parent.height - 36; spacing: 5
        ToolButton { objectName: "word-history"; textPixelSize: 12; glyphPixelSize: 12; glyph: "\uE81C"; tip: backend.state.chinese ? "历史与收藏" : "History"; onClicked: backend.action("history",null) }
        ToolButton { objectName: "word-speak-source"; textPixelSize: 12; glyphPixelSize: 12; text: backend.state.chinese ? "原文" : "Source"; glyph: backend.state.speaking ? "\uE71A" : "\uE767"; tip: backend.state.chinese ? (backend.state.speaking ? "停止朗读" : "朗读原文") : "Read original / stop"; enabled: !!backend.state.sourceText; onClicked: backend.action(backend.state.speaking ? "stopSpeech" : "speech",true) }
        ToolButton { objectName: "word-speak-target"; textPixelSize: 12; glyphPixelSize: 12; text: backend.state.chinese ? "译文" : "Target"; glyph: backend.state.speaking ? "\uE71A" : "\uE767"; tip: backend.state.chinese ? (backend.state.speaking ? "停止朗读" : "朗读译文") : "Read translation / stop"; enabled: !!backend.state.translatedText && !backend.state.loading; onClicked: backend.action(backend.state.speaking ? "stopSpeech" : "speech",false) }
    }
    ProviderMenu {
        id: providerMenu; objectName: "word-provider-menu"; hostWindow: wordWindow; anchorItem: providerButton
        onChosen: id => wordWindow.chooseProvider(id)
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import WinShun

Window {
    id: panel
    objectName: "translation-panel"
    visible: false; width: 960; height: 760
    minimumWidth: 600; minimumHeight: 520
    title: t("翻译面板 · Win顺", "Translation · WinShun")
    color: Theme.page
    property bool chinese: backend.state.chinese
    property bool automatic: backend.state.autoTranslate !== false
    property bool syncing: false
    function t(zh,en) { return chinese ? zh : en }
    function label(code) { var i = (backend.state.codes || []).indexOf(code); return i < 0 ? code : backend.state.sourceLabels[i] }
    function schedule() { debounce.stop(); if(automatic && input.text.trim().length && !input.inputMethodComposing && input.text.length <= 10000) debounce.restart() }
    function submit() { debounce.stop(); if(!input.inputMethodComposing) backend.action("translate",null) }
    function sync() { if(input.text !== backend.state.sourceText) { syncing=true; input.text=backend.state.sourceText || ""; syncing=false; schedule() } }
    onClosing: event => { event.accepted=false; debounce.stop(); sourceMenu.hide(); targetMenu.hide(); backend.action("close",null) }
    onVisibleChanged: if(visible) { sync(); schedule() } else debounce.stop()
    Component.onCompleted: sync()
    Connections { target: backend; function onChanged() { panel.sync() } }
    Timer { id: debounce; interval: 700; onTriggered: panel.submit() }
    Shortcut { sequence: "Ctrl+Return"; enabled: panel.visible; onActivated: panel.submit() }
    Column {
        id: header; x: 22; y: 18; width: parent.width-44; spacing: 12
        Item {
            width: parent.width; height: 36
            Text { anchors.verticalCenter: parent.verticalCenter; text: panel.t("翻译与释义","Translate and understand"); color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
            Row {
                anchors.right: parent.right; spacing: 8
                FlatButton { objectName: "panel-auto"; text: panel.t("自动翻译","Automatic"); highlighted: panel.automatic; textPixelSize: 13; onClicked: { backend.action("autoTranslate",!panel.automatic); panel.schedule() } }
                FlatButton { objectName: "panel-explain"; text: panel.t("释义","Explanation"); highlighted: backend.state.explain; textPixelSize: 13; onClicked: { backend.action("explain",!backend.state.explain); panel.schedule() } }
                ToolButton { glyph: "\uE713"; tip: panel.t("渠道设置","Service settings"); onClicked: backend.action("settings",null) }
            }
        }
        Row {
            spacing: 10
            FlatButton { id: sourceButton; objectName: "panel-source"; text: panel.label(backend.state.source); trailingGlyph: "\uE70D"; textPixelSize: 13; onClicked: sourceMenu.open() }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "→"; color: Theme.subtext; font.pixelSize: 18 }
            FlatButton { id: targetButton; objectName: "panel-target"; text: backend.state.target === "auto" ? panel.t("中英互译","Chinese ↔ English") : panel.label(backend.state.target); trailingGlyph: "\uE70D"; textPixelSize: 13; onClicked: targetMenu.open() }
            Text { anchors.verticalCenter: parent.verticalCenter; text: backend.state.detected ? panel.t("识别：","Detected: ") + panel.label(backend.state.detected) : ""; color: Theme.subtext; font.pixelSize: 12 }
        }
    }
    LanguageMenu { id: sourceMenu; objectName: "panel-source-menu"; hostWindow: panel; anchorItem: sourceButton; labels: backend.state.sourceLabels || []; codes: backend.state.codes || []; current: backend.state.source; onChosen: code => { backend.action("source",code); panel.schedule() } }
    LanguageMenu { id: targetMenu; objectName: "panel-target-menu"; hostWindow: panel; anchorItem: targetButton; labels: [panel.t("中英互译","Chinese ↔ English")].concat((backend.state.sourceLabels || []).slice(1)); codes: backend.state.codes || []; current: backend.state.target; onChosen: code => { backend.action("target",code); panel.schedule() } }
    Item {
        id: comparison
        anchors.top: header.bottom; anchors.topMargin: 16
        anchors.left: parent.left; anchors.leftMargin: 22
        anchors.right: parent.right; anchors.rightMargin: 22
        height: Math.max(170, panel.height - header.height - 58 - (definitions.visible ? definitions.height + 12 : 0) - footer.height)
        Rectangle {
            id: sourceCard; width: (parent.width - 12)/2; height: parent.height
            color: Theme.card; radius: Theme.largeCardRadius; border.color: Theme.cardBorder
            Text { x: 16; y: 14; text: panel.t("原文","Original"); color: Theme.subtext; font.pixelSize: 12 }
            ToolButton { anchors.right: parent.right; y: 4; glyph: "\uE8BB"; tip: panel.t("清空","Clear"); enabled: input.text.length > 0; onClicked: input.text="" }
            Flickable {
                id: inputScroll; x: 16; y: 42; width: parent.width-32; height: parent.height-76
                contentWidth: width; contentHeight: Math.max(height,input.contentHeight); clip: true; boundsBehavior: Flickable.StopAtBounds
                TextEdit {
                    id: input; objectName: "panel-input"; width: inputScroll.width; height: Math.max(inputScroll.height,contentHeight)
                    color: Theme.text; selectionColor: Theme.textSelection; selectedTextColor: Theme.text
                    font.pixelSize: 17; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; selectByMouse: true; persistentSelection: true
                    onTextChanged: if(!panel.syncing) { backend.action("input",text); panel.schedule() }
                    onInputMethodComposingChanged: { if(inputMethodComposing) { debounce.stop(); backend.action("input",text) } else panel.schedule() }
                    onCursorRectangleChanged: {
                        if(cursorRectangle.y < inputScroll.contentY) inputScroll.contentY=cursorRectangle.y
                        else if(cursorRectangle.y + cursorRectangle.height > inputScroll.contentY + inputScroll.height) inputScroll.contentY=cursorRectangle.y + cursorRectangle.height - inputScroll.height
                    }
                    Text { visible: input.text.length === 0 && !input.inputMethodComposing; text: panel.t("输入或粘贴文字…","Type or paste text…"); color: Theme.faint; font.pixelSize: 17 }
                }
            }
            ListScrollBar { parent: sourceCard; anchors.top: inputScroll.top; anchors.bottom: inputScroll.bottom; anchors.right: parent.right; flickable: inputScroll }
            Text { x: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 12; text: input.text.length + " / 10,000"; color: input.text.length>10000 ? Theme.danger : Theme.faint; font.pixelSize: 11 }
        }
        Rectangle {
            id: resultCard; anchors.right: parent.right; width: sourceCard.width; height: parent.height
            color: Theme.card; radius: Theme.largeCardRadius; border.color: Theme.cardBorder
            Text { x: 16; y: 14; text: panel.t("译文","Translation"); color: Theme.subtext; font.pixelSize: 12 }
            ToolButton { anchors.right: parent.right; y: 4; glyph: "\uE8C8"; tip: panel.t("复制译文","Copy translation"); enabled: !!backend.state.translatedText; onClicked: backend.action("copy","translatedText") }
            Flickable {
                id: translationScroll; x: 16; y: 42; width: parent.width-32; height: parent.height-76
                contentWidth: width; contentHeight: Math.max(height,translated.contentHeight); clip: true; boundsBehavior: Flickable.StopAtBounds
                TextEdit { id: translated; objectName: "panel-translation"; width: parent.width; height: Math.max(translationScroll.height,contentHeight); readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; color: Theme.text; selectionColor: Theme.textSelection; selectedTextColor: Theme.text; font.pixelSize: 17; text: backend.state.translatedText || "" }
                Text { visible: !backend.state.translatedText; width: parent.width; wrapMode: Text.Wrap; color: backend.state.loading ? Theme.subtext : Theme.faint; font.pixelSize: 15; text: backend.state.loading ? panel.t("正在翻译…","Translating…") : panel.t("译文将在这里显示","Your translation appears here") }
            }
            ListScrollBar { parent: resultCard; anchors.top: translationScroll.top; anchors.bottom: translationScroll.bottom; anchors.right: parent.right; flickable: translationScroll }
            Text { id: providerLabel; x: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 12; width: parent.width-32; elide: Text.ElideRight; text: backend.state.provider || ""; color: backend.state.providerFallback ? Theme.caution : Theme.subtext; font.pixelSize: 11
                HoverHandler { id: providerHover }
                HoverTip { target: providerHover.hovered ? providerLabel : null; text: backend.state.providerNote || "" }
            }
        }
    }
    Rectangle {
        id: definitions; visible: backend.state.explain; x: 22; width: parent.width-44; height: Math.min(190,panel.height*.25)
        anchors.top: comparison.bottom; anchors.topMargin: 12
        color: Theme.card; radius: Theme.largeCardRadius; border.color: Theme.cardBorder
        Text { x: 16; y: 12; text: panel.t("释义与用法","Meaning and usage"); color: Theme.subtext; font.pixelSize: 12 }
        ToolButton { anchors.right: parent.right; y: 2; glyph: "\uE8C8"; tip: panel.t("复制释义","Copy explanation"); enabled: !!backend.state.definition; onClicked: backend.action("copy","definition") }
        Flickable {
            id: definitionScroll; x: 16; y: 40; width: parent.width-32; height: parent.height-64
            contentWidth: width; contentHeight: Math.max(height,definition.contentHeight); clip: true; boundsBehavior: Flickable.StopAtBounds
            TextEdit { id: definition; objectName: "panel-definition"; width: parent.width; height: Math.max(definitionScroll.height,contentHeight); readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; color: Theme.text; font.pixelSize: 15; text: backend.state.definition || backend.state.definitionStatus || panel.t("词语显示词义与用法；句子可由已配置的 AI 渠道解释。","Words show meanings and usage; configured AI services can explain sentences.") }
        }
        ListScrollBar { parent: definitions; anchors.top: definitionScroll.top; anchors.bottom: definitionScroll.bottom; anchors.right: parent.right; flickable: definitionScroll }
        Text { x: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 7; width: parent.width-32; elide: Text.ElideRight; text: backend.state.definitionProvider || ""; color: backend.state.definitionFallback ? Theme.caution : Theme.subtext; font.pixelSize: 11 }
    }
    Item {
        id: footer; x: 22; anchors.bottom: parent.bottom; anchors.bottomMargin: 14; width: parent.width-44; height: 38
        Text { anchors.verticalCenter: parent.verticalCenter; width: parent.width-translateButton.width-16; wrapMode: Text.Wrap; text: backend.state.status || (panel.automatic ? panel.t("停止输入后自动翻译 · Ctrl+Enter 立即翻译","Translates after typing · Ctrl+Enter to translate now") : panel.t("Ctrl+Enter 翻译","Ctrl+Enter to translate")); color: backend.state.status && !backend.state.loading ? Theme.danger : Theme.subtext; font.pixelSize: 12 }
        FlatButton { id: translateButton; objectName: "panel-submit"; anchors.right: parent.right; highlighted: true; text: backend.state.loading ? panel.t("重新翻译","Translate again") : panel.t("翻译","Translate"); enabled: input.text.trim().length > 0 && input.text.length <= 10000 && !input.inputMethodComposing; onClicked: panel.submit() }
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import WinShun
Window {
    id: window
    objectName: "translation-history"
    visible: false; width: 860; height: 680; minimumWidth: 600; minimumHeight: 440
    color: Theme.page; title: t("翻译历史与收藏", "Translation history")
    flags: Qt.Window | Qt.FramelessWindowHint
    property real cornerRadius: 8
    readonly property string windowSizeKey: "history"
    function t(zh,en) { return backend.state.chinese ? zh : en }
    property bool confirmClear: false
    Timer { id: searchDelay; interval: 180; onTriggered: backend.action("search",searchInput.text) }
    onClosing: event => { event.accepted=false; backend.action("close",null) }
    WindowFrame { id: frame; hostWindow: window; buttonPrefix: "history"; chinese: !!backend.state.chinese }
    Column {
        id: header; x: 20; y: frame.titleHeight + 10; width: parent.width-40; spacing: 10
        Row { spacing: 8
            FlatButton { objectName: "history-recording"; text: window.t("复制时记录", "Record on copy"); highlighted: !!backend.state.recording; onClicked: backend.action("recording",!backend.state.recording) }
            FlatButton { objectName: "history-filter"; text: window.t("只看收藏", "Favorites"); highlighted: !!backend.state.favoritesOnly; onClicked: backend.action("favoritesOnly",!backend.state.favoritesOnly) }
            FlatButton { objectName: "history-clear"; text: window.confirmClear ? window.t("确认清除普通历史", "Confirm clear") : window.t("清除普通历史", "Clear recent"); onClicked: { if(window.confirmClear) { backend.action("clear",null); window.confirmClear=false } else window.confirmClear=true } }
        }
        InputBox { id: searchInput; objectName: "history-search"; width: parent.width; placeholder: window.t("搜索原文或译文…", "Search original or translation…"); onTextChanged: searchDelay.restart() }
        Text { width: parent.width; wrapMode: Text.Wrap; text: window.t("仅主动复制或收藏时保存 · 普通历史最多 500 条 · 收藏保留", "Saved on copy or favorite · Up to 500 recent entries · Favorites are kept"); color: Theme.subtext; font.pixelSize: 12 }
    }
    ListView {
        id: list; objectName: "history-list"
        x: 20; anchors.top: header.bottom; anchors.topMargin: 12; width: parent.width-40; anchors.bottom: footer.top; anchors.bottomMargin: 12
        clip: true; spacing: 8; boundsBehavior: Flickable.StopAtBounds; model: backend.state.rows || []
        delegate: Rectangle {
            id: entry; required property var modelData; required property int index
            width: list.width; height: contents.height+24; color: Theme.card; radius: Theme.cardRadius; border.color: Theme.cardBorder
            Column {
                id: contents; x: 12; y: 12; width: parent.width-124; spacing: 7
                Text { width: parent.width; maximumLineCount: 2; elide: Text.ElideRight; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: entry.modelData.source; font.pixelSize: 14; color: Theme.subtext }
                Text { width: parent.width; maximumLineCount: 3; elide: Text.ElideRight; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: entry.modelData.translated; font.pixelSize: 16; color: Theme.text }
                Text { width: parent.width; elide: Text.ElideRight; text: entry.modelData.provider + " · " + Qt.formatDateTime(new Date(entry.modelData.time), "yyyy-MM-dd hh:mm"); color: Theme.faint; font.pixelSize: 11 }
            }
            Row { anchors.right: parent.right; anchors.rightMargin: 8; y: 8; spacing: 1
                ToolButton { objectName: "history-open-"+entry.index; glyph: "\uE8A7"; tip: window.t("打开全文", "Open full text"); onClicked: backend.action("open",entry.modelData.id) }
                ToolButton { objectName: "history-star-"+entry.index; glyph: "\uE734"; checked: entry.modelData.favorite; tip: entry.modelData.favorite ? window.t("取消收藏","Unfavorite") : window.t("收藏","Favorite"); onClicked: backend.action("favorite",{id:entry.modelData.id,enabled:!entry.modelData.favorite}) }
                ToolButton { objectName: "history-remove-"+entry.index; glyph: "\uE74D"; tip: window.t("删除记录", "Delete entry"); onClicked: backend.action("remove",entry.modelData.id) }
            }
        }
        Text { anchors.centerIn: parent; visible: list.count===0; text: window.t("还没有记录。复制译文或点击收藏后，会出现在这里。", "Copy or favorite a translation to see it here."); color: Theme.faint; width: parent.width-36; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
    }
    ListScrollBar { anchors.right: parent.right; anchors.top: list.top; anchors.bottom: list.bottom; flickable: list }
    Column {
        id: footer; x: 20; width: parent.width-40; anchors.bottom: parent.bottom; anchors.bottomMargin: 16; spacing: 8
        TextEdit { selectionColor: Theme.textSelection; selectedTextColor: Theme.text; objectName: "history-status"; width: parent.width; height: contentHeight; readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; font.pixelSize: 12; color: Theme.subtext; text: backend.state.status || "" }
        Row { spacing: 8
            FlatButton { objectName: "history-csv"; text: window.t("导出 CSV", "Export CSV"); enabled: list.count>0; onClicked: backend.action("export","csv") }
            FlatButton { objectName: "history-anki"; text: window.t("导出 Anki", "Export Anki"); enabled: list.count>0; onClicked: backend.action("export","anki") }
            FlatButton { visible: !!backend.state.exportFolder; text: window.t("打开导出文件夹", "Open export folder"); onClicked: backend.action("folder",null) }
            Text { anchors.verticalCenter: parent.verticalCenter; text: list.count+window.t(" 条", " entries"); color: Theme.faint; font.pixelSize: 12 }
        }
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import WinShun

Column {
    id: editor
    property bool chinese: backend.state.chinese
    property var channels: backend.state.aiChannels || []
    property int current: backend.state.aiCurrent || 0
    property var channel: channels.length ? channels[Math.min(current, channels.length - 1)] : ({})
    property var models: channel.models || []
    spacing: 12
    enabled: !backend.state.testing
    function t(zh,en) { return chinese ? zh : en }
    function field(key,value) { backend.action("aiField", {key:key,value:value}) }
    function modelField(index,key,value) { backend.action("aiModelField", {index:index,key:key,value:value}) }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: Theme.subtext; font.pixelSize: 13
        text: editor.t("每个渠道可填写多个模型。手动模式按渠道、模型从上到下尝试；自动权重模式优先使用近期表现较好的模型。失败记忆和恢复间隔沿用首页设置。", "Add multiple models per endpoint. Manual mode follows channel and model order; automatic mode uses recent performance. Recovery follows the shared settings.")
    }
    Repeater {
        model: editor.channels.length
        Rectangle {
            id: channelRow
            required property int index
            property var entry: editor.channels[index] || ({})
            width: editor.width; height: 46; radius: Theme.cardRadius
            color: editor.current === index ? Theme.chipActive : Theme.card
            border.color: editor.current === index ? Theme.accent : Theme.cardBorder
            Text { x: 12; anchors.verticalCenter: parent.verticalCenter; width: parent.width - channelActions.width - 24; elide: Text.ElideRight; text: (channelRow.index+1) + ". " + (channelRow.entry.name || editor.t("未命名渠道","Unnamed channel")); color: channelRow.entry.enabled === false ? Theme.faint : Theme.text; font.pixelSize: 14 }
            MouseArea { anchors.fill: parent; onClicked: backend.action("aiSelect",channelRow.index) }
            Row {
                id: channelActions; anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                Text { anchors.verticalCenter: parent.verticalCenter; text: channelRow.entry.enabled === false ? editor.t("已禁用","Disabled") : editor.t("已启用","Enabled"); color: channelRow.entry.enabled === false ? Theme.faint : Theme.accent; font.pixelSize: 12 }
                ToolButton { glyph: "\uE70E"; tip: editor.t("上移渠道","Move channel up"); enabled: channelRow.index > 0; onClicked: backend.action("aiMove",{from:channelRow.index,to:channelRow.index-1}) }
                ToolButton { glyph: "\uE70D"; tip: editor.t("下移渠道","Move channel down"); enabled: channelRow.index < editor.channels.length-1; onClicked: backend.action("aiMove",{from:channelRow.index,to:channelRow.index+1}) }
                ToolButton { objectName: "ai-remove-" + channelRow.index; glyph: "\uE8BB"; tip: editor.t("删除渠道（保存后生效）","Remove channel after saving"); onClicked: backend.action("aiRemove",channelRow.index) }
            }
        }
    }
    FlatButton { objectName: "ai-add"; text: editor.t("＋ 添加 AI 渠道","＋ Add AI channel"); onClicked: backend.action("aiAdd",null) }
    Rectangle {
        visible: editor.channels.length > 0
        width: parent.width; height: details.implicitHeight + 32; color: Theme.card; radius: Theme.cardRadius; border.color: Theme.cardBorder
        Column {
            id: details; x: 16; y: 16; width: parent.width - 32; spacing: 12
            Row {
                spacing: 12; width: parent.width
                InputBox { objectName: "ai-name"; width: parent.width - channelEnabled.width - 12; text: editor.channel.name || ""; placeholder: editor.t("渠道名称","Channel name"); onTextChanged: if(inputFocused) editor.field("name",text) }
                FlatButton { id: channelEnabled; objectName: "ai-enabled"; text: editor.channel.enabled === false ? editor.t("已禁用","Disabled") : editor.t("已启用","Enabled"); highlighted: editor.channel.enabled !== false; onClicked: editor.field("enabled",editor.channel.enabled === false) }
            }
            Text { text: editor.t("服务地址 · 兼容 OpenAI","Endpoint · OpenAI compatible"); color: Theme.subtext; font.pixelSize: 12 }
            InputBox { objectName: "ai-endpoint"; width: parent.width; text: editor.channel.endpoint || ""; placeholder: "https://api.example.com/v1"; onTextChanged: if(inputFocused) editor.field("endpoint",text) }
            Text { text: editor.t("API 密钥 · Windows 加密保存","API key · encrypted by Windows"); color: Theme.subtext; font.pixelSize: 12 }
            InputBox { objectName: "ai-key"; width: parent.width; text: editor.channel.apiKey || ""; echoMode: TextInput.Password; placeholder: editor.t("输入此渠道的 API 密钥","Enter channel API key"); onTextChanged: if(inputFocused) editor.field("apiKey",text) }
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            Text { text: editor.t("模型与降级顺序","Models and fallback order"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Repeater {
                model: editor.models.length
                Row {
                    id: modelRow
                    required property int index
                    property var entry: editor.models[index] || ({})
                    width: details.width; spacing: 6; height: 36
                    InputBox { objectName: "ai-model-"+modelRow.index; width: parent.width - modelEnabled.width - 114; text: modelRow.entry.name || ""; placeholder: editor.t("模型名 / 接入点 ID","Model / endpoint ID"); onTextChanged: if(inputFocused) editor.modelField(modelRow.index,"name",text) }
                    FlatButton { id: modelEnabled; objectName: "ai-model-enabled-"+modelRow.index; textPixelSize: 12; text: modelRow.entry.enabled === false ? editor.t("禁用","Off") : editor.t("启用","On"); highlighted: modelRow.entry.enabled !== false; onClicked: editor.modelField(modelRow.index,"enabled",modelRow.entry.enabled === false) }
                    ToolButton { glyph: "\uE70E"; tip: editor.t("上移模型","Move model up"); enabled: modelRow.index > 0; onClicked: backend.action("aiModelMove",{index:modelRow.index,to:modelRow.index-1}) }
                    ToolButton { glyph: "\uE70D"; tip: editor.t("下移模型","Move model down"); enabled: modelRow.index < editor.models.length-1; onClicked: backend.action("aiModelMove",{index:modelRow.index,to:modelRow.index+1}) }
                    ToolButton { glyph: "\uE8BB"; tip: editor.t("删除模型","Remove model"); onClicked: backend.action("aiModelRemove",{index:modelRow.index}) }
                }
            }
            FlatButton { objectName: "ai-model-add"; text: editor.t("＋ 添加模型","＋ Add model"); onClicked: backend.action("aiModelAdd",null) }
        }
    }
    Repeater {
        model: backend.state.aiTests || []
        Text {
            required property var modelData
            width: editor.width; wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 12
            color: modelData.status === "failed" ? Theme.danger : Theme.subtext
            text: modelData.name + " · " + modelData.detail + (modelData.elapsedMs !== undefined ? " · " + modelData.elapsedMs + " ms" : "")
        }
    }
}

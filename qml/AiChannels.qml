pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import WinShun

Column {
    id: editor
    property bool chinese: backend.state.chinese
    property var channels: backend.state.aiChannels || []
    property int current: backend.state.aiCurrent || 0
    property var channel: channels.length ? channels[Math.min(current, channels.length - 1)] : ({})
    property var models: channel.models || []
    property string searchText: ""
    property bool onlyNew: false
    property var catalog: backend.state.aiCatalog || []
    property var picked: backend.state.aiPicked || []
    property var filteredCatalog: catalog.filter(function(name) { return name.toLowerCase().indexOf(searchText.toLowerCase().trim()) >= 0 && (!onlyNew || !hasModel(name)) })
    function hasModel(name) { return models.some(function(m) { return (m.name || "").trim() === name }) }
    property string channelId: channel.id || ""
    onChannelIdChanged: searchText = ""
    Timer { interval: 5000; repeat: true; running: editor.visible; onTriggered: backend.action("aiRefreshHealth", null) }
    spacing: 12
    enabled: !backend.state.testing
    function t(zh,en) { return chinese ? zh : en }
    function field(key,value) { backend.action("aiField", {key:key,value:value}) }
    function modelField(index,key,value) { backend.action("aiModelField", {index:index,key:key,value:value}) }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: Theme.subtext; font.pixelSize: 13
        text: editor.t("可从服务获取模型并勾选添加。手动模式按顺序尝试；自动权重优先近期表现好的模型。故障按模型独立冷却，其他模型仍可使用。", "Fetch and select models from your service. Manual mode follows order; automatic mode uses recent performance. Each model cools down independently.")
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
            Row {
                spacing: 8
                FlatButton { objectName: "ai-streaming"; text: editor.t("流式显示","Streaming"); highlighted: editor.channel.streaming !== false; onClicked: editor.field("streaming",editor.channel.streaming === false) }
                FlatButton { objectName: "ai-thinking"; text: editor.t("优先关闭思考","Prefer no reasoning"); highlighted: editor.channel.disableThinking !== false; onClicked: editor.field("disableThinking",editor.channel.disableThinking === false) }
            }
            Text { width: parent.width; wrapMode: Text.Wrap; text: editor.t("译文边返回边显示。关闭思考仅用于已适配的服务；服务不支持时会记住兼容方式。","Show translation as it arrives. Reasoning controls apply to supported services; incompatible options are remembered."); color: Theme.faint; font.pixelSize: 12 }
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            Row {
                width: parent.width; height: 36; spacing: 8
                FlatButton { objectName: "ai-fetch-models"; text: backend.state.aiDiscovering ? editor.t("正在获取…","Fetching…") : editor.t("获取 /models","Fetch /models"); enabled: !backend.state.aiDiscovering; onClicked: backend.action("aiFetchModels", null) }
                FlatButton { visible: backend.state.aiDiscovering || false; text: editor.t("取消","Cancel"); onClicked: backend.action("aiCancelModels", null) }
            }
            Text { width: parent.width; visible: text.length > 0; text: backend.state.aiDiscoveryMessage || ""; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.subtext; font.pixelSize: 12 }
            Column {
                visible: editor.catalog.length > 0; width: parent.width; spacing: 8
                InputBox { objectName: "ai-model-search"; width: parent.width; text: editor.searchText; placeholder: editor.t("搜索模型名称…","Search model names…"); onTextChanged: if(inputFocused) editor.searchText = text }
                Row {
                    width: parent.width; height: 36; spacing: 8
                    FlatButton { objectName: "ai-model-filter"; text: editor.onlyNew ? editor.t("仅未添加","New models only") : editor.t("全部模型","All models"); highlighted: editor.onlyNew; onClicked: editor.onlyNew = !editor.onlyNew }
                    FlatButton { objectName: "ai-add-picked"; text: editor.t("添加勾选 (","Add selected (") + editor.picked.length + ")"; enabled: editor.picked.length > 0; onClicked: backend.action("aiAddPickedModels", null) }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: editor.filteredCatalog.length + editor.t(" 个匹配"," matches"); color: Theme.subtext; font.pixelSize: 12 }
                }
                Rectangle {
                    width: parent.width; height: Math.min(240, Math.max(42, editor.filteredCatalog.length * 38)); color: Theme.control; radius: Theme.controlRadius; border.color: Theme.controlBorder
                    ListView {
                        id: modelCatalog; objectName: "ai-model-catalog"; anchors.fill: parent; anchors.margins: 4; clip: true
                        model: editor.filteredCatalog; boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            id: catalogRow; required property string modelData
                            readonly property bool added: editor.hasModel(modelData)
                            readonly property bool checked: editor.picked.indexOf(modelData) >= 0
                            width: modelCatalog.width - 10; height: 38; radius: 4
                            color: catalogPointer.containsMouse ? Theme.controlHover : "transparent"
                            Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: catalogRow.added || catalogRow.checked ? "☑" : "☐"; color: catalogRow.added ? Theme.faint : Theme.accent; font.pixelSize: 18 }
                            Text { x: 36; width: parent.width - 42; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight; textFormat: Text.PlainText; text: catalogRow.modelData + (catalogRow.added ? editor.t(" · 已添加"," · added") : ""); color: catalogRow.added ? Theme.faint : Theme.text; font.pixelSize: 13 }
                            MouseArea { id: catalogPointer; objectName: "ai-pick-" + catalogRow.modelData; anchors.fill: parent; hoverEnabled: true; enabled: !catalogRow.added; cursorShape: Qt.PointingHandCursor; onClicked: backend.action("aiPickModel", catalogRow.modelData) }
                        }
                    }
                }
            }
            Text { text: editor.t("模型与降级顺序","Models and fallback order"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text { width: parent.width; wrapMode: Text.Wrap; text: editor.t("冷却单位为分钟，0 沿用全局设置。自定义冷却期间不自动探测；连续失败会延长等待，认证或额度错误可能等待更久。","Cooldown is in minutes; 0 uses shared settings. Custom cooldowns skip early probes. Repeated, authentication or quota errors may extend the wait."); color: Theme.faint; font.pixelSize: 12 }
            Repeater {
                model: editor.models.length
                Column {
                    id: modelRow
                    required property int index
                    property var entry: editor.models[index] || ({})
                    property var health: (backend.state.aiModelHealth || [])[index] || ({})
                    width: details.width; spacing: 6
                    Row {
                    width: parent.width; spacing: 6; height: 36
                    InputBox { objectName: "ai-model-"+modelRow.index; width: parent.width - modelEnabled.width - 114; text: modelRow.entry.name || ""; placeholder: editor.t("模型名 / 接入点 ID","Model / endpoint ID"); onTextChanged: if(inputFocused) editor.modelField(modelRow.index,"name",text) }
                    FlatButton { id: modelEnabled; objectName: "ai-model-enabled-"+modelRow.index; textPixelSize: 12; text: modelRow.entry.enabled === false ? editor.t("禁用","Off") : editor.t("启用","On"); highlighted: modelRow.entry.enabled !== false; onClicked: editor.modelField(modelRow.index,"enabled",modelRow.entry.enabled === false) }
                    ToolButton { glyph: "\uE70E"; tip: editor.t("上移模型","Move model up"); enabled: modelRow.index > 0; onClicked: backend.action("aiModelMove",{index:modelRow.index,to:modelRow.index-1}) }
                    ToolButton { glyph: "\uE70D"; tip: editor.t("下移模型","Move model down"); enabled: modelRow.index < editor.models.length-1; onClicked: backend.action("aiModelMove",{index:modelRow.index,to:modelRow.index+1}) }
                    ToolButton { glyph: "\uE8BB"; tip: editor.t("删除模型","Remove model"); onClicked: backend.action("aiModelRemove",{index:modelRow.index}) }
                    }
                    Row {
                        width: parent.width; height: 36; spacing: 8
                        Text { height: 36; verticalAlignment: Text.AlignVCenter; text: editor.t("失败冷却","Cooldown"); color: Theme.subtext; font.pixelSize: 12 }
                        InputBox {
                            objectName: "ai-model-cooldown-" + modelRow.index; width: 78; text: String(modelRow.entry.cooldownMinutes || 0)
                            validator: IntValidator { bottom: 0; top: 10080 }
                            onTextChanged: if(inputFocused) editor.modelField(modelRow.index,"cooldownMinutes",Number(text) || 0)
                        }
                        Text { height: 36; verticalAlignment: Text.AlignVCenter; text: modelRow.health.status === "cooling" ? editor.t("冷却中 · 剩余 ","Cooling · ") + Math.ceil((modelRow.health.retrySeconds || 0) / 60) + editor.t(" 分钟"," min") : modelRow.health.status === "disabled" ? editor.t("已禁用","Disabled") : modelRow.health.status === "success" ? editor.t("上次成功","Last request succeeded") : modelRow.health.status === "skipped" ? editor.t("待配置","Needs configuration") : editor.t("等待请求","Ready"); color: Theme.subtext; font.pixelSize: 12 }
                        FlatButton { objectName: "ai-model-reset-" + modelRow.index; visible: modelRow.health.status === "cooling"; text: editor.t("解除冷却","Clear cooldown"); textPixelSize: 12; onClicked: backend.action("aiModelReset", {index:modelRow.index}) }
                    }
                    Text { width: parent.width; visible: text.length > 0; text: modelRow.health.detail || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: Theme.faint; font.pixelSize: 12 }
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

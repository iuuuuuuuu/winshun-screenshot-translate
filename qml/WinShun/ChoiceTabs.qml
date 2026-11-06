// From LingCore/WinShun v0.20.0 (MIT); see THIRD_PARTY_NOTICES.md.
pragma ComponentBehavior: Bound
import QtQuick
import WinShun

// Plugin choices remain one exclusive setting when they span multiple lines.
Rectangle {
    id: tabs
    property var labels: []
    property int current: 0
    signal activated(int index)
    readonly property real dpr: Screen.devicePixelRatio
    implicitWidth: measure.implicitWidth + 6
    implicitHeight: flow.implicitHeight + 6
    radius: Theme.largeCardRadius
    color: Theme.track

    Row {
        id: measure
        visible: false
        spacing: 2
        Repeater {
            model: tabs.labels
            Text {
                id: measuredLabel
                required property string modelData
                text: modelData
                textFormat: Text.PlainText
                font.pixelSize: Theme.fontBody
                font.weight: Theme.fontStrong
                width: Theme.snapUp(Math.max(implicitWidth, normalMeasure.advanceWidth) + 26, tabs.dpr)
                TextMetrics {
                    id: normalMeasure
                    text: measuredLabel.modelData
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Normal
                }
            }
        }
    }

    Flow {
        id: flow
        x: 3
        y: 3
        width: Math.max(1, tabs.width - 6)
        spacing: 2
        Repeater {
            id: items
            model: tabs.labels
            delegate: Rectangle {
                id: chip
                required property int index
                required property string modelData
                objectName: "choice-" + index
                readonly property bool active: tabs.current === index
                width: Math.min(flow.width, Theme.snapUp(Math.max(metrics.advanceWidth, normalMetrics.advanceWidth) + 26, tabs.dpr))
                height: Math.max(32, label.implicitHeight + 14)
                radius: Theme.largeControlRadius
                color: active ? Theme.chipActive : pointer.containsMouse ? Theme.chipHover : "transparent"
                border.width: active || activeFocus ? 1 : 0
                border.color: activeFocus ? Theme.accent : Theme.chipBorder
                activeFocusOnTab: true
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData
                Accessible.checked: active
                Accessible.onPressAction: tabs.activated(index)
                TextMetrics {
                    id: metrics
                    text: chip.modelData
                    font.pixelSize: Theme.fontBody
                    font.weight: Theme.fontStrong
                }
                TextMetrics {
                    id: normalMetrics
                    text: chip.modelData
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.Normal
                }
                Text {
                    id: label
                    objectName: "label-" + chip.index
                    x: 12
                    y: Theme.snap((parent.height - height) / 2, tabs.dpr)
                    width: Math.max(1, parent.width - 24)
                    text: chip.modelData
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: chip.active ? Theme.text : Theme.subtext
                    font.pixelSize: Theme.fontBody
                    font.weight: chip.active ? Theme.fontStrong : Font.Normal
                }
                MouseArea {
                    id: pointer
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { chip.forceActiveFocus(); tabs.activated(chip.index) }
                }
                Keys.onPressed: event => {
                    let next = index
                    if (event.key === Qt.Key_Right || event.key === Qt.Key_Down)
                        next = (index + 1) % items.count
                    else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up)
                        next = (index + items.count - 1) % items.count
                    else if (event.key !== Qt.Key_Space && event.key !== Qt.Key_Return)
                        return
                    items.itemAt(next).forceActiveFocus()
                    tabs.activated(next)
                    event.accepted = true
                }
            }
        }
    }
}

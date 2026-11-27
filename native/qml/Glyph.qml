import QtQuick
import WinShun
Item {
    property string glyph
    property real size: 16
    property color color: Theme.subtext
    implicitWidth: size
    implicitHeight: size
    property var aliases: ({"\uE70D":"\uE136", "\uE70F":"\uE3B4", "\uE8BB":"\uE4F6", "\uE711":"\uE4F6",
        "\uE713":"\uE270", "\uE8AB":"\uE83C", "\uE8C8":"\uE1CA", "\uE890":"\uE220",
        "\uE72C":"\uE036", "\uE8C1":"\uE4A2", "\uE73E":"\uE182", "\uE712":"\uE1FE",
        "\uE70E":"\uE13C", "\uE718":"\uE3E2", "\uE76F":"\uEAE2", "\uE7A7":"\uE08A",
        "\uE734":"\uE46A", "\uE81C":"\uE19A", "\uE767":"\uE44A", "\uE71A":"\uE46C",
        "\uE74D":"\uE4A6", "\uE8A7":"\uE5DE",
        capture:"\uE10E", text:"\uE23A", compare:"\uE5A2"})
    PhosphorGlyph {
        text: parent.aliases[parent.glyph] || parent.glyph
        family: "Phosphor"
        em: parent.size * 1.125
        box: Qt.size(parent.width, parent.height)
        color: parent.color
    }
}

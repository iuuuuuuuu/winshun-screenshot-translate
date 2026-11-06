import QtQuick
import WinShunBridge

Item {
    property string glyph
    property real size: 16
    property color color: Theme.subtext
    implicitWidth: size
    implicitHeight: size
    OutlineGlyph {
        anchors.fill: parent
        glyph: parent.glyph
        em: parent.size
        ink: parent.color
    }
}

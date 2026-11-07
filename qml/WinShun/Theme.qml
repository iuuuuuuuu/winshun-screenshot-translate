pragma Singleton
import QtQuick

// WinShun v0.20.0 theme tokens (MIT), with the worker's appearance source.
QtObject {
    readonly property bool dark: backend.state.dark || false
    readonly property color accent: dark ? "#60CDFF" : "#0067C0"
    readonly property color accentFill: "#0067C0"
    readonly property color accentText: "#FFFFFF"
    readonly property color page: dark ? "#202020" : "#F3F3F3"
    readonly property color text: dark ? "#F3F3F3" : "#1B1B1B"
    readonly property color subtext: dark ? "#B8B8B8" : "#666666"
    readonly property color faint: "#8A8A8A"
    readonly property color card: dark ? "#2B2B2B" : "#FBFBFB"
    readonly property color cardBorder: dark ? "#1D1D1D" : "#E5E5E5"
    readonly property color divider: dark ? "#2E2E2E" : "#EAEAEA"
    readonly property color hover: dark ? "#292929" : "#F1F1F1"
    readonly property color track: dark ? "#2A2A2A" : "#EDEDED"
    readonly property color chipActive: dark ? "#3A3A3A" : "#FFFFFF"
    readonly property color chipHover: dark ? "#333333" : "#E4E4E4"
    readonly property color chipBorder: dark ? "#454545" : "#DCDCDC"
    readonly property color control: dark ? "#373737" : "#FFFFFF"
    readonly property color controlHover: dark ? "#3C3C3C" : "#F6F6F6"
    readonly property color controlPressed: dark ? "#323232" : "#F0F0F0"
    readonly property color controlBorder: dark ? "#444444" : "#D5D5D5"
    readonly property color inputFocused: dark ? "#1F1F1F" : "#FFFFFF"
    readonly property color textSelection: Qt.rgba(accent.r, accent.g, accent.b, .35)
    readonly property color menuBackground: dark ? "#2C2C2C" : "#F9F9F9"
    readonly property color menuHover: dark ? "#3A3A3A" : "#ECECEC"
    readonly property color danger: dark ? "#FF99A4" : "#C42B1C"
    readonly property color caution: dark ? "#FCE100" : "#9D5D00"
    readonly property color success: dark ? "#6CCB5F" : "#0F7B0F"
    readonly property int controlRadius: 4
    readonly property int largeControlRadius: 6
    readonly property int cardRadius: 6
    readonly property int largeCardRadius: 8
    readonly property int lineRadius: 1
    readonly property int fontStrong: Font.Bold
    readonly property int fontBody: 16
    readonly property int fontCaption: 14
    function edge(dpr) { return Math.max(1, Math.round(dpr)) / dpr }
    function snap(v, dpr) { return Math.round(v * dpr) / dpr }
    function snapUp(v, dpr) { return Math.ceil(v * dpr - 1e-6) / dpr }
}

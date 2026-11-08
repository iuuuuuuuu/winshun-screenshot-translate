import QtQuick

Window {
    // App supplies the current monitor's logical work area (excluding taskbar).
    property rect availableArea: Qt.rect(0, 0, 1280, 900)
    property real preferredWidth: 324
    property real contentHeight: 154
    readonly property int widthLimit: Math.max(1, Math.min(720, Math.floor(availableArea.width * 0.8)))
    readonly property int heightLimit: Math.max(1, Math.min(720, Math.floor(availableArea.height * 0.8)))
    maximumWidth: widthLimit
    maximumHeight: heightLimit
    width: Math.min(widthLimit, Math.max(324, Math.ceil(preferredWidth)))
    height: Math.min(heightLimit, Math.max(154, Math.ceil(contentHeight)))
}

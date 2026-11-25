import QtQuick

Window {
    // App supplies the current monitor's logical work area (excluding taskbar).
    property rect availableArea: Qt.rect(0, 0, 1280, 900)
    property real preferredWidth: 324
    property real contentHeight: 154
    readonly property int widthLimit: Math.max(1, Math.min(720, Math.floor(availableArea.width * 0.8)))
    readonly property int heightLimit: Math.max(1, Math.min(720, Math.floor(availableArea.height * 0.8)))
    // The software backing store can leave its last row/column unpainted when
    // integer logical dimensions land on fractional physical pixels. Align
    // both dimensions, including after moving between differently scaled screens.
    readonly property int pixelStep: {
        var dpr = Screen.devicePixelRatio
        for (var step = 1; step <= 16; ++step)
            if (Math.abs(step * dpr - Math.round(step * dpr)) < 0.0001) return step
        return 1
    }
    function alignedSize(preferred, limit) {
        return Math.max(1, Math.min(Math.ceil(preferred / pixelStep) * pixelStep,
                                   Math.floor(limit / pixelStep) * pixelStep))
    }
    maximumWidth: widthLimit
    maximumHeight: heightLimit
    width: alignedSize(Math.max(324, preferredWidth), widthLimit)
    height: alignedSize(Math.max(154, contentHeight), heightLimit)
}

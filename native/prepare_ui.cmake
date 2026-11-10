# Keep the community version's layout, with WinShun's own Quick-only helpers.
# The host deliberately does not ship the QtQuick.Controls QML plugin.
foreach(name Settings Toolbar AiChannels)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../qml/${name}.qml" content)
    string(REPLACE "import QtQuick.Controls\n" "" content "${content}")
    if(name STREQUAL "Settings")
        string(REPLACE "ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }"
            "ListScrollBar { parent: scroll.parent; anchors.top: scroll.top; anchors.bottom: scroll.bottom; anchors.right: scroll.right; flickable: scroll }" content "${content}")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nativeui/${name}.qml" "${content}")
endforeach()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../qml/WinShun/ToolButton.qml" content)
string(REPLACE "import QtQuick.Controls\n" "" content "${content}")
string(REPLACE "ToolTip.visible: tipEnabled && enabled && pointer.hovered && tip.length > 0\n    ToolTip.delay: 450\n    ToolTip.text: tip"
    "HoverTip { target: button.tipEnabled && button.enabled && pointer.hovered && button.tip.length > 0 ? button : null; delay: 450; text: button.tip }" content "${content}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shared/ToolButton.qml" "${content}")

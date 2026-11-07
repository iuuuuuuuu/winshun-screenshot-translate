cmake_minimum_required(VERSION 3.24)
project(ScreenshotTranslate VERSION 0.4.4 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick Network)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
add_compile_options(/utf-8 /EHsc)
add_executable(ScreenshotTranslate Launcher.cpp)
set_target_properties(ScreenshotTranslate PROPERTIES AUTOMOC OFF MSVC_RUNTIME_LIBRARY MultiThreaded)
qt_add_library(ScreenshotTranslateApp SHARED
    Main.cpp App.cpp App.h Services.cpp Services.h Dictionary.cpp Dictionary.h Http.cpp Http.h Desktop.cpp Desktop.h Probe.cpp Probe.h
    vendor/protocol/Protocol.cpp vendor/protocol/Protocol.h
    vendor/capture/PinWindow.cpp vendor/capture/PinTools.cpp vendor/capture/Geometry.cpp
    vendor/capture/SmartRedact.cpp
    vendor/PhosphorGlyph.cpp vendor/PhosphorGlyph.h)
target_include_directories(ScreenshotTranslateApp PRIVATE vendor/capture vendor/protocol vendor)
target_compile_options(ScreenshotTranslateApp PRIVATE /Zi)
target_link_options(ScreenshotTranslateApp PRIVATE /DEBUG /MAP)
target_link_libraries(ScreenshotTranslateApp PRIVATE Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network
    windowsapp ole32 oleaut32 user32 gdi32 imm32 crypt32 dwmapi shell32 winhttp psapi)
set(shared_qml)
foreach(name Theme FlatButton InputBox ChoiceTabs ToolButton ServiceRow LanguageMenu ServiceTip)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../qml/WinShun/${name}.qml" "${CMAKE_CURRENT_BINARY_DIR}/shared/${name}.qml" COPYONLY)
    set(file "${CMAKE_CURRENT_BINARY_DIR}/shared/${name}.qml")
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}.qml")
    list(APPEND shared_qml "${file}")
endforeach()
include(prepare_ui.cmake)
foreach(name HoverTip ListScrollBar)
    set(file "${CMAKE_CURRENT_SOURCE_DIR}/vendor/${name}.qml")
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}.qml")
    list(APPEND shared_qml "${file}")
endforeach()
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/shared/Theme.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Glyph.qml PROPERTIES QT_RESOURCE_ALIAS Glyph.qml)
qt_add_qml_module(ScreenshotTranslateApp URI WinShun NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/WinShun"
    QML_FILES ${shared_qml} qml/Glyph.qml
    SOURCES vendor/PhosphorGlyph.h)
set_target_properties(ScreenshotTranslateApp PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/nativeui/Settings.qml" PROPERTIES QT_RESOURCE_ALIAS Settings.qml)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/nativeui/Toolbar.qml" PROPERTIES QT_RESOURCE_ALIAS Toolbar.qml)
qt_add_resources(ScreenshotTranslateApp "screens" PREFIX "/ui" BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "${CMAKE_CURRENT_BINARY_DIR}/nativeui/Settings.qml" "${CMAKE_CURRENT_BINARY_DIR}/nativeui/Toolbar.qml"
        qml/Status.qml qml/Text.qml qml/PinMenu.qml qml/Word.qml)
qt_add_resources(ScreenshotTranslateApp "fonts" PREFIX "/fonts" BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    FILES ../assets/Phosphor.ttf)
option(BUILD_NATIVE_TESTS "Build native integration test driver" ON)
if(BUILD_NATIVE_TESTS)
    qt_add_executable(native_tests Tests.cpp Services.cpp Dictionary.cpp Http.cpp Desktop.cpp
        vendor/capture/PinWindow.cpp vendor/capture/PinTools.cpp vendor/capture/Geometry.cpp vendor/capture/SmartRedact.cpp)
    target_include_directories(native_tests PRIVATE vendor/capture)
    target_link_libraries(native_tests PRIVATE Qt6::Gui Qt6::Network windowsapp ole32 oleaut32 user32 gdi32 imm32 crypt32 winhttp)
endif()

# The dictionary is an optional Release asset, never a runtime dependency of
# the plugin package. Integration tests install it through the download path.

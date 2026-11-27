cmake_minimum_required(VERSION 3.24)
project(ScreenshotTranslate VERSION 0.4.37 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick)
if(NOT Qt6_VERSION VERSION_LESS "6.13.0")
    message(FATAL_ERROR "The host Qt contract is pinned to Qt 6.12.x")
endif()
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
add_compile_definitions(ST_VERSION="${PROJECT_VERSION}")
add_compile_options(/utf-8 /EHsc)
# Exercise the same UI font in developer probes and the shipped package.
configure_file(assets/AlibabaPuHuiTi-3-55-Regular.ttf fonts/AlibabaPuHuiTi-3-55-Regular.ttf COPYONLY)
add_executable(ScreenshotTranslate Launcher.cpp)
set_target_properties(ScreenshotTranslate PROPERTIES AUTOMOC OFF MSVC_RUNTIME_LIBRARY MultiThreaded)
target_link_libraries(ScreenshotTranslate PRIVATE version)
qt_add_library(ScreenshotTranslateApp SHARED
    Main.cpp App.cpp AppHistory.cpp App.h WindowSizeMemory.cpp WindowSizeMemory.h History.cpp History.h TextTools.cpp TextTools.h Speech.cpp Speech.h Services.cpp Services.h AiStream.cpp AiStream.h Dictionary.cpp Dictionary.h Http.cpp Http.h Desktop.cpp Desktop.h FirefoxSelection.cpp FirefoxSelection.h SelectionCopy.cpp SelectionCopy.h Probe.cpp Probe.h
    vendor/protocol/Protocol.cpp vendor/protocol/Protocol.h
    vendor/capture/PinWindow.cpp vendor/capture/PinTools.cpp vendor/capture/Geometry.cpp
    vendor/capture/SmartRedact.cpp
    vendor/PhosphorGlyph.cpp vendor/PhosphorGlyph.h)
target_include_directories(ScreenshotTranslateApp PRIVATE vendor/capture vendor/protocol vendor)
target_compile_options(ScreenshotTranslateApp PRIVATE /Zi)
target_link_options(ScreenshotTranslateApp PRIVATE /DEBUG /MAP)
target_link_libraries(ScreenshotTranslateApp PRIVATE Qt6::Gui Qt6::Quick Qt6::Qml
    windowsapp ole32 oleaut32 oleacc user32 gdi32 imm32 crypt32 dwmapi shell32 winhttp psapi elscore sapi)
set(shared_qml)
foreach(name Theme FlatButton InputBox ChoiceTabs ToolButton ServiceRow LanguageMenu ServiceTip WindowFrame)
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
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/nativeui/AiChannels.qml" PROPERTIES QT_RESOURCE_ALIAS AiChannels.qml)
# Cache the large settings UI at build time instead of parsing both pages on
# every open. They share the host-compatible Quick-only control module.
qt_target_qml_sources(ScreenshotTranslateApp QML_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/nativeui/Settings.qml" "${CMAKE_CURRENT_BINARY_DIR}/nativeui/AiChannels.qml")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/nativeui/Toolbar.qml" PROPERTIES QT_RESOURCE_ALIAS Toolbar.qml)
qt_add_resources(ScreenshotTranslateApp "screens" PREFIX "/ui" BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "${CMAKE_CURRENT_BINARY_DIR}/nativeui/Toolbar.qml"
        qml/Status.qml qml/Text.qml qml/PinMenu.qml qml/Word.qml qml/AdaptiveWindow.qml qml/Panel.qml qml/History.qml qml/SelectionIcon.qml qml/ProviderMenu.qml)
qt_add_resources(ScreenshotTranslateApp "fonts" PREFIX "/fonts" BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    FILES ../assets/Phosphor.ttf)
qt_add_resources(ScreenshotTranslateApp "probe-fixtures" PREFIX "/fixtures" BASE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    FILES fixtures/ocr-word.png fixtures/ocr-spaces.png fixtures/ocr-mixed-chinese.png fixtures/ocr-mixed-english.png
        fixtures/ocr-wrapped-chinese.png fixtures/ocr-wrapped-english.png)
option(BUILD_NATIVE_TESTS "Build native integration test driver" ON)
if(BUILD_NATIVE_TESTS)
    qt_add_executable(native_tests Tests.cpp SelectionCopyTests.cpp SelectionCopyReleaseTests.cpp Services.cpp AiStream.cpp History.cpp TextTools.cpp Speech.cpp Dictionary.cpp Http.cpp Desktop.cpp FirefoxSelection.cpp SelectionCopy.cpp
        vendor/capture/PinWindow.cpp vendor/capture/PinTools.cpp vendor/capture/Geometry.cpp vendor/capture/SmartRedact.cpp)
    target_include_directories(native_tests PRIVATE vendor/capture)
    target_compile_definitions(native_tests PRIVATE ST_SELECTION_COPY_TESTS)
    target_link_libraries(native_tests PRIVATE Qt6::Gui windowsapp ole32 oleaut32 oleacc user32 gdi32 imm32 crypt32 winhttp elscore sapi)
endif()

# The dictionary is an optional Release asset, never a runtime dependency of
# the plugin package. Integration tests install it through the download path.

#include "App.h"
#include <QFontDatabase>
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QClipboard>
#include <QIcon>
#include <QPixmap>
#include <shellapi.h>
#include "Probe.h"
#include <windows.h>
#include <cstdio>
extern "C" __declspec(dllexport) int screenshotTranslateMain(const wchar_t* appDirName)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QByteArray rest;auto first=ws::captureplugin::Protocol::readFirst(&rest);
    if(!first||first->value("method").toString()!="initialize")return 2;
    auto params=first->value("params").toObject();QString appDir=QString::fromWCharArray(appDirName);
    QCoreApplication::setLibraryPaths({appDir});qputenv("QML_IMPORT_PATH",(appDir+"/qml").toUtf8());
    // Follow the renderer already selected by WinShun. Forcing software here
    // made a large, scrolling settings page behave differently from the host.
    const auto renderer=params.value("appearance").toObject().value("renderer").toString();
    const bool software=renderer=="software"||(renderer!="d3d11"&&qEnvironmentVariable("QT_QUICK_BACKEND")=="software");
    if(!software&&qEnvironmentVariable("QT_QUICK_BACKEND")=="software")qunsetenv("QT_QUICK_BACKEND");
    qputenv("QT_QPA_PLATFORM",software?"windows:fontengine=gdi":"windows:fontengine=freetype");
    qputenv("QT_D3D_NO_VBLANK_THREAD","1");
    if(!qEnvironmentVariableIsSet("QT_D3D_MAX_FRAME_LATENCY"))qputenv("QT_D3D_MAX_FRAME_LATENCY","0");
    int argc=1;char name[]="ScreenshotTranslate";char* argv[]{name,nullptr};QGuiApplication app(argc,argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);
    HICON icon=nullptr;
    auto hostExe=(appDir+"/WinShun.exe").toStdWString();
    if(ExtractIconExW(hostExe.c_str(),0,&icon,nullptr,1)&&icon){
        QGuiApplication::setWindowIcon(QIcon(QPixmap::fromImage(QImage::fromHICON(icon))));DestroyIcon(icon);
    }
    QGuiApplication::clipboard()->blockSignals(true);
    QQuickWindow::setGraphicsApi(software?QSGRendererInterface::Software:QSGRendererInterface::Direct3D11);
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    QFontDatabase::addApplicationFont(":/fonts/Phosphor.ttf");
    QFile f(QCoreApplication::applicationDirPath()+"/fonts/AlibabaPuHuiTi-3-55-Regular.ttf");
    if(f.open(QIODevice::ReadOnly)){
        auto id=QFontDatabase::addApplicationFontFromData(f.readAll());auto families=QFontDatabase::applicationFontFamilies(id);
        if(!families.isEmpty()){QFont font(families[0]);font.setHintingPreference(QFont::PreferVerticalHinting);QGuiApplication::setFont(font);}
    }
    // The same grayscale policy also keeps glyph edges clean on transparent
    // popup windows and fractional display scales (no ClearType colour halos).
    auto uiFont=QGuiApplication::font();
    uiFont.setStyleStrategy(QFont::StyleStrategy(uiFont.styleStrategy()|QFont::NoSubpixelAntialias));
    QGuiApplication::setFont(uiFont);
    ws::captureplugin::Protocol protocol(rest);
    try {
        st::App plugin(protocol,appDir,params);
        QObject::connect(&protocol,&ws::captureplugin::Protocol::requested,&plugin,&st::App::dispatch);
        QObject::connect(&protocol,&ws::captureplugin::Protocol::notified,&plugin,[&](auto method,auto params){plugin.dispatch({},method,params);});
        // Let Qt release render threads before Windows unloads the GPU DLLs.
        QObject::connect(&protocol,&ws::captureplugin::Protocol::closed,&app,&QCoreApplication::quit);
        protocol.respond(first->value("id"),QJsonObject{{"protocolVersion","1"},{"name","iuuuuuuuu.screenshot-translate"}});
        HANDLE token=nullptr;TOKEN_ELEVATION elevation{};DWORD size=0;
        if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){
            GetTokenInformation(token,TokenElevation,&elevation,sizeof elevation,&size);CloseHandle(token);
        }
        std::fprintf(stderr,"[screenshot-translate] version=%s runtime=host-qt ocr=windows selection-reader=%s\n",
            ST_VERSION,elevation.TokenIsElevated?"administrator":"standard");
        if(st::Probe::requested())QTimer::singleShot(0,&plugin,[&]{st::Probe::run(plugin,params);});
        return app.exec();
    }catch(const std::exception&){
        protocol.respondError(first->value("id"),-32000,"无法初始化；已有配置未覆盖");return 3;
    }
}
extern "C" __declspec(dllexport) int screenshotTranslateProbe(const wchar_t* appDir)
{
    if(!st::Probe::privateDesktop())return 10;
    st::Probe::enable();return screenshotTranslateMain(appDir);
}
extern "C" __declspec(dllexport) int screenshotTranslateSelection(const wchar_t*)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc=1;char name[]="SelectionReader";char* argv[]{name,nullptr};QCoreApplication app(argc,argv);
    return st::selectionWorker();
}

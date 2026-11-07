#include "App.h"
#include <QFontDatabase>
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QNetworkProxyFactory>
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
    // These are small controls, not the capture editor. Software rendering
    // avoids per-window D3D heaps and waiting for GPU presentation on close.
    bool software=true;
    qputenv("QT_QPA_PLATFORM",software?"windows:fontengine=gdi":"windows:fontengine=freetype");qputenv("QT_D3D_NO_VBLANK_THREAD","1");
    int argc=1;char name[]="ScreenshotTranslate";char* argv[]{name,nullptr};QGuiApplication app(argc,argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);QNetworkProxyFactory::setUseSystemConfiguration(true);
    HICON icon=nullptr;
    auto hostExe=(appDir+"/WinShun.exe").toStdWString();
    if(ExtractIconExW(hostExe.c_str(),0,&icon,nullptr,1)&&icon){
        QGuiApplication::setWindowIcon(QIcon(QPixmap::fromImage(QImage::fromHICON(icon))));DestroyIcon(icon);
    }
    QGuiApplication::clipboard()->blockSignals(true);
    QQuickWindow::setGraphicsApi(software?QSGRendererInterface::Software:QSGRendererInterface::Direct3D11);
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    QFontDatabase::addApplicationFont(":/fonts/Phosphor.ttf");
    QFile f(appDir+"/fonts/AlibabaPuHuiTi-3-55-Regular.ttf");
    if(f.open(QIODevice::ReadOnly)){
        auto id=QFontDatabase::addApplicationFontFromData(f.readAll());auto families=QFontDatabase::applicationFontFamilies(id);
        if(!families.isEmpty()){QFont font(families[0]);font.setHintingPreference(QFont::PreferVerticalHinting);QGuiApplication::setFont(font);}
    }
    ws::captureplugin::Protocol protocol(rest);
    try {
        st::App plugin(protocol,appDir,params);
        QObject::connect(&protocol,&ws::captureplugin::Protocol::requested,&plugin,&st::App::dispatch);
        QObject::connect(&protocol,&ws::captureplugin::Protocol::notified,&plugin,[&](auto method,auto params){plugin.dispatch({},method,params);});
        QObject::connect(&protocol,&ws::captureplugin::Protocol::closed,&app,[]{ExitProcess(0);});
        protocol.respond(first->value("id"),QJsonObject{{"protocolVersion","1"},{"name","iuuuuuuuu.screenshot-translate"}});
        HANDLE token=nullptr;TOKEN_ELEVATION elevation{};DWORD size=0;
        if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){
            GetTokenInformation(token,TokenElevation,&elevation,sizeof elevation,&size);CloseHandle(token);
        }
        std::fprintf(stderr,"[screenshot-translate] version=0.4.0 runtime=native ocr=windows selection-reader=%s\n",
            elevation.TokenIsElevated?"administrator":"standard");
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

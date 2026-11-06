#include "Probe.h"
#include "App.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QPainter>
#include <QQuickItem>
#include <QMouseEvent>
#include <QQmlProperty>
#include <QTimer>
#include <QThread>
#include <functional>
#include <cstring>
#include <cstdio>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdexcept>
namespace st {
namespace {
bool enabled=false;
POINT rawPoint{};
USHORT rawFlags=0;
UINT WINAPI readRaw(HRAWINPUT,UINT,LPVOID data,PUINT size,UINT)
{
    RAWINPUT raw{};raw.header.dwType=RIM_TYPEMOUSE;raw.header.dwSize=sizeof raw;raw.data.mouse.usButtonFlags=rawFlags;
    if(data)std::memcpy(data,&raw,sizeof raw);*size=sizeof raw;return sizeof raw;
}
BOOL WINAPI cursor(LPPOINT p){*p=rawPoint;return TRUE;}
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
QJsonObject memory()
{
    PROCESS_MEMORY_COUNTERS_EX info{};info.cb=sizeof info;
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),sizeof info);
    DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);
    return {{"privateMB",double(info.PrivateUsage)/(1024*1024)},{"workingMB",double(info.WorkingSetSize)/(1024*1024)},{"handles",int(handles)}};
}
void pump(int ms)
{
    QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();
}
bool until(std::function<bool()> ready,int max=3000)
{
    QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<max)pump(10);return ready();
}
void click(QQuickWindow* w,QQuickItem* item)
{
    check(w&&item,"missing click target");auto p=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    QMouseEvent press(QEvent::MouseButtonPress,p,w->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,p,w->mapToGlobal(p.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(w,&press);QCoreApplication::sendEvent(w,&release);pump(20);
}
}
bool Probe::privateDesktop()
{
    wchar_t name[256]{};DWORD bytes=0;
    if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,name,sizeof name,&bytes))return false;
    return QString::fromWCharArray(name).startsWith("WinShun_ST_Probe_");
}
bool Probe::requested(){return enabled;}
void Probe::enable(){enabled=true;}
void Probe::run(App& app,const QJsonObject& params)
{
    QString out=params.value("probeOutput").toString();QJsonObject report;
    int code=1;
    try{
        check(privateDesktop(),"private desktop required");check(!out.isEmpty(),"probe output required");QDir().mkpath(out);
        report["qtVersion"]=qVersion();
        report["uiRenderer"]="software";
        report["hostRenderer"]=params.value("appearance").toObject().value("renderer");
        QStringList modules;bool python=false;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        MODULEENTRY32W entry{sizeof entry};
        if(snapshot!=INVALID_HANDLE_VALUE){
            if(Module32FirstW(snapshot,&entry))do{
                QString name=QString::fromWCharArray(entry.szModule);modules<<name;
                python=python||name.startsWith("python",Qt::CaseInsensitive)||name.startsWith("pyside",Qt::CaseInsensitive);
            }while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);
        }
        check(!python,"runtime imported Python");report["pythonLoaded"]=python;
        report["loadedModules"]=QJsonArray::fromStringList(modules);
        report["startupMemory"]=memory();
        QImage fixture(820,380,QImage::Format_RGB32);fixture.fill(Qt::white);
        QPainter paint(&fixture);QFont font("Segoe UI");font.setPixelSize(24);paint.setFont(font);paint.setPen(QColor("#1B1B1B"));
        paint.drawText(32,70,"Screenshot translation");
        paint.drawText(32,126,"Settings");paint.drawText(240,126,"History");paint.drawText(480,126,"Downloads");
        paint.drawText(36,205,"Native windows keep the capture in place.");
        paint.drawText(32,268,"Loading and outside clicks stay responsive.");paint.end();
        fixture.save(out+"/source.png");
        app.m_capture=[fixture](const QRect& area){
            QImage image(area.size(),QImage::Format_RGB32);image.fill(Qt::white);QPainter p(&image);
            p.drawImage(QPoint(100,100)-area.topLeft(),fixture);return image;
        };
        report["captureSource"]="synthetic pixels on private desktop; native monitor masks";
        ws::capture::PinWindow background(fixture,{100,100},{});
        pump(150);
        QImage selected;QRect selectedRect;QElapsedTimer timer;timer.start();
        Selector selector([&](QImage image,QRect area){selected=image;selectedRect=area;},app.m_capture);
        check(selector.show(),"selector show");
        report["selectorShowMs"]=double(timer.elapsed());
        QJsonArray monitors;
        for(auto hwnd:selector.windows()){
            RECT r{};GetWindowRect(hwnd,&r);POINT center{(r.left+r.right)/2,(r.top+r.bottom)/2};auto expected=monitorRect(center);
            check(QRect(r.left,r.top,r.right-r.left,r.bottom-r.top)==expected,"monitor mask coverage");
            monitors.append(QJsonArray{int(r.left),int(r.top),int(r.right-r.left),int(r.bottom-r.top)});
        }
        report["monitors"]=monitors;
        selector.input(WM_LBUTTONDOWN,{100,100});selector.input(WM_MOUSEMOVE,{919,479});selector.input(WM_LBUTTONUP,{919,479});
        check(until([&]{return !selected.isNull();}),"selector result");
        check(selected.size()==fixture.size(),"selected pixel size");
        check(selected.pixelColor(36,40)==fixture.pixelColor(36,40),"capture pixels");
        if(monitors.size()>1&&monitors[1].toArray()[0].toInt()==monitors[0].toArray()[2].toInt()){
            QImage crossImage;QRect crossRect;
            Selector cross([&](QImage image,QRect area){crossImage=image;crossRect=area;},app.m_capture);
            check(cross.show(),"cross screen selector");
            int seam=monitors[1].toArray()[0].toInt();
            cross.input(WM_LBUTTONDOWN,{seam-80,180});cross.input(WM_MOUSEMOVE,{seam+80,260});cross.input(WM_LBUTTONUP,{seam+80,260});
            check(until([&]{return !crossImage.isNull();}),"cross screen result");
            check(crossRect==QRect(seam-80,180,161,81),"physical cross screen coordinates");
            report["crossScreenSelection"]=true;
        }
        background.close();
        std::fputs("[probe] opening pin\n",stderr);std::fflush(stderr);
        app.openPin(selected,selectedRect);
        std::fputs("[probe] starting OCR\n",stderr);std::fflush(stderr);app.recognizePin();
        pump(25);check(app.m_status.window&&app.m_pinBackend.values.value("loading").toBool(),"loading window");
        app.m_status.window->grabWindow().save(out+"/loading.png");
        check(until([&]{return !app.m_blocks.isEmpty();},6000),"real Windows OCR");
        std::fputs("[probe] OCR completed\n",stderr);std::fflush(stderr);
        report["ocrBlocks"]=app.m_blocks.size();
        check(until([&]{return !app.m_busy;},25000),"translation completion");
        std::fputs("[probe] translation completed\n",stderr);std::fflush(stderr);
        check(!app.m_data.value("translated").toString().isEmpty(),"native screenshot translation");
        check(app.m_pin->pictureRect().size()==fixture.size(),"translation pixel stability");
        auto pinOrigin=app.m_pin->pictureRect().topLeft();RECT suggestion{20,20,600,300};
        UINT dpi=GetDpiForWindow(app.m_pin->hwnd());
        SendMessageW(app.m_pin->hwnd(),WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&suggestion));
        check(app.m_pin->pictureRect()==QRect(pinOrigin,fixture.size()),"DPI change preserves physical image");
        SendMessageW(app.m_pin->hwnd(),WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&suggestion));
        report["dpiPixelStable"]=true;
        app.m_translated.save(out+"/translation.png");
        app.m_bar.window->grabWindow().save(out+"/toolbar.png");
        app.m_pin->image().save(out+"/pin.png");
        app.pinAction("compare",{});check(app.m_pin->pictureRect().height()==fixture.height()*2+60,"vertical comparison");
        check(app.m_pin->image().pixelColor(30,fixture.height()+75)==fixture.pixelColor(30,15),"translation first");
        app.m_pin->image().save(out+"/comparison.png");
        app.m_settings["comparisonOrder"]="source-first";app.render();
        check(app.m_pin->image().pixelColor(30,45)==fixture.pixelColor(30,15),"source first");
        app.pinAction("compare",{});check(app.m_pin->pictureRect().size()==fixture.size(),"comparison reset");
        auto menu=app.m_bar.window->findChild<QQuickWindow*>("target-menu");check(menu,"target menu");
        check(QMetaObject::invokeMethod(menu,"open"),"open language menu");
        check(menu->isVisible()&&menu->height()>app.m_bar.window->height(),"top level language menu");
        pump(60);
        // A never-switched desktop cannot keep native keyboard focus. The
        // immediate open above verifies the action; restore only the preview.
        if(!menu->isVisible()){menu->setProperty("receivedFocus",false);menu->show();pump(15);}
        menu->grabWindow().save(out+"/languages.png");menu->hide();
        app.showSettings();pump(250);check(app.m_config.window->isVisible(),"settings visible");
        app.m_config.window->grabWindow().save(out+"/settings.png");
        auto testAll=app.m_config.window->findChild<QQuickItem*>("test-all");click(app.m_config.window,testAll);
        check(until([&]{return app.m_testRunning==0&&app.m_testQueue.isEmpty();},25000),"batch tests finish");
        check(app.m_tests.size()==10,"batch covers all ten providers");
        report["batchResults"]=QJsonValue::fromVariant(app.m_configBackend.values.value("testResults"));
        app.m_config.window->grabWindow().save(out+"/settings-tested.png");
        report["activeMemory"]=memory();
        auto preferred=app.m_settings.value("provider");app.configSelect("deepl");app.configAction("save",{});
        check(app.m_settings.value("provider")==preferred,"save doesn't switch preferred provider");
        pump(40);
        check(until([&]{return app.m_raw&&app.m_raw->m_hwnd.load();}),"independent raw thread");
        app.m_raw->m_read=&readRaw;app.m_raw->m_cursor=&cursor;
        app.m_pinOpened=GetTickCount()-500;
        RECT pinBounds{};GetWindowRect(app.m_pin->hwnd(),&pinBounds);
        rawPoint={pinBounds.left+30,pinBounds.top+30};rawFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
        PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);pump(40);check(bool(app.m_pin),"inside pin keeps open");
        RECT barBounds{};GetWindowRect(HWND(app.m_bar.window->winId()),&barBounds);
        rawPoint={barBounds.left+15,barBounds.top+20};
        PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);pump(40);check(bool(app.m_pin),"inside bar keeps open");
        rawPoint={-30000,-30000};rawFlags=0;PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);pump(40);check(bool(app.m_pin),"movement keeps open");
        app.m_settings["closeOnOutsideClick"]=false;rawFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
        PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);pump(40);check(bool(app.m_pin),"outside dismissal configurable");
        app.m_settings["closeOnOutsideClick"]=true;timer.restart();
        PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);
        QThread::msleep(45); // deliberately withhold GUI pumping: raw thread still receives
        check(until([&]{return !app.m_pin;},300),"outside dismiss");
        report["outsideDismissMs"]=double(timer.elapsed());
        app.openPin(fixture,{100,100,820,380});
        app.m_blocks={{"Hello world",QRectF(20,20,160,32),24}};
        app.m_settings["provider"]="mymemory";app.m_settings["fallbackEnabled"]=false;
        auto blocked=app.m_services->profile("mymemory");blocked["endpoint"]=params.value("probeBlockedEndpoint");
        app.m_services->save({{"mymemory",blocked}});
        app.dispatch(200,"query",{{"command","clip"},{"text","Hello + 100%"},{"queryId",99}});
        pump(30);
        app.dispatch({},"cancel",{{"queryId",99}});
        check(app.m_queries.empty(),"query cancellation clears pending state");
        app.dispatch(201,"query",{{"command","clip"},{"text","Hello + 100%"},{"queryId",100}});
        app.dispatch(202,"query",{{"command","clip"},{"text","New input"},{"queryId",100}});
        app.dispatch({},"cancel",{{"queryId",100}});
        app.m_settings["provider"]="google";
        app.dispatch(203,"query",{{"command","clip"},{"text","Hello + 100%"},{"queryId",101}});
        check(until([&]{return app.m_queries.empty();}),"text query completion");
        report["queryProtocol"]=true;app.m_settings["provider"]="mymemory";
        // The harness sets this service endpoint to a deliberately blocked response.
        app.translatePin();pump(80);
        check(app.m_busy,"blocked request started");auto hwnd=app.m_pin->hwnd();timer.restart();
        QQuickItem* close=nullptr;
        for(auto item:app.m_bar.window->findChildren<QQuickItem*>())
            if(item->property("glyph").toString()=="\uE8BB"&&item->property("tipEnabled").isValid())close=item;
        click(app.m_bar.window,close);check(!app.m_pin&&!IsWindow(hwnd)&&!app.m_busy,"close cancels blocked request");
        report["closeWhileBlockedMs"]=double(timer.elapsed());
        app.dispatch({},"hotkey",QJsonObject{{"key","Alt+Shift+T"}});pump(30);check(bool(app.m_selector),"new hotkey accepted immediately");
        auto first=app.m_selector.get();
        for(int i=0;i<5;++i)app.dispatch({},"hotkey",QJsonObject{{"key","Alt+Shift+T"}});
        pump(20);check(app.m_selector.get()==first,"busy repeated hotkeys ignored");
        report["hotkeyProtocol"]=true;app.closePin();
        // Hotkey uniqueness can span desktops in the window station. Use
        // probe-only chords rather than competing with the user's real keys.
        app.m_settings["shotHotkey"]="Ctrl+Alt+Shift+F23";
        app.m_settings["clipHotkey"]="Ctrl+Alt+Shift+F24";
        app.m_hostHotkeys=false;app.registerKeys();
        check(app.m_nativeKeys.size()==2,"native fallback registers both Windows hotkeys");
        PostMessageW(app.m_keyWindow,WM_HOTKEY,1,0);
        check(until([&]{return bool(app.m_selector);}),"WM_HOTKEY opens selector");app.closePin();
        app.m_settings["shotHotkey"]="Alt+Shift+F8";app.m_settings["clipHotkey"]="";app.registerKeys();
        check(app.m_nativeKeys.size()==1,"native hotkey configuration refresh");
        check(RegisterHotKey(app.m_keyWindow,20,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F23),"old hotkey released");
        UnregisterHotKey(app.m_keyWindow,20);
        report["nativeHotkeyFallback"]=true;
        pump(200);report["closedMemory"]=memory();
        report["passed"]=true;code=0;
    }catch(const std::exception& e){report["passed"]=false;report["error"]=QString::fromUtf8(e.what());}
    try{atomicJson(out+"/report.json",report);}catch(...){}
    app.closePin();app.configAction("close",{});
    ExitProcess(code);
}
}

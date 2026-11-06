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
QImage printed(HWND window,QSize size)
{
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};void* pixels=nullptr;
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size.width();
    info.bmiHeader.biHeight=-size.height();info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    check(dc&&bitmap,"print client bitmap");auto old=SelectObject(dc,bitmap);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    auto image=QImage(static_cast<uchar*>(pixels),size.width(),size.height(),QImage::Format_RGB32).copy();
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return image;
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
        // Nonuniform pixels expose scaling, vertical flips and source-coordinate
        // errors that a white OCR fixture cannot detect.
        Selector patterned([](QImage,QRect){},[](const QRect& area){
            QImage image(area.size(),QImage::Format_RGB32);
            for(int y=0;y<image.height();++y){
                auto row=reinterpret_cast<QRgb*>(image.scanLine(y));
                for(int x=0;x<image.width();++x)row[x]=qRgb((x+area.x())%251,(y+area.y())%241,(x/17+y/13)%239);
            }return image;
        });
        check(patterned.show(),"patterned selector");
        auto& surface=*patterned.m_surfaces[0];auto origin=surface.rect.topLeft();
        for(int step=0;step<8;++step){
            auto start=origin+QPoint(80,90),end=origin+QPoint(600-step*40,500-step*30);
            patterned.input(WM_LBUTTONDOWN,{start.x(),start.y()});
            patterned.input(WM_MOUSEMOVE,{end.x(),end.y()});
            UpdateWindow(surface.hwnd);
            auto frame=printed(surface.hwnd,surface.rect.size());
            check(frame.pixel(130,140)==surface.image.pixel(130,140),"selector pixels stay aligned");
            check(frame.pixel(30,40)==surface.dim.pixel(30,40),"outside selection stays dim");
            if(step)check(frame.pixel(595,495)==surface.dim.pixel(595,495),"shrunk selection restored");
            if(step==0)frame.copy(0,0,720,580).save(out+"/selector-drag.png");
        }
        patterned.cancel();report["selectorDragPixels"]=true;
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
        QImage word(40,20,QImage::Format_RGB32);word.fill(Qt::white);
        QPainter wordPaint(&word);QFont wordFont("Segoe UI");wordFont.setPixelSize(12);
        wordPaint.setFont(wordFont);wordPaint.drawText(2,15,"word");wordPaint.end();
        app.openPin(word,{400,300,40,20});
        app.m_pinBackend.update({{"loadingStage","translate"}});
        app.status("正在翻译…");pump(40);
        check(app.m_status.window->size()==QSize(24,24),"tiny capture has 24px loading indicator");
        auto unobscured=[&]{
            RECT r{};GetWindowRect(HWND(app.m_status.window->winId()),&r);
            return !QRect(r.left,r.top,r.right-r.left,r.bottom-r.top).intersects(app.m_pin->pictureRect());
        };
        check(unobscured(),"loading indicator leaves word unobscured");
        app.m_status.window->grabWindow().save(out+"/loading-word.png");
        auto beforeMove=app.m_status.window->position();
        SendMessageW(app.m_pin->hwnd(),WM_KEYDOWN,VK_RIGHT,0);app.place();pump(20);
        check(app.m_status.window->position()!=beforeMove&&unobscured(),"indicator follows word movement");
        auto zoomWord=[&](double target){
            for(int i=0;i<80&&std::abs(app.m_pin->zoom()-target)>.001;++i){
                auto at=app.m_pin->pictureRect().topLeft();
                int delta=app.m_pin->zoom()<target?WHEEL_DELTA:-WHEEL_DELTA;
                SendMessageW(app.m_pin->hwnd(),WM_MOUSEWHEEL,MAKEWPARAM(0,SHORT(delta)),MAKELPARAM(at.x(),at.y()));
            }
            check(std::abs(app.m_pin->zoom()-target)<.001,"word wheel zoom");
            app.place();pump(25);app.place();
        };
        zoomWord(3);
        check(!app.m_status.window->property("iconOnly").toBool(),"expanded word shows short loading text");
        auto label=app.m_status.window->findChild<QQuickItem*>("status-label");
        check(label&&label->isVisible()&&label->property("text").toString()=="翻译中","compact translation label");
        report["compactLoadingSize"]=QJsonArray{app.m_status.window->width(),app.m_status.window->height()};
        report["compactLoadingUnobscured"]=unobscured();
        app.m_status.window->grabWindow().save(out+"/loading-compact.png");
        check(app.m_status.window->height()<=28&&app.m_status.window->width()<=120&&unobscured(),"compact loading size");
        zoomWord(8);
        check(!app.m_status.window->property("compact").toBool()&&app.m_status.window->height()<=36,"larger capture loading size");
        zoomWord(.5);
        check(app.m_status.window->size()==QSize(24,24)&&unobscured(),"loading shrinks with capture");
        app.status("无法连接服务；请检查网络或共用代理",false);pump(25);app.place();
        check(label->isVisible()&&app.m_status.window->width()<=240,"small capture retains readable error");
        app.m_status.window->grabWindow().save(out+"/loading-word-error.png");
        report["adaptiveLoading"]=true;app.closePin();pump(25);
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

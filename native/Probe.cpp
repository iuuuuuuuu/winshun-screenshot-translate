#include "Probe.h"
#include "App.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QPainter>
#include <QQuickItem>
#include <QScreen>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QFontMetricsF>
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
    PROCESS_MEMORY_COUNTERS_EX2 info{};info.cb=sizeof info;
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),sizeof info);
    DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);
    return {{"privateMB",double(info.PrivateUsage)/(1024*1024)},{"workingMB",double(info.WorkingSetSize)/(1024*1024)},
        {"privateWorkingMB",double(info.PrivateWorkingSetSize)/(1024*1024)},{"handles",int(handles)}};
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
    check(item->isVisible()&&item->isEnabled()&&item->width()>0&&item->height()>0,"click target is visible and enabled");
    // Route through the native event dispatcher so Qt's global button state
    // matches the window events, including on Qt 6.10. These messages target
    // only this test's HWND on its never-activated private desktop.
    check(Probe::privateDesktop(),"click requires private test desktop");
    auto point=p*w->devicePixelRatio();auto position=MAKELPARAM(qRound(point.x()),qRound(point.y()));
    auto hwnd=reinterpret_cast<HWND>(w->winId());
    PostMessageW(hwnd,WM_MOUSEMOVE,0,position);pump(10);
    PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,position);pump(10);
    PostMessageW(hwnd,WM_LBUTTONUP,0,position);pump(20);
}
QQuickItem* visualItem(QQuickItem* root,const QString& name)
{
    if(root->objectName()==name)return root;
    for(auto child:root->childItems())if(auto found=visualItem(child,name))return found;
    return nullptr;
}
void typeDraft(QQuickWindow* window,const QString& text,const QString& name="word-editor")
{
    check(Probe::privateDesktop(),"typing requires private test desktop");
    auto editor=visualItem(window->contentItem(),name);check(editor,"source editor exists");
    // Deliver focus and IME commits only to the probe window on its private
    // desktop. This does not send input to the Owner's foreground application.
    QFocusEvent focus(QEvent::FocusIn,Qt::MouseFocusReason);QCoreApplication::sendEvent(window,&focus);
    editor->forceActiveFocus(Qt::MouseFocusReason);
    check(window->activeFocusItem()==editor,"editor receives window keyboard focus");
    check(!editor->property("readOnly").toBool(),"source editor accepts changes");
    QMetaObject::invokeMethod(editor,"selectAll");
    if(text.isEmpty()){
        QKeyEvent remove(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);QCoreApplication::sendEvent(window,&remove);
    }else{
        QInputMethodEvent input;input.setCommitString(text);QCoreApplication::sendEvent(window,&input);
    }pump(10);
    check(editor->property("text").toString()==text,"IME commit changes source draft");
}
void typeField(QQuickWindow* window,const QString& name,const QString& text)
{
    check(Probe::privateDesktop(),"field typing requires private desktop");
    auto box=visualItem(window->contentItem(),name);check(box&&box->isVisible(),"AI text field exists");
    auto scroll=window->findChild<QQuickItem*>("settings-scroll");check(scroll,"settings scroll exists");
    auto y=box->mapToItem(scroll,QPointF()).y()+scroll->property("contentY").toDouble();
    scroll->setProperty("contentY",std::clamp(y-40,0.0,std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height())));pump(30);
    QFocusEvent focus(QEvent::FocusIn,Qt::MouseFocusReason);QCoreApplication::sendEvent(window,&focus);
    QMetaObject::invokeMethod(box,"takeFocus");check(window->activeFocusItem(),"AI field takes focus");
    QKeyEvent select(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier);QCoreApplication::sendEvent(window,&select);
    QInputMethodEvent input;input.setCommitString(text);QCoreApplication::sendEvent(window,&input);pump(30);
    check(box->property("text").toString()==text,"AI field accepts input without losing focus");
}
void dragQueue(QQuickWindow* window,QQuickItem* item,int distance)
{
    check(Probe::privateDesktop()&&window&&item&&item->isVisible(),"queue drag requires private visible test item");
    auto start=item->mapToScene(QPointF(item->width()/2,item->height()/2));auto hwnd=HWND(window->winId());
    auto point=[&](int dy){auto p=(start+QPointF(0,dy))*window->devicePixelRatio();return MAKELPARAM(qRound(p.x()),qRound(p.y()));};
    PostMessageW(hwnd,WM_MOUSEMOVE,0,point(0));pump(10);
    PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point(0));pump(10);
    for(int i=1;i<=8;++i){PostMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,point(distance*i/8));pump(10);}
    PostMessageW(hwnd,WM_LBUTTONUP,0,point(distance));pump(30);
}
void editableSource(QQuickWindow* window,const QString& source)
{
    auto editor=window->findChild<QQuickItem*>("word-editor");
    auto meaning=window->findChild<QQuickItem*>("word-meaning");
    check(editor&&editor->isVisible()&&editor->isEnabled()&&!editor->property("readOnly").toBool(),
        "source is directly editable without switching modes");
    check(editor->property("text").toString()==source,"editable source follows recognized or selected text");
    check(meaning&&meaning->isVisible(),"automatic translation remains visible alongside editable source");
}
void key(QQuickWindow* window,int code,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress,code,modifiers),release(QEvent::KeyRelease,code,modifiers);
    QCoreApplication::sendEvent(window,&press);QCoreApplication::sendEvent(window,&release);pump(20);
}
void hoverItem(QQuickWindow* window,QQuickItem* item)
{
    check(Probe::privateDesktop()&&window&&item,"hover requires private test desktop");
    auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    QMouseEvent move(QEvent::MouseMove,point,window->mapToGlobal(point.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&move);pump(20);
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
        QStringList modules;QJsonObject modulePaths;bool python=false;
        auto runtime=QDir::cleanPath(params.value("probeRuntime").toString(QCoreApplication::applicationDirPath()+"/runtime"));
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        MODULEENTRY32W entry{sizeof entry};
        if(snapshot!=INVALID_HANDLE_VALUE){
            if(Module32FirstW(snapshot,&entry))do{
                QString name=QString::fromWCharArray(entry.szModule);modules<<name;
                auto path=QDir::fromNativeSeparators(QString::fromWCharArray(entry.szExePath));
                modulePaths[name]=path;
                if(name.startsWith("Qt6",Qt::CaseInsensitive)||name=="qwindows.dll")
                    check(path.startsWith(runtime+"/",Qt::CaseInsensitive),"Qt must load from the selected runtime");
                python=python||name.startsWith("python",Qt::CaseInsensitive)||name.startsWith("pyside",Qt::CaseInsensitive);
            }while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);
        }
        check(!python,"runtime imported Python");report["pythonLoaded"]=python;
        report["loadedModules"]=QJsonArray::fromStringList(modules);
        report["loadedModulePaths"]=modulePaths;report["runtimeDirectory"]=runtime;
        report["qtBundled"]=runtime.compare(QDir::cleanPath(QCoreApplication::applicationDirPath()+"/runtime"),Qt::CaseInsensitive)==0;
        report["startupMemory"]=memory();
        if(params.value("probeMemory").toBool()){
            app.m_mouse=[] {return QPoint(100,100);};
            app.m_readClipboard=[] {return QString("Hello world");};
            QElapsedTimer elapsed;elapsed.start();app.textFlow("clip");
            check(until([&]{return !app.m_wordBackend.values.value("loading").toBool();}),"memory probe clipboard translation");
            report["firstWordMs"]=double(elapsed.elapsed());
            report["wordMemory"]=memory();app.closeWord();
            QImage captureFixture(820,380,QImage::Format_RGB32);captureFixture.fill(Qt::white);
            {QPainter draw(&captureFixture);QFont font("Segoe UI");font.setPixelSize(24);draw.setFont(font);draw.setPen(Qt::black);draw.drawText(20,60,"Screenshot translation");}
            app.openPin(captureFixture,{100,100,820,380});app.recognizePin();
            check(until([&]{return !app.m_busy&&!app.m_blocks.isEmpty();},15000),"memory probe screenshot OCR");
            report["screenshotMemory"]=memory();app.closePin();
            app.showSettings();pump(200);report["settingsMemory"]=memory();app.configAction("close",{});
            pump(500);report["closedMemory"]=memory();
            // Use the real idle delay, without touching the Owner desktop.
            pump(24000);report["idleMemory"]=memory();
            report["idleWordEngineRetained"]=bool(app.m_word.engine);
            check(!app.m_word.engine&&!app.m_config.engine&&!app.m_bar.engine,"idle destroys unused QML engines");
            check(app.m_original.isNull()&&app.m_translated.isNull()&&!app.m_raw,"idle frees capture and input resources");
            check(app.m_trimCount->load()>0,"idle returns resident pages to Windows");
            report["idleTrimCount"]=int(app.m_trimCount->load());
            elapsed.restart();app.dispatch({},"hotkey",{{"key","Alt+Shift+C"}});
            check(until([&]{return app.m_word.window&&app.m_word.window->isVisible();}),"word reopens after idle");
            report["reopenWindowMs"]=double(elapsed.elapsed());
            check(until([&]{return !app.m_wordBackend.values.value("loading").toBool();}),"word translates after idle");
            report["reopenTranslationMs"]=double(elapsed.elapsed());app.closeWord();
            // A pinned popup must survive idle cleanup, including after sleep.
            app.openWord("clip",{100,100});app.wordAction("pin",{});app.translateWord("Hello");
            check(until([&]{return !app.m_wordBackend.values.value("loading").toBool();}),"pinned memory fixture");
            auto live=app.m_word.window;auto trims=app.m_trimCount->load();app.m_idleSince=GetTickCount64()-21000;app.reclaimIdle();pump(300);
            check(app.m_word.window==live&&live->isVisible()&&app.m_trimCount->load()==trims,"pinned popup prevents idle release and trim");
            app.closeWord();app.m_holdAfterResume=true;app.m_idleSince=GetTickCount64()-21000;app.reclaimIdle();pump(300);
            check(app.m_word.engine&&app.m_trimCount->load()==trims,"sleep resume holds trimming until next use");
            app.used();check(!app.m_holdAfterResume,"next use releases sleep hold");
            report["idlePreservesPinnedAndResume"]=true;
            report["passed"]=true;atomicJson(out+"/report.json",report);ExitProcess(0);
        }
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
        check(app.m_bar.window->width()<=460&&app.m_bar.window->height()==32,"word-sized capture keeps full compact toolbar");
        report["compactToolbarSize"]=QJsonArray{app.m_bar.window->width(),app.m_bar.window->height()};
        app.m_bar.window->grabWindow().save(out+"/toolbar-compact.png");
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
        auto screenshotProvider=app.m_bar.window->findChild<QQuickItem*>("toolbar-provider");
        check(screenshotProvider&&screenshotProvider->isVisible()&&screenshotProvider->property("text").toString()=="渠道：translate.js","screenshot shows actual service inline");
        app.pinAction("text",{});pump(30);
        check(app.m_text.window->findChild<QQuickItem*>("text-provider")->property("text").toString()=="渠道：translate.js","full translation shows service");
        const auto savedTranslation=app.m_pinBackend.values["translatedText"];
        app.m_pinBackend.update({{"translatedText",QStringLiteral("短译文")}});pump(30);
        const auto shortTextSize=app.m_text.window->size();
        app.m_pinBackend.update({{"translatedText",QStringLiteral("这是一段需要完整显示的长译文，窗口会自动适应文字长度，并限制最大宽度与高度。\n").repeated(100)}});pump(70);
        auto textWindow=app.m_text.window;
        auto textWork=textWindow->screen()->availableGeometry();
        check(textWindow->width()>shortTextSize.width()&&textWindow->height()>shortTextSize.height(),"full translation grows in both dimensions");
        check(textWindow->width()<=std::min(720,int(textWork.width()*.8))&&textWindow->height()<=std::min(720,int(textWork.height()*.8))&&
            textWork.contains(textWindow->geometry()),"full translation respects monitor and size limits");
        auto textScroll=textWindow->findChild<QQuickItem*>("text-scroll");
        check(textScroll->property("contentHeight").toReal()>textScroll->height(),"full translation overflow scrolls");
        auto textBar=textWindow->findChild<QQuickItem*>("text-scrollbar");
        check(textBar->isVisible()&&textBar->x()>textWindow->width()-60&&textBar->y()>=54,"full translation scrollbar stays beside the body");
        textWindow->grabWindow().save(out+"/text-adaptive-limit.png");
        app.m_pinBackend.update({{"translatedText",QStringLiteral("短译文")}});pump(40);
        check(textWindow->size()==shortTextSize,"full translation shrinks after short result");
        app.m_pinBackend.update({{"translatedText",savedTranslation}});pump(30);
        report["textAdaptiveGeometry"]=true;
        app.m_text.window->hide();
        check(app.m_pin->pictureRect().size()==fixture.size(),"translation pixel stability");
        auto pinOrigin=app.m_pin->pictureRect().topLeft();RECT suggestion{20,20,600,300};
        UINT dpi=GetDpiForWindow(app.m_pin->hwnd());
        SendMessageW(app.m_pin->hwnd(),WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&suggestion));
        check(app.m_pin->pictureRect()==QRect(pinOrigin,fixture.size()),"DPI change preserves physical image");
        SendMessageW(app.m_pin->hwnd(),WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&suggestion));
        report["dpiPixelStable"]=true;
        app.m_translated.save(out+"/translation.png");
        app.m_bar.window->grabWindow().save(out+"/toolbar.png");
        check(app.m_bar.window->width()<=520&&app.m_bar.window->height()==32,"large capture keeps full compact toolbar and readable provider");
        report["toolbarSize"]=QJsonArray{app.m_bar.window->width(),app.m_bar.window->height()};
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
        app.m_pin->setPicture(fixture.copy(0,0,180,80),{});app.place();pump(60);
        for(auto id:{"source","swap","target","compare","provider","copy","retry","text","settings","close"}){
            auto item=app.m_bar.window->findChild<QQuickItem*>("toolbar-"+QString(id));
            check(item&&item->isVisible(),"every original toolbar control remains directly visible");
            auto at=item->mapToScene({0,0});
            check(at.x()>=0&&at.y()>=0&&at.x()+item->width()<=app.m_bar.window->width()&&at.y()+item->height()<=app.m_bar.window->height(),"toolbar control stays inside bar");
        }
        auto sourceMenu=app.m_bar.window->findChild<QQuickWindow*>("source-menu");check(sourceMenu,"source menu");
        auto sourceOpened=std::make_shared<bool>(false);
        QObject::connect(sourceMenu,&QWindow::visibleChanged,sourceMenu,[sourceOpened](bool visible){if(visible)*sourceOpened=true;});
        click(app.m_bar.window,app.m_bar.window->findChild<QQuickItem*>("toolbar-source"));
        check(*sourceOpened,"small capture opens source language directly");sourceMenu->hide();
        click(app.m_bar.window,app.m_bar.window->findChild<QQuickItem*>("toolbar-settings"));
        pump(250);check(app.m_config.window&&app.m_config.window->isVisible(),"settings reachable directly from toolbar");
        report["toolbarAllControlsVisible"]=true;
        check(app.m_configBackend.values.value("home").toBool(),"settings opens on home");
        check(app.m_config.window->findChild<QQuickItem*>("test-all")->isVisible(),"test all is visible on home");
        auto queueIds=[&]{QStringList ids;for(auto v:app.m_configBackend.values.value("queue").toList())ids<<v.toMap().value("provider").toString();return ids;};
        auto originalQueue=queueIds();check(originalQueue.size()==providers.size()&&originalQueue.first()=="zvo","home shows every service with preferred first");
        app.m_config.window->grabWindow().save(out+"/settings-home-initial.png");
        auto moved=originalQueue[2];
        click(app.m_config.window,visualItem(app.m_config.window->contentItem(),"queue-up-"+moved));
        check(queueIds()[1]==moved,"move up changes backup order");
        dragQueue(app.m_config.window,visualItem(app.m_config.window->contentItem(),"queue-drag-"+moved),80);
        check(queueIds()==originalQueue,"drag restores actual backup order");
        report["manualQueueControls"]=true;
        auto modes=app.m_config.window->findChild<QQuickItem*>("routing-modes");
        click(app.m_config.window,visualItem(modes,"choice-1"));
        check(app.m_routingDraft.value("mode")=="auto","automatic mode is selectable");
        check(!visualItem(app.m_config.window->contentItem(),"queue-up-"+moved)->isVisible(),"auto order cannot be manually changed");
        app.configAction("routingMode","manual");pump(30);
        app.m_config.window->grabWindow().save(out+"/settings.png");
        app.configSelect("ai");pump(30);
        app.m_config.window->grabWindow().save(out+"/settings-ai.png");
        check(!app.m_config.window->findChild<QQuickItem*>("test-all")->isVisible(),"test all moved out of provider footer");
        for(auto id:{"ai-key","ai-endpoint","ai-model-0"}){
            auto item=visualItem(app.m_config.window->contentItem(),QString::fromLatin1(id));
            report[QString("control-")+id]=QJsonObject{{"exists",bool(item)},{"visible",item&&item->isVisible()}};
            check(item&&item->isVisible(),"multi-channel AI fields are visible");
        }
        check(providers.count("ai")==1&&!providers.contains("openai")&&!providers.contains("deepseek"),"AI has one exposed channel");
        app.m_config.window->grabWindow().save(out+"/settings-ai.png");
        app.configSelect("youdao");pump(30);
        check(app.m_config.window->findChild<QQuickItem*>("appId")->isVisible(),"Youdao application ID is editable");
        app.m_config.window->grabWindow().save(out+"/settings-youdao.png");
        app.configSelect("ecdict");pump(30);
        check(!app.m_config.window->findChild<QQuickItem*>("apiKey")->isVisible()&&!app.m_config.window->findChild<QQuickItem*>("endpoint")->isVisible(),"offline dictionary needs no network configuration");
        check(!app.m_configBackend.values.value("dictionaryReady").toBool(),"fresh package has no dictionary");
        app.m_config.window->grabWindow().save(out+"/settings-ecdict-missing.png");
        auto dictionarySource=app.m_dictionarySource;
        auto dictionaryBase=params.value("probeDictionaryEndpoint").toString();
        check(dictionaryBase.startsWith("http://127.0.0.1:"),"dictionary fixture is local");
        app.m_dictionarySource.url=dictionaryBase+"/dictionary-truncated";
        click(app.m_config.window,app.m_config.window->findChild<QQuickItem*>("dictionary-download"));
        check(until([&]{return !app.m_dictionaryCancel;},5000),"bad download finishes");
        check(!app.m_dictionaryMessage.isEmpty()&&!dictionaryAvailable(app.m_services->directory()),"incomplete download is not enabled");
        app.m_dictionarySource.url=dictionaryBase+"/dictionary-slow";
        click(app.m_config.window,app.m_config.window->findChild<QQuickItem*>("dictionary-download"));
        check(until([&]{return app.m_dictionaryReceived>0;},5000),"download reports streamed progress");
        app.m_config.window->grabWindow().save(out+"/settings-ecdict-downloading.png");
        click(app.m_config.window,app.m_config.window->findChild<QQuickItem*>("dictionary-cancel"));
        check(until([&]{return !app.m_dictionaryCancel;},5000),"download cancels promptly");
        check(!dictionaryAvailable(app.m_services->directory()),"cancel does not enable partial dictionary");
        app.m_dictionarySource.url=dictionaryBase+"/dictionary-redirect";
        click(app.m_config.window,app.m_config.window->findChild<QQuickItem*>("dictionary-download"));
        auto downloadToken=app.m_dictionaryCancel;app.configAction("downloadDictionary",{});
        check(app.m_dictionaryCancel==downloadToken,"duplicate click does not start another download");
        app.configAction("close",{});pump(10);app.showSettings();app.configSelect("ecdict");
        check(until([&]{return !app.m_dictionaryCancel;},15000),"download finishes across settings close and reopen");
        check(app.m_dictionaryMessage.isEmpty()&&app.m_configBackend.values.value("dictionaryReady").toBool(),"downloaded dictionary becomes ready");
        check(!app.m_config.window->findChild<QQuickItem*>("dictionary-download")->isEnabled(),"installed dictionary is not downloaded again");
        app.m_dictionarySource=dictionarySource;
        report["dictionaryDownloadUi"]=true;
        app.m_config.window->grabWindow().save(out+"/settings-ecdict.png");
        report["newProviderSettings"]=true;app.configSelect("zvo");app.configAction("home",{});pump(30);
        auto testAll=app.m_config.window->findChild<QQuickItem*>("test-all");click(app.m_config.window,testAll);
        report["batchAfterClick"]=QJsonObject{{"count",int(app.m_tests.size())},{"running",app.m_testRunning},{"queued",int(app.m_testQueue.size())},
            {"message",app.m_configBackend.values.value("message").toString()}};
        check(app.m_batch,"batch button starts service tests");
        check(until([&]{return app.m_testRunning==0&&app.m_testQueue.isEmpty();},25000),"batch tests finish");
        check(app.m_tests.size()==providers.size(),"batch covers every provider");
        report["batchResults"]=QJsonValue::fromVariant(app.m_configBackend.values.value("testResults"));
        app.m_config.window->grabWindow().save(out+"/settings-tested.png");
        app.configAction("routingMode","auto");pump(40);
        app.m_config.window->grabWindow().save(out+"/settings-auto.png");
        app.appearance({{"theme","dark"}});pump(40);
        app.m_config.window->grabWindow().save(out+"/settings-dark.png");
        app.m_config.window->resize(720,520);pump(40);
        app.m_config.window->grabWindow().save(out+"/settings-small.png");
        check(app.m_config.window->findChild<QQuickItem*>("test-all")->isVisible(),"small settings retains test all");
        app.appearance({{"theme","light"}});
        report["settingsHome"]=true;report["automaticQueueControls"]=true;
        app.configSelect("zvo");auto existingEndpoint=app.m_drafts.value("zvo").toObject().value("endpoint").toString();
        app.configAction("endpoint",existingEndpoint);
        check(app.m_tests["zvo"].value("status")=="success","unchanged fields preserve test results");
        app.configAction("endpoint",existingEndpoint+"?edited");
        check(!app.m_tests.contains("zvo"),"edited configuration invalidates old test result");
        app.configAction("endpoint",existingEndpoint);
        report["activeMemory"]=memory();
        auto preferred=app.m_settings.value("provider");app.configSelect("deepl");app.configAction("save",{});
        check(app.m_settings.value("provider")==preferred,"save doesn't switch preferred provider");
        check(app.m_services->routing().value("mode")=="auto","saved mode drives translation services");
        app.showSettings();pump(30);check(app.m_configBackend.values.value("home").toBool()&&app.m_routingDraft.value("mode")=="auto","reopening keeps saved mode and starts on home");
        app.configAction("routingMode","manual");app.configAction("close",{});
        check(app.m_services->routing().value("mode")=="auto","cancel does not save routing edits");
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
        // Read a real Windows EDIT selection through an explicit HWND on the
        // private desktop, never through the Owner's focused element.
        auto parent=CreateWindowExW(0,L"STATIC",L"Selection fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            500,300,400,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        auto edit=CreateWindowExW(0,L"EDIT",L"Hello world",WS_CHILD|WS_VISIBLE|ES_MULTILINE,
            20,20,340,70,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(parent&&edit,"native selection fixture");
        SendMessageW(edit,EM_SETSEL,0,5);
        struct ReadResult {Selection selection;QString error;std::atomic_bool ready=false;};
        auto read=std::make_shared<ReadResult>();
        std::thread([read,edit]{
            try{read->selection=isolatedSelection(POINT{},edit,std::make_shared<std::atomic_bool>(false));}catch(const std::exception& e){read->error=e.what();}
            read->ready=true;
        }).detach();
        check(until([&]{return read->ready.load();},6000),"real Windows selection read completes");
        check(read->error.isEmpty()&&read->selection.text=="Hello"&&!read->selection.area.isEmpty(),"real Windows selection text and bounds");
        report["wordIsolatedSelection"]=true;
        // Force a native provider to stall without running its owning thread's
        // message loop. The worker must exit, and the next read must succeed.
        auto stalled=std::make_shared<ReadResult>();
        std::thread([stalled,edit]{
            try{stalled->selection=isolatedSelection(POINT{},edit,std::make_shared<std::atomic_bool>(false),40);}catch(const std::exception& e){stalled->error=e.what();}
            stalled->ready=true;
        }).detach();
        QThread::msleep(1000);
        check(stalled->ready.load()&&!stalled->error.isEmpty(),"stalled selection worker is reaped within its deadline");
        auto recovered=std::make_shared<ReadResult>();
        std::thread([recovered,edit]{
            try{recovered->selection=isolatedSelection(POINT{},edit,std::make_shared<std::atomic_bool>(false));}catch(const std::exception& e){recovered->error=e.what();}
            recovered->ready=true;
        }).detach();
        check(until([&]{return recovered->ready.load();},4000)&&recovered->error.isEmpty()&&recovered->selection.text=="Hello","selection works after a stalled provider");
        report["wordSelectionTimeoutRecovery"]=true;
        DestroyWindow(parent);report["wordRealEditSelection"]=true;
        // Production hotkey routes, actual QML windows and Windows OCR. Only
        // input text/cursor/pixels are injected; never alter the Owner clipboard.
        app.m_settings["provider"]="google";
        app.m_mouse=[] {return QPoint(400,180);};
        app.m_readClipboard=[] {return QString("Hello + 100%");};
        app.dispatch({},"hotkey",{{"key","Alt+Shift+C"}});
        check(app.m_word.window&&app.m_word.window->isVisible(),"clipboard hotkey opens popup immediately");
        check(app.m_word.window->flags().testFlag(Qt::WindowDoesNotAcceptFocus),"word popup preserves selection focus");
        check(until([&]{return !app.m_wordBackend.values["loading"].toBool();}),"clipboard popup translation");
        check(app.m_wordBackend.values["translatedText"].toString()=="你好 + 100%","clipboard popup meaning");
        editableSource(app.m_word.window,"Hello + 100%");
        check(!app.m_word.window->findChild<QQuickItem*>("word-submit-edit")&&!app.m_word.window->findChild<QQuickItem*>("word-cancel-edit"),"word popup has no footer edit buttons");
        auto restore=app.m_word.window->findChild<QQuickItem*>("word-restore");
        hoverItem(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        check(!restore->isVisible(),"hovering unchanged source keeps restore hidden");
        auto sourceFont=app.m_word.window->findChild<QQuickItem*>("word-editor")->property("font").value<QFont>().family();
        check(sourceFont=="Segoe UI","word popup uses Windows body font");
        report["wordFontFamily"]=sourceFont;
        check(app.m_word.window->width()<=340&&app.m_word.window->height()<220,"word popup is compact");
        app.m_word.window->grabWindow().save(out+"/word-clipboard.png");
        report["wordClipboard"]=true;
        {
        const auto compactState=app.m_wordBackend.values;
        const auto compactSize=app.m_word.window->size();
        const QString paragraph=QStringLiteral("Long text should expand this translation window while keeping the toolbar visible. ");
        const QString translation=QStringLiteral("长文本会自动调整窗口宽高，达到上限后可以滚动查看完整译文，操作按钮始终可见。");
        auto word=app.m_word.window;
        auto bounded=[&](QQuickWindow* window){
            const auto work=window->screen()->availableGeometry();
            return window->width()<=std::min(720,int(work.width()*.8))&&
                window->height()<=std::min(720,int(work.height()*.8))&&work.contains(window->geometry());
        };
        app.m_wordBackend.update({{"sourceText",paragraph.repeated(3)},{"translatedText",translation.repeated(6)}});pump(80);
        check(word->width()>compactSize.width()&&word->height()>compactSize.height(),"paragraph expands both word window dimensions");
        auto mediumSize=word->size();word->grabWindow().save(out+"/word-adaptive-paragraph.png");
        app.m_wordBackend.update({{"sourceText",paragraph.repeated(90)},{"translatedText",translation.repeated(200)}});pump(80);
        auto longScroll=word->findChild<QQuickItem*>("word-scroll");
        check(bounded(word)&&word->width()>=mediumSize.width()&&word->height()>=mediumSize.height(),"huge text stops at screen-aware width and height caps");
        check(longScroll->property("contentHeight").toReal()>longScroll->height(),"huge text remains scrollable");
        auto wordBar=word->findChild<QQuickItem*>("word-scrollbar");
        check(wordBar->isVisible()&&wordBar->x()>word->width()-60&&wordBar->y()>=44,"word scrollbar stays beside the body");
        longScroll->setProperty("contentY",longScroll->property("contentHeight").toReal()-longScroll->height());pump(30);
        check(longScroll->property("contentY").toReal()>0&&word->findChild<QQuickItem*>("word-close")->isVisible(),"scrolling long translation preserves close control");
        longScroll->setProperty("contentY",0);word->grabWindow().save(out+"/word-adaptive-limit.png");
        for(auto screen:QGuiApplication::screens()){
            const auto work=screen->availableGeometry();
            app.m_wordAnchor=screen->geometry().topLeft()+((work.bottomRight()-QPoint(12,12))-screen->geometry().topLeft())*screen->devicePixelRatio();
            app.placeWord();pump(40);
            check(word->screen()==screen&&bounded(word),"large word popup fits each monitor at the bottom-right edge");
        }
        word->setProperty("availableArea",QRect(0,0,640,480));pump(50);
        check(word->width()<=512&&word->height()<=384,"small work area lowers both popup limits");
        word->setProperty("availableArea",word->screen()->availableGeometry());pump(30);
        app.m_wordBackend.update(compactState);pump(60);
        check(word->size()==compactSize&&longScroll->property("contentY").toReal()==0,"short result restores compact size and scroll origin");
        app.m_wordPinned=true;
        word->setPosition(word->screen()->availableGeometry().bottomRight()-QPoint(word->width()+4,word->height()+4));
        app.m_wordBackend.update({{"translatedText",translation.repeated(200)}});pump(60);
        check(bounded(word)&&word->width()>compactSize.width(),"pinned translation also grows without leaving the screen");
        const auto pinnedPosition=word->position();
        app.m_wordBackend.update(compactState);pump(60);
        check(word->position()==pinnedPosition,"shrinking pinned content preserves its position");
        app.m_wordPinned=false;app.m_wordAnchor=QPoint(400,180);app.placeWord();pump(30);
        report["wordAdaptiveGeometry"]=true;
        }
        auto readableHeight=app.m_word.window->height();
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        check(!app.m_wordEditing&&!app.m_word.window->flags().testFlag(Qt::WindowDoesNotAcceptFocus),"clicking source enables keyboard focus without marking a clean draft dirty");
        check(app.m_word.window->height()==readableHeight,"focusing source does not resize the popup");
        typeDraft(app.m_word.window,"History");
        check(app.m_wordEditing,"first direct text change marks the draft dirty");
        key(app.m_word.window,Qt::Key_Escape);
        check(!app.m_wordEditing&&app.m_wordSource=="Hello + 100%","cancel discards draft without replacing source");
        editableSource(app.m_word.window,"Hello + 100%");
        typeDraft(app.m_word.window,"History");auto restoreEpoch=app.m_wordEpoch;
        hoverItem(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-provider"));
        check(!restore->isVisible()||restore->opacity()==0,"edited source hides restore when pointer leaves");
        hoverItem(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        check(restore->isVisible()&&restore->opacity()>0,"edited source shows restore only on hover");
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-restore"));pump(220);
        check(app.m_wordEpoch==restoreEpoch&&!app.m_wordEditing,"restore icon cancels draft without translating");
        check(!restore->isVisible(),"restoring source hides restore icon immediately");
        report["wordRestoreHoverOnly"]=true;
        editableSource(app.m_word.window,"Hello + 100%");
        typeDraft(app.m_word.window,QString("Long editable source line\n").repeated(24));pump(30);
        auto editor=app.m_word.window->findChild<QQuickItem*>("word-editor");
        auto scroller=app.m_word.window->findChild<QQuickItem*>("word-scroll");
        auto caretBottom=editor->mapToScene(editor->property("cursorRectangle").toRectF().bottomLeft());
        auto top=scroller->mapToScene(QPointF()).y();
        check(caretBottom.y()>=top&&caretBottom.y()<=top+scroller->height()+2,"long source editing keeps caret visible");
        typeDraft(app.m_word.window,"Settings");
        key(app.m_word.window,Qt::Key_Return,Qt::ShiftModifier);
        check(app.m_wordEditing&&app.m_word.window->findChild<QQuickItem*>("word-editor")->property("text").toString()=="Settings\n","Shift Enter inserts a line without translating");
        typeDraft(app.m_word.window,"Settings");
        QInputMethodEvent composing("设",{});QCoreApplication::sendEvent(app.m_word.window,&composing);pump(10);
        auto composingEpoch=app.m_wordEpoch;
        QMetaObject::invokeMethod(app.m_word.window,"submitEditing");
        check(app.m_word.window->findChild<QQuickItem*>("word-editor")->property("inputMethodComposing").toBool()&&
            app.m_wordEpoch==composingEpoch,"IME composition cannot submit partial text");
        QInputMethodEvent stopComposing;QCoreApplication::sendEvent(app.m_word.window,&stopComposing);
        check(app.m_word.window->findChild<QQuickItem*>("word-meaning")->isVisible()&&
            app.m_word.window->findChild<QQuickItem*>("word-copy")->isEnabled(),"draft editing retains the previous translation and copy action");
        app.m_word.window->grabWindow().save(out+"/word-edit.png");
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-provider"));
        check(until([&]{return app.m_wordBackend.values["translatedText"].toString()=="设置";}),"edited clipboard source is translated");
        check(!app.m_wordEditing&&app.m_wordSource=="Settings"&&!app.m_word.window->flags().testFlag(Qt::WindowDoesNotAcceptFocus),"submit keeps the source keyboard editable");
        editableSource(app.m_word.window,"Settings");
        auto cleanEpoch=app.m_wordEpoch;
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-provider"));pump(220);
        check(app.m_wordEpoch==cleanEpoch,"leaving unchanged source does not repeat translation");
        report["wordAutoTranslateOnBlur"]=true;
        typeDraft(app.m_word.window,"History");key(app.m_word.window,Qt::Key_Return);
        check(until([&]{return app.m_wordBackend.values["translatedText"].toString()=="历史";}),"source can be edited again immediately after submission");
        editableSource(app.m_word.window,"History");
        report["wordDefaultEditable"]=true;
        report["wordEditableClipboard"]=true;
        check(until([&]{return app.m_raw&&app.m_raw->m_hwnd.load();}),"word raw input");
        app.m_raw->m_read=&readRaw;app.m_raw->m_cursor=&cursor;app.m_wordOpened=GetTickCount()-500;
        rawPoint={-30000,-30000};rawFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
        app.m_settings["closeOnOutsideClick"]=false;
        PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);
        check(until([&]{return !app.m_word.window->isVisible();},300),"unpinned word closes independently of screenshot setting");
        auto originalReader=app.m_readSelection;
        app.m_readSelection=[](POINT,HWND,Cancel){return Selection{};};
        for(const auto& mode:QStringList{"clip","ocr","jsel"}){
            app.openWord(mode,{400,180});app.translateWord("Hello + 100%");
            check(until([&]{return !app.m_wordBackend.values.value("loading").toBool();}),"outside close fixture translates");
            check(!app.m_wordPinned&&!app.m_wordBackend.values.value("pinned").toBool(),"every popup opens unpinned");
            QMetaObject::invokeMethod(app.m_word.window,"prepareMove");
            auto moved=app.m_word.window->position()+QPoint(80,30);app.m_word.window->setPosition(moved);
            app.translateWord("Settings");
            check(until([&]{return !app.m_wordBackend.values.value("loading").toBool();}),"moved unpinned popup translates");
            check(!app.m_wordPinned&&!app.m_followSelection.isActive()&&app.m_word.window->position()==moved,"title move preserves position without pinning or following");
            check(until([&]{return app.m_raw&&app.m_raw->m_hwnd.load();}),"outside close raw input ready");
            app.m_raw->m_read=&readRaw;app.m_raw->m_cursor=&cursor;
            RECT bounds{};GetWindowRect(HWND(app.m_word.window->winId()),&bounds);
            rawPoint={(bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2};app.m_wordOpened=GetTickCount()-500;
            PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);pump(30);
            check(app.m_word.window->isVisible(),"internal clicks keep unpinned popup open");
            click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-pin"));
            check(app.m_wordPinned,"only pin button pins popup");
            rawPoint={-30000,-30000};app.m_wordOpened=GetTickCount()-500;
            PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);pump(30);
            check(app.m_word.window->isVisible(),"explicitly pinned popup survives outside click");
            click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-pin"));
            check(!app.m_wordPinned,"pin button unpins popup");
            if(mode=="clip"){
                app.m_wordBackend.update({{"providerNote",QString("用于检查悬浮提示的外部点击不会阻挡自动关闭。").repeated(4)}});
                hoverItem(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-provider"));
                auto tip=app.m_word.window->findChild<QQuickWindow*>("provider-tip");
                check(tip&&until([&]{return tip->isVisible();}),"outside dismissal tooltip appears");
                RECT tipBounds{};GetWindowRect(HWND(tip->winId()),&tipBounds);GetWindowRect(HWND(app.m_word.window->winId()),&bounds);
                rawPoint={tipBounds.left+2,(tipBounds.top+tipBounds.bottom)/2};
                check(!PtInRect(&bounds,rawPoint),"tooltip click fixture is outside word popup");
            }
            PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);
            check(until([&]{return !app.m_word.window->isVisible();},300),"unpin immediately restores outside dismissal");
            rawPoint={-30000,-30000};
        }
        app.m_readSelection=originalReader;app.m_settings["closeOnOutsideClick"]=true;
        report["wordExplicitPinOnly"]=true;
        report["wordOutsideDismiss"]=true;
        auto retryReads=std::make_shared<std::atomic_int>(0);
        auto retryTargets=std::make_shared<std::atomic_bool>(true);
        auto originalTarget=HWND(quintptr(0x1234)); // used only by the injected reader
        app.m_readSelection=[retryReads,retryTargets,originalTarget](POINT point,HWND target,Cancel){
            int attempt=retryReads->fetch_add(1);
            if(attempt>0&&(target!=originalTarget||point.x!=400||point.y!=180))retryTargets->store(false);
            return attempt<4?Selection{}:Selection{"Settings",QRect(540,220,100,30)};
        };
        app.textFlow("jsel");
        check(until([&]{return !app.m_wordBackend.values["loading"].toBool();}),"empty selection displays retry");
        app.m_selectionTarget=originalTarget;
        auto retryPosition=app.m_word.window->position();
        auto retryAnchor=app.m_wordAnchor;
        for(int i=0;i<4;++i){
            // The mouse is over Retry now; it must not become the next source.
            auto onButton=app.m_word.window->position()+QPoint(36,140);
            app.m_mouse=[onButton]{return onButton;};
            auto button=app.m_word.window->findChild<QQuickItem*>("word-retry");
            check(button&&button->isVisible(),"retry button is available");
            click(app.m_word.window,button);
            check(app.m_word.window->position()==retryPosition,"retry loading does not move popup");
            check(until([&]{return !app.m_wordBackend.values["loading"].toBool();}),"retry completes");
            check(app.m_word.window->position()==retryPosition&&app.m_wordAnchor==retryAnchor,"repeated retry preserves original position and anchor");
        }
        check(retryReads->load()==5&&retryTargets->load()&&app.m_wordSource=="Settings","retry keeps original HWND and pointer until selection becomes available");
        app.m_word.window->grabWindow().save(out+"/word-retry-stable.png");
        report["wordRetryStable"]=true;report["wordRetryTarget"]=true;
        app.closeWord();app.m_mouse=[] {return QPoint(400,180);};
        auto readAttempts=std::make_shared<std::atomic_int>(0);
        auto readCancelled=std::make_shared<std::atomic_bool>(false);
        app.m_readSelection=[readAttempts,readCancelled](POINT,HWND,Cancel cancel){
            if(readAttempts->fetch_add(1)==0){
                QElapsedTimer wait;wait.start();
                while(!cancel->load()&&wait.elapsed()<2000)QThread::msleep(5);
                readCancelled->store(cancel->load());
                return Selection{"Stale selection",QRect(540,220,100,30)};
            }
            return Selection{"Settings",QRect(540,220,100,30)};
        };
        app.textFlow("jsel");
        check(until([&]{return readAttempts->load()==1;}),"selection read starts before replacement");
        app.closeWord();app.textFlow("jsel");
        check(until([&]{return app.m_wordBackend.values["translatedText"].toString()=="设置";}),"reopened selection starts after cancelled read without another hotkey");
        check(readCancelled->load()&&readAttempts->load()==2&&!app.m_selectionReading,"only the latest selection read completes");
        report["wordSelectionReplacement"]=true;app.closeWord();
        auto selectedIndex=std::make_shared<std::atomic_int>(0);
        app.m_readSelection=[selectedIndex](POINT,HWND,Cancel){
            return Selection{selectedIndex->load()==0?"Hello + 100%":"Settings",QRect(540,220,100,30)};
        };
        app.m_settings["selectionHotkey"]="Alt+Shift+S";
        app.dispatch({},"hotkey",{{"key","Alt+Shift+S"}});
        check(app.m_word.window->flags().testFlag(Qt::WindowDoesNotAcceptFocus),"reopening selection popup restores passive initial focus");
        check(until([&]{return !app.m_wordBackend.values["loading"].toBool();}),"selection popup translation");
        check(app.m_wordBackend.values["translatedText"].toString()=="你好 + 100%","selection source translated");
        check(app.m_wordSelection==QRect(540,220,100,30),"selection bounds position popup");
        editableSource(app.m_word.window,"Hello + 100%");
        app.m_word.window->grabWindow().save(out+"/word-selection.png");
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        typeDraft(app.m_word.window,"History");key(app.m_word.window,Qt::Key_Return);
        check(until([&]{return app.m_wordBackend.values["translatedText"].toString()=="历史";}),"selection source accepts direct corrections");
        report["wordEditableSelection"]=true;
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-pin"));
        check(app.m_wordPinned&&app.m_followSelection.isActive(),"pin enables selection following");
        auto movedWindow=app.m_word.window;
        auto oldPosition=movedWindow->position();
        SendMessageW(HWND(movedWindow->winId()),WM_ENTERSIZEMOVE,0,0);
        for(int i=0;i<8;++i){movedWindow->setPosition(oldPosition+QPoint(12*i,8*i));pump(35);}
        SendMessageW(HWND(movedWindow->winId()),WM_EXITSIZEMOVE,0,0);
        pump(650); // include cursor blinking and a native expose, not just a fresh scene grab
        auto frame=movedWindow->grabWindow();frame.save(out+"/word-moved-scene.png");
        check(movedWindow->color().alpha()==255&&!(GetWindowLongPtrW(HWND(movedWindow->winId()),GWL_EXSTYLE)&WS_EX_LAYERED),"word popup uses an opaque native backing store during moves");
        check(frame.pixelColor(frame.width()/2,frame.height()-6).alpha()==255&&
            movedWindow->findChild<QQuickItem*>("word-close")->isVisible()&&
            movedWindow->findChild<QQuickItem*>("word-provider")->isVisible(),"moved popup keeps card and actions");
        report["wordMoveRendering"]=true;
        auto fixedPosition=app.m_word.window->position();
        check(until([&]{return app.m_raw&&app.m_raw->m_hwnd.load();}),"pinned raw input");
        app.m_raw->m_read=&readRaw;app.m_raw->m_cursor=&cursor;app.m_wordOpened=GetTickCount()-500;
        PostMessageW(app.m_raw->m_hwnd,WM_INPUT,0,1);pump(30);
        check(app.m_word.window->isVisible(),"pinned popup survives external click");
        app.m_wordCancel->store(true); // a timed-out translation must not stop following
        selectedIndex->store(1);
        check(until([&]{return app.m_wordBackend.values["translatedText"].toString()=="设置";},4000),"pinned new selection auto replaces meaning");
        check(app.m_word.window->position()==fixedPosition,"pinned popup stays in position");
        app.m_word.window->grabWindow().save(out+"/word-pinned.png");
        report["wordPinnedSelection"]=true;
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        check(app.m_followSelection.isActive(),"clean editor focus does not stop pinned selection following");
        typeDraft(app.m_word.window,"草稿");selectedIndex->store(0);app.readSelection(true);pump(500);
        check(!app.m_followSelection.isActive()&&app.m_wordSource=="Settings"&&
            app.m_word.window->findChild<QQuickItem*>("word-editor")->property("text").toString()=="草稿","pinned following pauses while editing");
        hoverItem(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-restore"));
        check(app.m_followSelection.isActive(),"pinned following resumes after editing");
        check(until([&]{return app.m_wordSource=="Hello + 100%";}),"new selection is translated after editing");
        check(app.m_word.window->findChild<QQuickItem*>("word-editor")->property("text").toString()=="Hello + 100%","resumed selection updates the default editor");
        report["wordEditingPausesFollow"]=true;
        app.closeWord();check(!app.m_followSelection.isActive(),"closing stops follow polling");
        app.m_settings["selectionHotkey"]="";
        app.m_settings["provider"]="mymemory";app.textFlow("clip");pump(70);
        check(app.m_wordBackend.values["loading"].toBool(),"blocked word request loading");
        app.m_word.window->grabWindow().save(out+"/word-loading.png");
        auto oldWordCancel=app.m_wordCancel;
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
        typeDraft(app.m_word.window,"Settings");app.m_settings["provider"]="google";
        key(app.m_word.window,Qt::Key_Return);
        check(oldWordCancel->load(),"new word cancels old request");
        check(until([&]{return app.m_wordBackend.values["translatedText"].toString()=="设置";}),"latest word result wins");
        pump(100);check(app.m_wordSource=="Settings","stale result cannot overwrite current word");
        app.closeWord();
        app.m_settings["provider"]="mymemory";app.textFlow("clip");pump(70);auto closeCancel=app.m_wordCancel;timer.restart();
        click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-close"));
        check(!app.m_word.window->isVisible()&&closeCancel->load()&&!app.m_busy,"word close cancels without blocking screenshot");
        report["wordCloseWhileBlockedMs"]=double(timer.elapsed());report["wordCancellation"]=true;
        app.m_settings["provider"]="google";
        struct WordResult {QList<Block> words;QString error;std::atomic_bool ready=false;};
        auto recognized=std::make_shared<WordResult>();
        std::thread([recognized,fixture]{
            try{recognized->words=recognize(fixture,std::make_shared<std::atomic_bool>(false),false);}
            catch(const std::exception& e){recognized->error=e.what();}
            recognized->ready=true;
        }).detach();
        check(until([&]{return recognized->ready.load();},6000)&&recognized->error.isEmpty(),"background Windows word OCR");
        auto words=recognized->words;
        Block settingsWord;for(auto w:words)if(w.text=="Settings")settingsWord=w;
        check(!settingsWord.text.isEmpty(),"real OCR retains individual words");
        check(wordAt(words,settingsWord.box.center()).text=="Settings","word target by rectangle");
        check(wordAt(words,QPointF(800,350)).text.isEmpty(),"OCR blank area never picks remote word");
        auto wordPoint=settingsWord.box.center().toPoint()+QPoint(100,100);
        app.m_mouse=[wordPoint]{return wordPoint;};
        app.dispatch({},"hotkey",{{"key","Alt+Shift+O"}});
        check(until([&]{return app.m_word.window->isVisible()&&!app.m_wordBackend.values["loading"].toBool();},10000),"mouse OCR popup complete");
        check(app.m_wordSource=="Settings"&&app.m_wordBackend.values["translatedText"].toString()=="设置","mouse OCR translates just pointed word");
        app.m_word.window->grabWindow().save(out+"/word-ocr.png");
        report["wordMouseOcr"]=true;app.closeWord();
        Block sentenceStart,sentenceEnd;
        for(auto w:words){
            if(w.text=="Native")sentenceStart=w;
            if(w.text.startsWith("place"))sentenceEnd=w;
        }
        check(!sentenceStart.text.isEmpty()&&!sentenceEnd.text.isEmpty(),"OCR sentence fixture endpoints");
        QJsonArray sentenceAttempts;
        for(auto endpoint:{sentenceStart,sentenceEnd}){
            auto sentencePoint=endpoint.box.center().toPoint()+QPoint(100,100);
            app.m_mouse=[sentencePoint]{return sentencePoint;};
            app.dispatch({},"hotkey",{{"key","Alt+Shift+O"}});
            check(until([&]{return app.m_word.window->isVisible()&&!app.m_wordBackend.values["loading"].toBool();},10000),"pointer sentence OCR complete");
            sentenceAttempts.append(QJsonObject{{"endpoint",endpoint.text},{"source",app.m_wordSource},{"status",app.m_wordBackend.values["status"].toString()}});
            report["syntheticSentenceAttempts"]=sentenceAttempts;
            app.m_word.window->grabWindow().save(out+"/word-ocr-sentence.png");
            // The real recognizer may omit the tiny terminal dot at a different
            // crop offset. Require every sentence word, without extra content.
            auto sentence=app.m_wordSource;if(sentence.endsWith('.'))sentence.chop(1);
            check(sentence=="Native windows keep the capture in place","mouse OCR includes sentence from first or last word");
            check(app.m_wordBackend.values["translatedText"].toString()=="原生窗口保持截图的位置与尺寸。","mouse OCR translates sentence");
            check(app.m_wordSelection.left()==sentencePoint.x(),"phrase popup remains by pointer");
            app.closeWord();
        }
        report["wordMousePhraseOcr"]=true;
        QJsonArray spacingAttempts;
        for(auto expected:{QString("hi,i ma you baby"),QString("hi,i ma you ba by")}){
            QImage smallText(240,50,QImage::Format_RGB32);smallText.fill(Qt::white);
            QPainter draw(&smallText);QFont smallFont("Segoe UI");smallFont.setPixelSize(13);
            draw.setFont(smallFont);draw.setPen(QColor("#1B1B1B"));draw.drawText(16,30,expected);draw.end();
            app.m_capture=[smallText](const QRect& area){
                QImage image(area.size(),QImage::Format_RGB32);image.fill(Qt::white);QPainter p(&image);
                p.drawImage(QPoint(100,100)-area.topLeft(),smallText);return image;
            };
            app.m_mouse=[] {return QPoint(120,124);};
            app.dispatch({},"hotkey",{{"key","Alt+Shift+O"}});
            check(until([&]{return app.m_word.window->isVisible()&&!app.m_wordBackend.values["loading"].toBool();},10000),"small-text pointer OCR complete");
            spacingAttempts.append(QJsonObject{{"expected",expected},{"source",app.m_wordSource},{"status",app.m_wordBackend.values["status"].toString()}});
            report["syntheticSpacingAttempts"]=spacingAttempts;
            auto name=expected.endsWith("ba by")?"word-ocr-spaces":"word-ocr-baby";
            smallText.save(out+"/"+name+"-source.png");app.m_word.window->grabWindow().save(out+"/"+name+".png");
            check(app.m_wordSource==expected,"pointer OCR joins letters and preserves real word spaces");
            check(app.m_wordBackend.values["translatedText"].toString()=="翻译："+expected,"correct word spacing reaches translation service");
            app.closeWord();
        }
        report["wordMouseSpacingOcr"]=true;
        QJsonArray mixedAttempts;
        for(bool chineseMajority:{true,false}){
            QString prefix=chineseMajority?"这是中文说明，":"Now translate the ";
            QString minority=chineseMajority?"hello world":"中文";
            QString suffix=chineseMajority?" 是要翻译的英文。":" words in this sentence.";
            QImage mixedText(650,60,QImage::Format_RGB32);mixedText.fill(Qt::white);
            QPainter draw(&mixedText);QFont mixedFont("Microsoft YaHei UI");mixedFont.setPixelSize(18);
            draw.setFont(mixedFont);draw.setPen(QColor("#1B1B1B"));draw.drawText(16,32,prefix+minority+suffix);draw.end();
            mixedText.save(out+(chineseMajority?"/word-mixed-chinese-source.png":"/word-mixed-english-source.png"));
            app.m_capture=[mixedText,out](const QRect& area){
                QImage image(area.size(),QImage::Format_RGB32);image.fill(Qt::white);QPainter p(&image);
                p.drawImage(QPoint(100,100)-area.topLeft(),mixedText);p.end();
                image.save(out+"/word-mixed-capture.png");return image;
            };
            for(bool onMinority:{true,false}){
                int x=122+(onMinority?qRound(QFontMetricsF(mixedFont).horizontalAdvance(prefix)):0);
                app.m_mouse=[x] {return QPoint(x,125);};
                app.dispatch({},"hotkey",{{"key","Alt+Shift+O"}});
                check(until([&]{return app.m_word.window->isVisible()&&!app.m_wordBackend.values["loading"].toBool();},10000),"mixed-language OCR complete");
                mixedAttempts.append(QJsonObject{{"chineseMajority",chineseMajority},{"onMinority",onMinority},{"source",app.m_wordSource}});
                report["syntheticMixedAttempts"]=mixedAttempts;
                if(onMinority)check(app.m_wordSource==minority,"minority pointer excludes surrounding language");
                else check(app.m_wordSource.startsWith(prefix)&&app.m_wordSource.contains(minority)&&
                    app.m_wordSource.contains(suffix.trimmed().chopped(1)),"majority pointer includes complete mixed sentence");
                if(chineseMajority&&onMinority){
                    app.m_word.window->grabWindow().save(out+"/word-ocr-minority.png");
                    editableSource(app.m_word.window,app.m_wordSource);
                    click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-editor"));
                    typeDraft(app.m_word.window,"");
                    auto emptyEpoch=app.m_wordEpoch;
                    click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-provider"));pump(220);
                    check(app.m_wordEpoch==emptyEpoch,"empty edit cannot submit on blur");
                    typeDraft(app.m_word.window,"History");
                    click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-meaning"));
                    check(until([&]{return app.m_wordBackend.values["translatedText"].toString()=="历史";}),"edited OCR source is translated");
                    check(app.m_wordSource=="History"&&!app.m_wordEditing,"leaving source submits OCR correction");
                    editableSource(app.m_word.window,"History");
                    app.m_word.window->grabWindow().save(out+"/word-ocr-edited.png");
                    report["wordEditableOcr"]=true;
                    typeDraft(app.m_word.window,"Edited OCR draft");
                    auto closeEpoch=app.m_wordEpoch;
                    click(app.m_word.window,app.m_word.window->findChild<QQuickItem*>("word-close"));
                    pump(220);check(app.m_wordEpoch==closeEpoch+1,"closing edited popup cancels without submitting");
                    check(!app.m_word.window->isVisible()&&!app.m_wordEditing&&!app.m_followSelection.isActive(),"close while editing leaves no focus or follow state");
                }
                app.closeWord();
            }
        }
        report["wordMouseMixedOcr"]=true;
        QJsonArray wrapAttempts;
        for(bool chinese:{false,true}){
            QStringList lines=chinese?QStringList{"这是一段需要翻译的","连续换行中文内容。"}:
                QStringList{"Please translate this","whole sentence across","three lines."};
            QString expected=lines.join(chinese?QString():QString(" "));
            QImage wrapped(300,118,QImage::Format_RGB32);wrapped.fill(Qt::white);
            QPainter draw(&wrapped);QFont font(chinese?"Microsoft YaHei UI":"Segoe UI");font.setPixelSize(18);draw.setFont(font);draw.setPen(QColor("#1B1B1B"));
            for(int i=0;i<lines.size();++i)draw.drawText(16,32+i*27,lines[i]);draw.end();
            app.m_capture=[wrapped](const QRect& area){
                QImage image(area.size(),QImage::Format_RGB32);image.fill(Qt::white);QPainter p(&image);
                p.drawImage(QPoint(100,100)-area.topLeft(),wrapped);return image;
            };
            for(int i=0;i<lines.size();++i){
                app.m_mouse=[i]{return QPoint(124,125+i*27);};app.dispatch({},"hotkey",{{"key","Alt+Shift+O"}});
                check(until([&]{return app.m_word.window->isVisible()&&!app.m_wordBackend.values["loading"].toBool();},10000),"wrapped sentence OCR complete");
                wrapAttempts.append(QJsonObject{{"chinese",chinese},{"line",i},{"source",app.m_wordSource},{"expected",expected}});report["syntheticWrapAttempts"]=wrapAttempts;
                app.m_word.window->grabWindow().save(out+QString("/word-wrapped-%1-%2.png").arg(chinese).arg(i));
                check(app.m_wordSource==expected,"pointer OCR includes the complete wrapped sentence from each line");
                check(app.m_wordBackend.values["translatedText"].toString()=="翻译："+expected,"wrapped sentence reaches translation service");app.closeWord();
            }
        }
        report["wordMouseWrappedOcr"]=true;
        // Optional local screenshot fixtures exercise the complete hotkey ->
        // real Windows OCR -> local service -> popup path. Images stay local
        // and all capture/input hooks remain on this private desktop.
        QJsonArray localAttempts;int fixtureIndex=0;
        for(auto fixtureValue:params.value("probePointerFixtures").toArray()){
            auto test=fixtureValue.toObject();QImage source(test.value("image").toString());
            check(!source.isNull(),"local pointer fixture unavailable");
            app.m_capture=[source](const QRect& area){
                QImage image(area.size(),QImage::Format_RGB32);image.fill(Qt::white);QPainter p(&image);
                p.drawImage(QPoint(100,100)-area.topLeft(),source);return image;
            };
            int pointIndex=0;
            for(auto pointValue:test.value("points").toArray()){
                auto testPoint=pointValue.toObject();QPoint at(testPoint.value("x").toInt()+100,testPoint.value("y").toInt()+100);
                app.m_mouse=[at]{return at;};QElapsedTimer elapsed;elapsed.start();
                app.dispatch({},"hotkey",{{"key","Alt+Shift+O"}});
                check(until([&]{return app.m_word.window->isVisible()&&!app.m_wordBackend.values["loading"].toBool();},10000),"local screenshot pointer OCR complete");
                auto expected=testPoint.value("expected").toString();
                localAttempts.append(QJsonObject{{"fixture",fixtureIndex},{"point",pointIndex},{"source",app.m_wordSource},
                    {"expected",expected},{"translated",app.m_wordBackend.values["translatedText"].toString()},
                    {"ms",double(elapsed.elapsed())},{"status",app.m_wordBackend.values["status"].toString()}});
                report["localPointerAttempts"]=localAttempts;
                auto name=QString("/word-local-%1-%2.png").arg(fixtureIndex).arg(pointIndex);
                app.m_word.window->grabWindow().save(out+name);
                check(app.m_wordSource==expected,"local screenshot retains the complete pointed phrase");
                if(!expected.isEmpty())check(app.m_wordBackend.values["translatedText"].toString()=="翻译："+expected,"complete local screenshot phrase reaches service and popup");
                else check(!app.m_wordBackend.values["status"].toString().isEmpty(),"blank local screenshot reports no text");
                app.closeWord();++pointIndex;
            }++fixtureIndex;
        }
        if(fixtureIndex)report["wordLocalPixelOcr"]=true;
        app.m_readClipboard=[] {return QString();};app.textFlow("clip");
        check(!app.m_wordBackend.values["loading"].toBool()&&!app.m_wordBackend.values["status"].toString().isEmpty(),"empty clipboard has visible error");app.closeWord();
        // Hotkey uniqueness can span desktops in the window station. Use
        // probe-only chords rather than competing with the user's real keys.
        app.m_settings["shotHotkey"]="Ctrl+Alt+Shift+F23";
        app.m_settings["clipHotkey"]="Ctrl+Alt+Shift+F24";
        app.m_settings["wordOcrHotkey"]="";
        app.m_hostHotkeys=false;app.registerKeys();
        check(app.m_nativeKeys.size()==2,"native fallback registers both Windows hotkeys");
        PostMessageW(app.m_keyWindow,WM_HOTKEY,1,0);
        check(until([&]{return bool(app.m_selector);}),"WM_HOTKEY opens selector");app.closePin();
        app.m_settings["shotHotkey"]="Alt+Shift+F8";app.m_settings["clipHotkey"]="";app.registerKeys();
        check(app.m_nativeKeys.size()==1,"native hotkey configuration refresh");
        check(RegisterHotKey(app.m_keyWindow,20,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F23),"old hotkey released");
        UnregisterHotKey(app.m_keyWindow,20);
        report["nativeHotkeyFallback"]=true;
        // All word entry points share the actual result provider, not the
        // preferred provider. Use a real local-fallback request for each mode.
        app.appearance({{"theme","light"}});
        auto hoverProvider=[&](View& view,const char* name,const QString& snapshot){
            auto label=view.window->findChild<QQuickItem*>(name);
            auto tip=view.window->findChild<QQuickWindow*>("provider-tip");
            check(label&&tip,"provider hover target and separate tooltip exist");
            auto focus=GetForegroundWindow();
            auto point=label->mapToScene(QPointF(label->width()/2,label->height()/2));
            QMouseEvent move(QEvent::MouseMove,point,view.window->mapToGlobal(point.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(view.window,&move);
            check(until([&]{return tip->isVisible();},2000),"hover shows provider explanation");
            check(tip->property("text").toString().contains("恢复间隔"),"fallback tooltip explains recovery");
            check(GetForegroundWindow()==focus,"provider tooltip does not steal focus");
            check(tip->height()>32,"provider explanation is not clipped to compact toolbar");
            tip->grabWindow().save(out+"/"+snapshot+"-tip.png");
            QMouseEvent leave(QEvent::MouseMove,QPointF(1,1),view.window->mapToGlobal(QPoint(1,1)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(view.window,&leave);
            check(until([&]{return !tip->isVisible();}),"leaving provider hides explanation");
        };
        app.m_settings["provider"]="deepl";app.m_settings["fallbackEnabled"]=true;
        app.m_services->save({},{{"mode","manual"},{"order",QJsonArray{"google","zvo","ecdict"}}});
        for(const auto& mode:QStringList{"clip","ocr","jsel"}){
            app.openWord(mode,{100,100});app.translateWord("Hello + 100%");
            check(until([&]{return !app.m_wordBackend.values.value("loading").toBool();}),"fallback word result complete");
            auto label=app.m_word.window->findChild<QQuickItem*>("word-provider");
            check(label&&label->isVisible()&&label->property("text").toString()=="渠道：Google · 已降级","every word mode displays actual fallback provider");
            check(label->property("color").value<QColor>()==QColor("#9D5D00"),"fallback word service uses caution color");
            app.m_word.window->grabWindow().save(out+"/provider-"+mode+".png");
            hoverProvider(app.m_word,"word-provider","provider-"+mode);
            if(mode=="clip"){
                app.appearance({{"theme","dark"}});pump(30);
                check(label->property("color").value<QColor>()==QColor("#FCE100"),"fallback color follows dark theme");
                app.m_word.window->grabWindow().save(out+"/provider-clip-dark.png");
                app.appearance({{"theme","light"}});
            }
            app.m_settings["provider"]="google";app.translateWord("Hello + 100%");
            check(!app.m_wordBackend.values.value("providerFallback").toBool()&&app.m_wordBackend.values.value("providerNote").toString().isEmpty(),"new word request clears old fallback state");
            check(until([&]{return !app.m_wordBackend.values.value("loading").toBool();}),"preferred word result complete");
            check(label->property("text").toString()=="渠道：Google"&&label->property("color").value<QColor>()==QColor("#666666"),"successful preferred service restores default word color");
            check(!app.m_wordBackend.values.value("providerNote").toString().contains("恢复间隔"),"normal word result has no fallback explanation");
            app.m_settings["provider"]="deepl";app.closeWord();
        }
        app.openPin(fixture,{100,100,820,380});app.m_blocks={{"Hello + 100%",QRectF(20,20,180,32),24}};
        app.translatePin("en","zh",{"Hello + 100%"});
        check(until([&]{return !app.m_busy;}),"fallback screenshot result complete");
        check(app.m_bar.window->findChild<QQuickItem*>("toolbar-provider")->property("text").toString()=="渠道：Google · 已降级","screenshot displays actual fallback provider");
        check(app.m_bar.window->findChild<QQuickItem*>("toolbar-provider")->property("color").value<QColor>()==QColor("#9D5D00"),"screenshot fallback color");
        app.m_bar.window->grabWindow().save(out+"/provider-toolbar.png");
        hoverProvider(app.m_bar,"toolbar-provider","provider-toolbar");
        app.pinAction("text",{});pump(30);
        check(app.m_text.window->findChild<QQuickItem*>("text-provider")->property("color").value<QColor>()==QColor("#9D5D00"),"full translation fallback color");
        hoverProvider(app.m_text,"text-provider","provider-text");
        auto queryRows=app.results(app.m_data,"clip");
        check(queryRows.first().toObject().value("subtitle").toString().contains("渠道：Google · 已降级"),"keyword clipboard result displays actual fallback provider");
        app.m_settings["provider"]="google";app.translatePin("en","zh",{"Hello + 100%"});
        check(!app.m_pinBackend.values.value("providerFallback").toBool()&&app.m_pinBackend.values.value("providerNote").toString().isEmpty(),"new screenshot request clears fallback state");
        check(until([&]{return !app.m_busy;}),"preferred screenshot result complete");
        for(auto label:{app.m_bar.window->findChild<QQuickItem*>("toolbar-provider"),app.m_text.window->findChild<QQuickItem*>("text-provider")})
            check(label->property("text").toString()=="渠道：Google"&&label->property("color").value<QColor>()==QColor("#666666"),"preferred screenshot restores default color");
        report["providerFallbackAppearance"]=true;report["actualProviderLabels"]=true;app.closePin();
        // Exercise the real panel QML: debounce, editing, cancellation and independent lifetime.
        app.m_settings["provider"]="zvo";app.m_settings["fallbackEnabled"]=false;
        app.showPanel();pump(50);check(app.m_panel.window&&app.m_panel.window->isVisible(),"independent translation panel opens");
        auto panelInput=app.m_panel.window->findChild<QQuickItem*>("panel-input");check(panelInput,"panel input exists");
        app.panelAction("explain",false);typeDraft(app.m_panel.window,"hello","panel-input");
        check(until([&]{return app.m_panelBackend.values.value("translatedText").toString()=="翻译：hello";},4000),"panel automatically translates after typing");
        check(app.m_panelBackend.values.value("detected").toString()=="en","panel detects source language");
        typeDraft(app.m_panel.window,"newer input","panel-input");
        check(until([&]{return app.m_panelBackend.values.value("translatedText").toString()=="翻译：newer input";},4000),"panel latest input replaces result");
        app.m_panel.window->grabWindow().save(out+"/panel-translation.png");
        app.panelAction("autoTranslate",false);typeDraft(app.m_panel.window,"manual text","panel-input");pump(900);
        check(app.m_panelBackend.values.value("translatedText").toString().isEmpty(),"manual mode does not auto-submit");
        click(app.m_panel.window,app.m_panel.window->findChild<QQuickItem*>("panel-submit"));
        check(until([&]{return !app.m_panelBackend.values.value("translatedText").toString().isEmpty();}),"panel translate button works");
        // A cancelled request must never replace the newer input's result.
        app.m_settings["provider"]="mymemory";typeDraft(app.m_panel.window,"stale blocked text","panel-input");
        app.translatePanel();pump(80);auto pendingPanel=app.m_panelCancel;
        check(app.m_panelBackend.values.value("loading").toBool(),"panel blocked request starts");
        app.m_settings["provider"]="zvo";typeDraft(app.m_panel.window,"replacement text","panel-input");
        check(pendingPanel->load(),"new input cancels pending panel request");app.translatePanel();
        check(until([&]{return app.m_panelBackend.values.value("translatedText").toString()=="翻译：replacement text";},4000),"panel cancelled result cannot replace latest input");
        app.panelAction("explain",true);typeDraft(app.m_panel.window,"hello","panel-input");app.translatePanel();
        check(until([&]{return !app.m_panelBackend.values.value("definition").toString().isEmpty();},4000),"panel offline definition arrives independently");
        check(app.m_panelBackend.values.value("definitionProvider").toString().contains("ECDICT"),"panel uses enabled downloaded dictionary");
        app.appearance({{"theme","dark"}});pump(40);app.m_panel.window->grabWindow().save(out+"/panel-dark.png");
        app.m_panel.window->resize(600,520);pump(40);app.m_panel.window->grabWindow().save(out+"/panel-small.png");
        check(app.m_panel.window->findChild<QQuickItem*>("panel-submit")->isVisible(),"small panel retains translate button");
        app.appearance({{"theme","light"}});
        panelInput->setProperty("text",QString(10001,'x'));app.translatePanel();
        check(app.m_panelBackend.values.value("status").toString().contains("10,000"),"panel limits oversized input");
        app.panelAction("input","");app.translatePanel();check(!app.m_panelBackend.values.value("loading").toBool(),"empty panel stops work");
        app.m_settings["provider"]="mymemory";app.panelAction("explain",false);typeDraft(app.m_panel.window,"close while blocked","panel-input");
        app.translatePanel();pump(80);auto closingPanel=app.m_panelCancel;
        app.panelAction("close",{});pump(40);check(!app.m_panel.window&&closingPanel->load(),"panel closes and cancels pending work");
        app.showSettings();app.configSelect("ai");pump(30);
        auto initialChannels=app.m_configBackend.values.value("aiChannels").toList().size();
        app.aiAction("aiAdd",{});check(app.m_configBackend.values.value("aiChannels").toList().size()==initialChannels+1,"AI channel added");
        typeField(app.m_config.window,"ai-name","测试渠道");
        check(app.m_drafts.value("ai").toObject().value("channels").toArray().last().toObject().value("name")=="测试渠道","typed AI channel name updates draft");
        typeField(app.m_config.window,"ai-model-0","model-one");
        app.aiAction("aiModelAdd",{});app.aiAction("aiModelField",QVariantMap{{"index",1},{"key","name"},{"value","model-two"}});
        app.aiAction("aiModelMove",QVariantMap{{"index",1},{"to",0}});
        auto aiChannels=app.m_drafts.value("ai").toObject().value("channels").toArray();
        check(aiChannels.last().toObject().value("models").toArray()[0].toObject().value("name")=="model-two","AI model order changes");
        // Configure actual local endpoints and exercise every enabled AI model from the UI.
        auto aiEndpoint=params.value("probeDictionaryEndpoint").toString()+"/ai-test";
        app.aiAction("aiField",QVariantMap{{"key","endpoint"},{"value",aiEndpoint}});
        app.aiAction("aiField",QVariantMap{{"key","apiKey"},{"value","fixture-key"}});
        click(app.m_config.window,app.m_config.window->findChild<QQuickItem*>("test-current"));
        check(until([&]{return !app.m_testRunning&&app.m_testQueue.isEmpty();},5000),"AI UI model tests finish");
        check(app.m_configBackend.values.value("aiTests").toList().size()==2,"AI tests cover both enabled models");
        check(app.m_tests.at("ai").value("detail").toString().contains("2/2"),"AI summary includes model results");
        app.m_config.window->grabWindow().save(out+"/settings-ai-tested.png");
        app.aiAction("aiField",QVariantMap{{"key","name"},{"value","已修改渠道"}});
        check(app.m_configBackend.values.value("aiTests").toList().isEmpty(),"editing AI settings clears stale model tests");
        app.configAction("enabled",QVariantMap{{"provider","google"},{"enabled",false}});app.configAction("home",{});pump(30);
        for(auto row:app.m_configBackend.values.value("queue").toList())if(row.toMap().value("provider")=="google")check(!row.toMap().value("enabled").toBool(),"disabled provider visible in queue");
        app.configAction("close",{});report["translationPanel"]=true;report["aiChannelControls"]=true;
        pump(200);report["closedMemory"]=memory();
        report["passed"]=true;code=0;
    }catch(const std::exception& e){report["passed"]=false;report["error"]=QString::fromUtf8(e.what());}
    try{atomicJson(out+"/report.json",report);}catch(...){}
    app.closeWord();app.closePin();app.configAction("close",{});
    ExitProcess(code);
}
}

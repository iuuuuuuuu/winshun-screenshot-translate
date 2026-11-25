#include "App.h"
#include <QUuid>
#include "CaptureKeys.h"
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QScreen>
#include <QJsonDocument>
#include <QPointer>
#include <QKeySequence>
#include <QUrl>
#include <QDebug>
#include <QPixmapCache>
#include <QPainterPath>
#include <dwmapi.h>
#include <psapi.h>
#include <realtimeapiset.h>
#include <thread>
#include <cstdio>
namespace st {
namespace {
QJsonObject row(QString id,QString title,QString subtitle={},bool valid=true)
{return {{"id",id},{"title",title},{"subtitle",subtitle},{"valid",valid},{"icon","glyph:U+E8C1"}};}
QJsonObject effect(const QString& type){return {{"type",type}};}
bool hasId(const QJsonValue& id){return !id.isNull()&&!id.isUndefined();}
Cancel token(){return std::make_shared<std::atomic_bool>(false);}
std::atomic_int backgroundJobs{0};
quint64 awakeMilliseconds(){ULONGLONG time=0;QueryUnbiasedInterruptTime(&time);return time/10000;}
QRect rect(RECT r){return {r.left,r.top,r.right-r.left,r.bottom-r.top};}
QString failure(const std::exception& e){return QString::fromUtf8(e.what());}
template<class Work,class Done> void background(Work work,Done done)
{
    ++backgroundJobs;
    std::thread([work=std::move(work),done=std::move(done)]() mutable {
        using Result=decltype(work());Result value{};QString error;
        try{value=work();}catch(const std::exception& e){error=failure(e);}catch(...){error="操作失败";}
        QMetaObject::invokeMethod(qApp,[done=std::move(done),value=std::move(value),error]()mutable{
            struct Finished {~Finished(){--backgroundJobs;}} finished;
            done(value,error);
        },Qt::QueuedConnection);
    }).detach();
}
QPoint logical(const QPoint& physical,QScreen* s)
{
    POINT p{physical.x(),physical.y()};auto monitor=monitorRect(p);
    return s->geometry().topLeft()+(physical-monitor.topLeft())/s->devicePixelRatio();
}
QScreen* screenAt(QPoint physical)
{
    POINT p{physical.x(),physical.y()};auto area=monitorRect(p);
    for(auto s:QGuiApplication::screens()){
        // Qt keeps each monitor's native origin and scales only its size.
        if(s->geometry().topLeft()==area.topLeft())return s;
    }
    return QGuiApplication::primaryScreen();
}
void roundWindow(QQuickWindow* w)
{
    int corner=2;DwmSetWindowAttribute(HWND(w->winId()),33,&corner,sizeof corner);
    if(w->property("cornerRadius").toReal()<=0||w->property("roundedMaskInstalled").toBool())return;
    w->setProperty("roundedMaskInstalled",true);
    auto update=[w]{
        QPainterPath path;path.addRoundedRect(QRectF(0,0,w->width(),w->height()),w->property("cornerRadius").toReal(),w->property("cornerRadius").toReal());
        w->setMask(QRegion(path.toFillPolygon().toPolygon()));
    };
    QObject::connect(w,&QWindow::widthChanged,w,update);QObject::connect(w,&QWindow::heightChanged,w,update);
    QObject::connect(w,&QWindow::screenChanged,w,update);QObject::connect(w,&QWindow::visibleChanged,w,update);update();
}
QPoint boundedPosition(QQuickWindow* window,QPoint point)
{
    const auto work=window->screen()->availableGeometry().adjusted(4,4,-4,-4);
    point.setX(std::clamp(point.x(),work.left(),std::max(work.left(),work.right()+1-window->width())));
    point.setY(std::clamp(point.y(),work.top(),std::max(work.top(),work.bottom()+1-window->height())));
    return point;
}
void adaptiveWindow(QQuickWindow* window,std::function<void()> place)
{
    auto update=[window,place]{
        window->setProperty("availableArea",window->screen()->availableGeometry());
        place();
    };
    auto watch=[window,update](QScreen* screen){
        QObject::connect(screen,&QScreen::availableGeometryChanged,window,[window,screen,update]{
            if(window->screen()==screen)update();
        });
    };
    for(auto screen:QGuiApplication::screens())watch(screen);
    QObject::connect(qApp,&QGuiApplication::screenAdded,window,watch);
    QObject::connect(window,&QWindow::screenChanged,window,update);
    QObject::connect(window,&QWindow::widthChanged,window,place);
    QObject::connect(window,&QWindow::heightChanged,window,place);
    update();
}
}
App::App(ws::captureplugin::Protocol& protocol,QString appDir,QJsonObject initial):m_appDir(std::move(appDir)),m_protocol(protocol)
{
    defaults(initial.value("settings").toObject());m_hostHotkeys=initial.value("capabilities").toObject().value("hostHotkeys").toBool();
    QString dir=qEnvironmentVariable("WS_DATA_DIR",initial.value("dataDir").toString());
    if(dir.isEmpty())dir=qEnvironmentVariable("LOCALAPPDATA")+"/WinShun/plugins/iuuuuuuuu.screenshot-translate.data";
    m_services=std::make_shared<Services>(dir);
    m_settings["selectionIconEnabled"]=m_services->routing().value("selectionIconEnabled").toBool(true);
    m_services->migrateAi(m_settings);
    m_settings["provider"]=canonicalProvider(m_settings.value("provider").toString());
    m_pinBackend.update({{"comparison",false},{"fullText",false},{"ready",false},{"loading",false},{"status",""},
        {"translatedText",""},{"sourceText",""},{"source","auto"},{"target","zh"},
        {"codes",QStringList{"auto","zh","en","ja","ko","fr","de","es","ru"}}});
    auto look=initial.value("appearance").toObject();
    if(look.isEmpty())look=initial;
    if(!look.contains("locale"))look["locale"]=initial.value("locale").toString("zh");
    appearance(look);
    QObject::connect(&m_pinBackend,&Backend::invoked,this,&App::pinAction);
    QObject::connect(&m_configBackend,&Backend::invoked,this,&App::configAction);
    QObject::connect(&m_position,&QTimer::timeout,this,&App::place);m_position.setInterval(50);
    m_readClipboard=[] {return QGuiApplication::clipboard()->text().trimmed();};
    m_mouse=[] {POINT p{};GetPhysicalCursorPos(&p);return QPoint(p.x,p.y);};
    m_wordBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"pinned",false},{"editing",false},{"loading",false},
        {"sourceText",""},{"translatedText",""},{"status",""},{"provider",""},{"mode","clip"}});
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    m_wordBackend.update({{"animations",bool(animations)}});
    QObject::connect(&m_wordBackend,&Backend::invoked,this,&App::wordAction);
    m_panelBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"sourceText",""},{"source","auto"},{"target","auto"},
        {"translatedText",""},{"definition",""},{"status",""},{"loading",false},{"autoTranslate",true},{"explain",true},
        {"panelMode","auto"},{"panelPage","translate"},{"lookupRows",QVariantList()}});
    QObject::connect(&m_panelBackend,&Backend::invoked,this,&App::panelAction);
    m_followSelection.setInterval(450);
    QObject::connect(&m_followSelection,&QTimer::timeout,this,[this]{readSelection(true);});
    m_iconDelay.setSingleShot(true);m_iconDelay.setInterval(160);
    QObject::connect(&m_iconDelay,&QTimer::timeout,this,&App::readIconSelection);
    m_iconWatch.setInterval(180);
    QObject::connect(&m_iconWatch,&QTimer::timeout,this,[this]{
        if((GetAsyncKeyState(VK_ESCAPE)&0x8000)||!IsWindow(m_iconTarget)||
            GetAncestor(m_foreground(),GA_ROOT)!=GetAncestor(m_iconTarget,GA_ROOT)||
            DWORD(GetTickCount()-m_iconShown)>15000)hideSelectionIcon();
    });
    QObject::connect(&m_iconBackend,&Backend::invoked,this,[this](const QString& action,const QVariant&){if(action=="translate")translateIconSelection();});
    m_primerWatchdog.setSingleShot(true);m_primerRestart.setSingleShot(true);
    QObject::connect(&m_selectionPrimer,&QProcess::started,this,[this]{
        m_selectionPrimer.write("{\"prime\":true,\"watch\":true}\n");m_primerWatchdog.start(4000);
    });
    QObject::connect(&m_selectionPrimer,&QProcess::readyReadStandardOutput,this,[this]{
        m_selectionPrimer.readAllStandardOutput();m_primerWatchdog.start(4000);
    });
    QObject::connect(&m_selectionPrimer,&QProcess::readyReadStandardError,this,[this]{m_selectionPrimer.readAllStandardError();});
    QObject::connect(&m_primerWatchdog,&QTimer::timeout,this,[this]{m_selectionPrimer.kill();});
    QObject::connect(&m_selectionPrimer,&QProcess::finished,this,[this]{
        m_primerWatchdog.stop();m_primerRestart.start(60000);
    });
    QObject::connect(&m_selectionPrimer,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){m_primerWatchdog.stop();m_primerRestart.start(60000);}
    });
    QObject::connect(&m_primerRestart,&QTimer::timeout,this,&App::startSelectionPrimer);
    m_idleTimer.setSingleShot(true);
    QObject::connect(&m_idleTimer,&QTimer::timeout,this,&App::reclaimIdle);
    used();
    registerKeys();
    updateSelectionMonitor();
}
void App::used()
{
    ++*m_activityRevision;m_idleReleased=false;m_holdAfterResume=false;
    m_idleSince=m_lastTick=GetTickCount64();m_lastAwake=awakeMilliseconds();m_idleTimer.start(20000);
}
bool App::idleReady() const
{
    if(m_busy||m_pin||m_selector||m_selectionReading||!m_queries.empty()||m_testRunning||backgroundJobs.load())return false;
    for(auto window:QGuiApplication::allWindows())if(window->isVisible())return false;
    return true;
}
void App::reclaimIdle()
{
    // Follow WinShun's idle policy: keep hotkeys responsive and hold trimming
    // after sleep until the next use. A visible pinned popup is always in use.
    const quint64 tick=GetTickCount64(),awake=awakeMilliseconds();
    const auto elapsed=tick-m_lastTick,awakeFor=std::min(elapsed,awake-m_lastAwake);
    m_lastTick=tick;m_lastAwake=awake;
    if(elapsed-awakeFor>10000)m_holdAfterResume=true;
    m_idleTimer.start(30000);
    if(m_holdAfterResume)return;
    if(!idleReady()){m_idleSince=tick;return;}
    if(tick-m_idleSince<20000){m_idleTimer.start(int(20000-(tick-m_idleSince)));return;}
    if(!m_idleReleased){
        m_idleReleased=true;
        dispose(m_word);dispose(m_bar);dispose(m_status);dispose(m_text);dispose(m_menu);dispose(m_config);dispose(m_panel);dispose(m_icon);
        m_original={};m_translated={};m_blocks.clear();m_data={};m_drafts={};m_tests.clear();m_testCancels.clear();
        m_pinBackend.update({{"translatedText",""},{"sourceText",""},{"provider",""},{"providerNote",""}});
        m_wordBackend.update({{"sourceText",""},{"translatedText",""},{"provider",""},{"providerNote",""},{"status",""}});
        m_configBackend.update({{"apiKey",""},{"endpoint",""},{"model",""},{"region",""},{"appId",""},
            {"testResults",QVariantList()},{"queue",QVariantList()},{"aiChannels",QVariantList()},{"aiTests",QVariantList()},{"message",""}});
        QPixmapCache::clear();
        // QQmlEngine deletion is deferred: let it finish before returning pages.
        m_idleTimer.start(250);return;
    }
    for(auto it=m_cache.begin();it!=m_cache.end();)if(QDateTime::currentSecsSinceEpoch()-it->second.first>=120)it=m_cache.erase(it);else ++it;
    PROCESS_MEMORY_COUNTERS_EX2 info{};info.cb=sizeof info;
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),sizeof info)&&info.PrivateWorkingSetSize<24*1024*1024)return;
    if(m_trimRunning->exchange(true))return;
    auto running=m_trimRunning;auto revision=m_activityRevision;auto count=m_trimCount;auto expected=revision->load();
    std::thread([running,revision,count,expected]{
        if(revision->load()==expected&&!backgroundJobs.load()&&SetProcessWorkingSetSizeEx(GetCurrentProcess(),SIZE_T(-1),SIZE_T(-1),0))++*count;
        running->store(false);
    }).detach();
}
QString App::providerLabel(const QJsonObject& data) const
{
    const auto provider=data.value("provider").toString();
    if(provider.isEmpty())return {};
    auto label=names.value(providers.indexOf(provider),provider);
    if(!m_chinese){if(provider=="sogou")label="Sogou";else if(provider=="youdao")label="Youdao";else if(provider=="ai")label="AI";}
    if(provider=="ai"&&!data.value("model").toString().isEmpty())label+=" · "+data.value("channelName").toString()+" / "+data.value("model").toString();
    return (m_chinese?"渠道：":"Service: ")+label+(data.value("fallback").toBool()?(m_chinese?" · 已降级":" · fallback"):QString());
}
QString App::providerNote(const QJsonObject& data) const
{
    auto label=providerLabel(data);
    if(label.isEmpty()||!data.value("fallback").toBool())return label;
    return label+(m_chinese
        ?"\n主渠道本次未提供结果，已使用备选渠道。失败渠道会在设定的恢复间隔结束后，于后续请求中自动重试；未配置的渠道需先补全配置。"
        :"\nA backup service provided this result. Failed services are retried on later requests after the configured recovery interval. Unconfigured services need setup first.");
}
void App::defaults(const QJsonObject& settings)
{
    m_settings={{"provider","ecdict"},{"proxy",""},{"target","auto"},{"fallbackEnabled",true},{"fallbackRecoveryMinutes",60},
        {"comparisonOrder","translation-first"},{"closeOnOutsideClick",true},{"shotMode","multi"},{"shotHotkey","Alt+Shift+T"},{"clipHotkey","Alt+Shift+C"},{"selectionHotkey","Alt+Shift+W"},
        {"wordOcrHotkey","Alt+Shift+O"}};
    for(auto it=settings.begin();it!=settings.end();++it)m_settings[it.key()]=it.value();
    if(m_services){m_services->migrateAi(m_settings);m_settings["provider"]=canonicalProvider(m_settings.value("provider").toString());}
    if(m_services)m_settings["selectionIconEnabled"]=m_services->routing().value("selectionIconEnabled").toBool(true);
}
void App::appearance(const QJsonObject& params)
{
    if(params.contains("locale"))m_chinese=!params.value("locale").toString().startsWith("en");
    if(params.contains("theme"))m_dark=params.value("theme").toString()=="dark";
    QStringList labels=m_chinese?QStringList{"自动","中文","英文","日文","韩文","法文","德文","西班牙文","俄文"}:
        QStringList{"Automatic","Chinese","English","Japanese","Korean","French","German","Spanish","Russian"};
    m_pinBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"sourceLabels",labels}});
    auto serviceNames=names;if(m_chinese)serviceNames[providers.indexOf("microsoft")]="微软";
    else{serviceNames[4]="Sogou";serviceNames[5]="Youdao";serviceNames[9]="AI translation";}
    m_configBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"names",serviceNames}});
    m_wordBackend.update({{"chinese",m_chinese},{"dark",m_dark}});
    m_panelBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"sourceLabels",labels},{"codes",QStringList{"auto","zh","en","ja","ko","fr","de","es","ru"}}});
    if(m_pin)render();
}
View App::view(const QString& file,Backend* backend)
{
    View v;v.engine=std::make_unique<QQmlEngine>();v.engine->addImportPath(m_appDir+"/qml");
    v.engine->rootContext()->setContextProperty("backend",backend);
    QQmlComponent component(v.engine.get(),QUrl(file));
    auto obj=component.create();v.window=qobject_cast<QQuickWindow*>(obj);
    if(!v.window){
        std::fprintf(stderr,"QML view %s: status=%d type=%s error=%s\n",qPrintable(file),int(component.status()),
            obj?obj->metaObject()->className():"null",qPrintable(component.errorString()));
        delete obj;throw std::runtime_error("无法创建 WinShun 界面");
    }
    obj->setParent(v.engine.get());roundWindow(v.window);return v;
}
void App::dispose(View& v)
{
    if(v.window){v.window->hide();v.window->releaseResources();}
    if(v.engine){auto engine=v.engine.release();engine->deleteLater();}v.window=nullptr;
}
LRESULT CALLBACK App::hotkeyProc(HWND window,UINT message,WPARAM key,LPARAM data)
{
    auto self=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){
        self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(data)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(self&&message==WM_HOTKEY){
        if(auto found=self->m_nativeKeys.find(int(key));found!=self->m_nativeKeys.end())
            self->dispatch({},"hotkey",{{"key",found->second}});
        return 0;
    }
    return DefWindowProcW(window,message,key,data);
}
void App::registerKeys(bool retry)
{
    auto revision=++m_keyRevision;
    for(auto [id,key]:m_nativeKeys)UnregisterHotKey(m_keyWindow,id);
    m_nativeKeys.clear();
    QStringList keys;
    for(auto setting:{"shotHotkey","clipHotkey","selectionHotkey","wordOcrHotkey"}){
        auto key=m_settings.value(setting).toString().trimmed();if(!key.isEmpty()&&!keys.contains(key))keys<<key;
    }
    auto args=QJsonObject{{"keys",QJsonArray::fromStringList(keys)}};
    // Like the official capture plugin, support both the host's registration
    // service and standalone plugin hosts which do not advertise that service.
    m_protocol.notify("winshun/hotkeys",args);
    if(m_hostHotkeys){
        m_protocol.tool("register_hotkeys",args,[this,revision,retry,count=keys.size()](QJsonValue result,QString error){
            if(revision!=m_keyRevision)return;
            if(!error.isEmpty()){m_hostHotkeys=false;registerKeys();return;}
            auto failures=result.toObject().value("errors").toObject();
            std::fprintf(stderr,"[screenshot-translate] hotkeys=host registered=%lld failed=%lld\n",
                static_cast<long long>(count-failures.size()),static_cast<long long>(failures.size()));
            for(auto it=failures.begin();it!=failures.end();++it)
                std::fprintf(stderr,"[screenshot-translate] hotkey=%s registration_error=%d\n",qPrintable(it.key()),it.value().toInt());
            if(!failures.isEmpty()&&!retry)QTimer::singleShot(300,this,[this,revision]{if(revision==m_keyRevision)registerKeys(true);});
            else if(!failures.isEmpty())m_protocol.notify("winshun/notify",{{"message",
                "翻译快捷键 "+failures.keys().join("、")+" 注册失败，请在插件设置中更换"}});
        });return;
    }
    if(!m_keyWindow){
        WNDCLASSW type{};type.lpfnWndProc=hotkeyProc;type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"ScreenshotTranslate.Hotkeys";RegisterClassW(&type);
        m_keyWindow=CreateWindowExW(0,type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,this);
    }
    int id=1;bool failed=false;
    for(auto key:keys){
        auto parsed=ws::capture::CaptureKeys::parse(key);
        auto native=parsed?ws::capture::CaptureKeys::toVirtualKey(*parsed):std::nullopt;
        if(m_keyWindow&&native&&RegisterHotKey(m_keyWindow,id,native->modifiers|MOD_NOREPEAT,native->vk))
            m_nativeKeys[id]=key;
        else{
            failed=true;
            std::fprintf(stderr,"[screenshot-translate] hotkey registration failed id=%d error=%lu\n",id,GetLastError());
        }
        ++id;
    }
    std::fprintf(stderr,"[screenshot-translate] hotkeys=windows registered=%zu\n",m_nativeKeys.size());
    if(failed&&!retry)QTimer::singleShot(300,this,[this,revision]{if(revision==m_keyRevision)registerKeys(true);});
    else if(failed)m_protocol.notify("winshun/notify",{{"message","部分截图翻译热键注册失败；请检查快捷键冲突"}});
}
QJsonArray App::results(const QJsonObject& data,const QString& command)
{
    QJsonArray items;auto d=data;d["command"]=command;
    QJsonArray actions{QJsonObject{{"id","copy-translation"},{"title","复制译文"},{"key","Enter"}},
        QJsonObject{{"id","copy-source"},{"title","复制原文"},{"key","Ctrl+Enter"}},
        QJsonObject{{"id","retry-text"},{"title","重新翻译"},{"key","Shift+Enter"}}};
    for(auto field:{"translated","source"}){
        auto item=row(field,data.value(field).toString(),providerLabel(data)+" · "+data.value("sourceLanguage").toString()+" → "+data.value("targetLanguage").toString());
        item["copy"]=data.value(field);item["data"]=d;item["actions"]=actions;items.append(item);
    }return items;
}
void App::query(const QJsonValue& id,const QJsonObject& params)
{
    used();
    QString command=params.value("command").toString(),text=params.value("text").toString().trimmed();
    int qid=params.value("queryId").toInt();QJsonArray items;
    if(command=="panel"){
        auto item=row("panel",m_chinese?"打开翻译面板":"Open translation panel",m_chinese?"输入文字 · 自动翻译 · 双语对照与释义":"Type, translate and explain");
        item["data"]=QJsonObject{{"source",text}};item["actions"]=QJsonArray{QJsonObject{{"id","open-panel"},{"title","打开"},{"key","Enter"}}};
        m_protocol.respond(id,QJsonObject{{"items",QJsonArray{item}}});return;
    }
    if(command=="shot"){
        auto item=row("shot",m_chinese?"框选屏幕并翻译":"Capture and translate",m_chinese?"Enter 框选 · Ctrl+Enter 全屏 · Shift+Enter 当前窗口":"Enter region · Ctrl+Enter full · Shift+Enter window");
        item["actions"]=QJsonArray{QJsonObject{{"id","start-region"},{"title","框选"},{"key","Enter"}},
            QJsonObject{{"id","start-full"},{"title","全屏"},{"key","Ctrl+Enter"}},QJsonObject{{"id","start-window"},{"title","当前窗口"},{"key","Shift+Enter"}}};
        items.append(item);m_protocol.respond(id,QJsonObject{{"items",items}});return;
    }
    auto cached=m_cache.find(command);
    if(text.isEmpty()||text=="刚刚"||text=="just now"){
        if(cached!=m_cache.end()&&QDateTime::currentSecsSinceEpoch()-cached->second.first<120){m_protocol.respond(id,QJsonObject{{"items",cached->second.second}});return;}
        if(command=="clip")text=QGuiApplication::clipboard()->text().trimmed();
    }
    if(command=="jsel"){
        items.append(row("selection","先选中文字，再按划词热键","使用 UI Automation 读取选区；不模拟按键、不修改剪贴板",false));
        m_protocol.respond(id,QJsonObject{{"items",items}});return;
    }
    if(text.isEmpty()){
        m_protocol.respond(id,QJsonObject{{"items",QJsonArray{row("empty","剪贴板没有文字","可在 jft 后输入文字",false)}}});return;
    }
    if(text.size()>1200){
        auto item=row("long-text",m_chinese?"打开长文翻译":"Translate document",
            m_chinese?"分批翻译并显示进度，可切换逐句对照":"Translate in batches with progress and sentence comparison");
        item["data"]=QJsonObject{{"source",text}};
        item["actions"]=QJsonArray{QJsonObject{{"id","open-panel"},{"title","打开"},{"key","Enter"}}};
        m_protocol.respond(id,QJsonObject{{"items",QJsonArray{item}}});return;
    }
    auto previous=m_queries.find(qid);
    if(previous!=m_queries.end()){previous->second.second->store(true);m_protocol.respond(previous->second.first,QJsonObject{{"items",QJsonArray()}});}
    auto cancel=token();m_queries[qid]={id,cancel};auto services=m_services;auto settings=m_settings;
    m_protocol.notify("winshun/progress",{{"queryId",qid},{"text","正在翻译…"}});
    background([services,settings,text,cancel]{return services->translate(settings,{text},"auto",targetFor(text,settings.value("target").toString()),cancel).json({text});},
        [this,qid,id,command,cancel](QJsonObject data,QString error){
            auto it=m_queries.find(qid);if(it==m_queries.end()||it->second.second!=cancel)return;m_queries.erase(it);
            QJsonArray items=error.isEmpty()?results(data,command):QJsonArray{row("error","翻译失败",error,false)};
            m_protocol.respond(id,QJsonObject{{"items",cancel->load()?QJsonArray():items}});
        });
}
void App::dispatch(const QJsonValue& id,const QString& method,const QJsonObject& params)
{
    try{
        if(method=="query")query(id,params);
        else if(method=="cancel"){
            auto it=m_queries.find(params.value("queryId").toInt());
            if(it!=m_queries.end()){it->second.second->store(true);m_protocol.respond(it->second.first,QJsonObject{{"items",QJsonArray()}});m_queries.erase(it);}
        }else if(method=="hotkey"){
            auto key=params.value("key").toString();
            if(key.trimmed().isEmpty())return;
            if(key.compare(m_settings.value("shotHotkey").toString(),Qt::CaseInsensitive)==0)shot("region");
            else if(key.compare(m_settings.value("clipHotkey").toString(),Qt::CaseInsensitive)==0)textFlow("clip");
            else if(!key.isEmpty()&&key.compare(m_settings.value("selectionHotkey").toString(),Qt::CaseInsensitive)==0)textFlow("jsel");
            else if(key.compare(m_settings.value("wordOcrHotkey").toString(),Qt::CaseInsensitive)==0)textFlow("ocr");
        }else if(method=="action"){
            QString action=params.value("action").toString();QJsonArray effects;
            if(action=="open-panel"){
                showPanel(params.value("item").toObject().value("data").toObject().value("source").toString());effects.append(effect("hide"));
            }else if(action.startsWith("copy-")){
                auto data=params.value("item").toObject().value("data").toObject();
                effects.append(QJsonObject{{"type","copy"},{"text",data.value(action=="copy-source"?"source":"translated")}});
            }else if(action=="retry-text"){
                auto data=params.value("item").toObject().value("data").toObject();
                effects.append(QJsonObject{{"type","setQuery"},{"text","jft "+data.value("source").toString()}});
            }else{
                effects.append(effect(m_busy?"keep":"hide"));
                if(!m_busy){
                    if(action=="retry-clip")textFlow("clip");
                    else if(action=="read-selection")textFlow("jsel");
                    else shot(action=="start-full"?"full":action=="start-window"?"window":"region",160);
                }
            }m_protocol.respond(id,QJsonObject{{"effects",effects}});
        }else if(method=="showSettings"){showSettings();if(hasId(id))m_protocol.respond(id,QJsonObject());}
        else if(method=="settingsChanged"){
            defaults(params.value("settings").toObject());registerKeys();updateSelectionMonitor();
            if(m_pin){render();m_pinOpened=GetTickCount();}
        }else if(method=="appearance")appearance(params);
        else if(method=="shutdown"){
            if(hasId(id))m_protocol.respond(id,QJsonObject());closeWord();closePin();panelAction("close",{});dispose(m_config);ExitProcess(0);
        }else if(hasId(id))m_protocol.respond(id,QJsonObject());
    }catch(const std::exception& e){
        m_busy=false;
        if(hasId(id))m_protocol.respond(id,QJsonObject{{"items",QJsonArray{row("error","操作失败",failure(e),false)}}});
        else m_protocol.notify("winshun/notify",{{"message",failure(e)}});
    }
}
void App::shot(const QString& kind,int delay)
{
    if(m_busy)return;
    used();
    hideSelectionIcon();closePin();m_busy=true;HWND foreground=GetForegroundWindow();auto epoch=++m_epoch;
    QTimer::singleShot(delay,this,[this,epoch,kind,foreground]{
        if(epoch!=m_epoch)return;
        try{
            if(kind=="region"){
                m_selector=std::make_unique<Selector>([this,epoch](QImage image,QRect area){
                    if(epoch!=m_epoch)return;m_selector.reset();
                    if(image.isNull()){m_busy=false;return;}openPin(image,area);recognizePin();
                },m_capture);
                if(!m_selector->show()){m_selector.reset();m_busy=false;}
            }else{
                QRect area;
                if(kind=="window"){
                    HWND target=GetForegroundWindow();if(!target)target=foreground;RECT r{};
                    if(FAILED(DwmGetWindowAttribute(target,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof r))&&!GetWindowRect(target,&r))
                        throw std::runtime_error("无法读取当前窗口");
                    area=rect(r).intersected(virtualScreen());
                }
                else{POINT p{};GetPhysicalCursorPos(&p);area=m_settings.value("shotMode").toString()=="single"?monitorRect(p):virtualScreen();}
                openPin(m_capture(area),area);recognizePin();
            }
        }catch(const std::exception& e){m_busy=false;m_protocol.notify("winshun/notify",{{"message",failure(e)}});}
    });
}
void App::openPin(QImage image,QRect area)
{
    used();
    m_original=image;m_translated=image;m_blocks.clear();m_data={};m_compare=false;m_fullText=false;m_cancel=token();m_pinOpened=GetTickCount();
    auto captureScreen=screenAt(area.center());
    m_pinBackend.update({{"translatedText",""},{"sourceText",""},{"ready",false},{"loading",true},{"comparison",false},{"fullText",false},
        {"provider",m_chinese?"等待翻译":"Awaiting translation"},{"providerNote",""},{"providerFallback",false},{"source","auto"},{"target","zh"},
        {"captureWidth",area.width()/captureScreen->devicePixelRatio()},{"captureHeight",area.height()/captureScreen->devicePixelRatio()},
        {"loadingStage","recognize"},{"status",""}});
    ws::capture::PinWindow::Host host;
    host.closed=[this](auto*){QTimer::singleShot(0,this,[this]{closePin();});};
    host.copy=[this](auto*){pinAction("copy",false);};
    host.copyText=[this](const QString& text){if(!text.isEmpty())m_protocol.tool("clipboard_copy",{{"text",text}});};
    host.save=[this](auto* pin){
        auto path=qEnvironmentVariable("USERPROFILE")+"/Pictures/WinShun_Translate_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")+".png";
        if(pin->image().save(path))m_protocol.notify("winshun/toast",{{"message","已保存到图片文件夹"}});
    };
    host.menu=[this](auto*,POINT at){
        if(!m_menu.window)m_menu=view("qrc:/ui/qml/PinMenu.qml",&m_pinBackend);
        auto s=screenAt({at.x,at.y});m_menu.window->setScreen(s);auto area=s->availableGeometry();
        QPoint pos=logical({at.x,at.y},s);
        pos.setX(std::clamp(pos.x(),area.left(),std::max(area.left(),area.right()-m_menu.window->width())));
        pos.setY(std::clamp(pos.y(),area.top(),std::max(area.top(),area.bottom()-m_menu.window->height())));
        m_menu.window->setPosition(pos);m_menu.window->show();m_menu.window->requestActivate();
    };
    host.dark=[this]{return m_dark;};host.accent=[this]{return QColor(m_dark?"#60CDFF":"#0067C0");};
    m_pin=std::make_unique<ws::capture::PinWindow>(image,area.topLeft(),std::move(host));
    m_pin->setDoubleClick(ws::capture::PinDoubleClick::Close);
    m_bar=view("qrc:/ui/Toolbar.qml",&m_pinBackend);m_status=view("qrc:/ui/qml/Status.qml",&m_pinBackend);
    place();m_bar.window->show();m_position.start();
    ensureRaw();
}
void App::ensureRaw()
{
    if(m_raw)return;
    m_raw=std::make_unique<RawClicks>([this](POINT point,DWORD timestamp){
        QMetaObject::invokeMethod(qApp,[this,point,timestamp]{
            auto contains=[&](HWND hwnd){
                RECT r{};return hwnd&&IsWindowVisible(hwnd)&&GetWindowRect(hwnd,&r)&&PtInRect(&r,point);
            };
            auto insideView=[&](View& view){
                if(view.engine)for(auto window:view.engine->findChildren<QQuickWindow*>())
                    if(window->isVisible()&&!window->flags().testFlag(Qt::WindowTransparentForInput)&&contains(HWND(window->winId())))return true;
                return false;
            };
            // Word popups are transient unless the user explicitly pins them.
            // Screenshot pin preferences and other plugin windows do not keep
            // an unpinned word popup open.
            const bool insideWord=insideView(m_word);
            if(m_word.window&&m_word.window->isVisible()&&!m_wordPinned&&!insideWord&&LONG(timestamp-m_wordOpened)>=150)closeWord();
            if(insideWord)return;
            if(m_word.window)QMetaObject::invokeMethod(m_word.window,"dismissProviderMenu");
            if(m_pin&&contains(m_pin->hwnd()))return;
            for(auto v:{&m_bar,&m_status,&m_config,&m_text,&m_menu})if(insideView(*v))return;
            DWORD pid=0;auto clicked=WindowFromPoint(point);GetWindowThreadProcessId(clicked,&pid);
            if(pid==GetCurrentProcessId())return;
            if(m_pin&&m_settings.value("closeOnOutsideClick").toBool(true)&&LONG(timestamp-m_pinOpened)>=150)closePin();
        },Qt::QueuedConnection);
    },[this](POINT point,DWORD timestamp,USHORT flags){
        QMetaObject::invokeMethod(this,[this,point,timestamp,flags]{selectionGesture(point,timestamp,flags);},Qt::QueuedConnection);
    });
}
void App::updateSelectionMonitor()
{
    startSelectionPrimer();
    if(m_settings.value("selectionIconEnabled").toBool(true))ensureRaw();
    else{hideSelectionIcon();m_iconPressed=false;if(!m_pin&&(!m_word.window||!m_word.window->isVisible()))m_raw.reset();}
}
void App::startSelectionPrimer()
{
    bool enabled=m_settings.value("selectionIconEnabled").toBool(true)||!m_settings.value("selectionHotkey").toString().isEmpty();
    if(!enabled){m_primerWatchdog.stop();m_primerRestart.stop();m_selectionPrimer.kill();return;}
    if(m_selectionPrimer.state()!=QProcess::NotRunning)return;
    wchar_t desktop[256]{};DWORD size=0;
    if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof desktop,&size))return;
    const auto name=QString::fromWCharArray(desktop);
    m_selectionPrimer.setCreateProcessArgumentsModifier([name](QProcess::CreateProcessArguments* args){
        args->flags|=CREATE_NO_WINDOW;
        auto startup=reinterpret_cast<STARTUPINFOW*>(args->startupInfo);
        startup->lpDesktop=const_cast<wchar_t*>(reinterpret_cast<const wchar_t*>(name.utf16()));
        startup->dwFlags|=STARTF_USESHOWWINDOW;startup->wShowWindow=SW_HIDE;
    });
    m_selectionPrimer.start(QCoreApplication::applicationDirPath()+"/ScreenshotTranslate.exe",{"--selection-worker"});
}
void App::hideSelectionIcon()
{
    ++m_iconEpoch;if(m_iconCancel)m_iconCancel->store(true);
    m_iconDelay.stop();m_iconWatch.stop();m_iconSelection={};
    if(m_icon.window)m_icon.window->hide();
}
void App::selectionGesture(POINT point,DWORD timestamp,USHORT flags)
{
    if(!m_settings.value("selectionIconEnabled").toBool(true))return;
    DWORD pid=0;GetWindowThreadProcessId(WindowFromPoint(point),&pid);
    // Clicking the passive icon must keep both the source selection and the
    // captured text alive until QML delivers the click.
    if(pid==GetCurrentProcessId())return;
    if(flags&(RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL)){
        // Wheel scrolling while holding the selection drag is still the same
        // gesture. Only dismiss an already displayed icon.
        if(m_iconPressed)m_iconDouble=true; // counts even if mouse-up returns to the starting pixel
        hideSelectionIcon();return;
    }
    if(flags&(RI_MOUSE_RIGHT_BUTTON_DOWN|RI_MOUSE_MIDDLE_BUTTON_DOWN)){
        hideSelectionIcon();m_iconPressed=false;return;
    }
    const QPoint at(point.x,point.y);
    if(flags&RI_MOUSE_LEFT_BUTTON_DOWN){
        hideSelectionIcon();m_iconDown=at;m_iconPressed=true;
        m_iconDouble=DWORD(timestamp-m_iconClickTime)<=GetDoubleClickTime()&&(at-m_iconPreviousClick).manhattanLength()<8;
        m_iconClickTime=timestamp;m_iconPreviousClick=at;
    }
    if(flags&RI_MOUSE_LEFT_BUTTON_UP){
        if(!m_iconPressed)return;m_iconPressed=false;
        if(!m_iconDouble&&(at-m_iconDown).manhattanLength()<4&&!(GetAsyncKeyState(VK_SHIFT)&0x8000))return;
        if(m_selector||m_wordPinned||m_wordEditing)return;
        m_iconPoint=at;m_iconTarget=selectionTarget();
        GetWindowThreadProcessId(m_iconTarget,&pid);
        if(!m_iconTarget||pid==GetCurrentProcessId())return;
        m_iconDelay.start();
    }
}
void App::readIconSelection()
{
    if(!m_settings.value("selectionIconEnabled").toBool(true)||m_selector||m_wordPinned||m_wordEditing)return;
    if(GetAncestor(m_foreground(),GA_ROOT)!=GetAncestor(m_iconTarget,GA_ROOT))return;
    if(m_iconReading||m_selectionReading){m_iconDelay.start(100);return;}
    m_iconReading=true;m_iconCancel=token();auto cancel=m_iconCancel;auto epoch=m_iconEpoch;
    auto reader=m_readSelection;auto at=m_iconPoint;auto target=m_iconTarget;
    background([reader,at,target,cancel]{return reader(POINT{at.x(),at.y()},target,cancel);},
        [this,epoch,cancel,target,at](Selection selection,QString error){
            m_iconReading=false;
            auto pending=m_selectionPendingEpoch;
            if(pending&&pending==m_wordEpoch){m_selectionPendingEpoch=0;readSelection();return;}
            if(cancel->load()||epoch!=m_iconEpoch||!error.isEmpty()||selection.text.trimmed().isEmpty()||
                m_selector||m_wordPinned||m_wordEditing||
                GetAncestor(m_foreground(),GA_ROOT)!=GetAncestor(target,GA_ROOT))return;
            m_iconSelection=selection;
            // Text ranges are returned in document order, regardless of drag
            // direction. Anchor to the captured mouse-up point, even if the
            // pointer has moved while the accessibility read was running.
            const auto point=at;
            if(!m_icon.window)m_icon=view("qrc:/ui/qml/SelectionIcon.qml",&m_iconBackend);
            auto screen=screenAt(point);m_icon.window->setScreen(screen);
            m_icon.window->setPosition(boundedPosition(m_icon.window,logical(point,screen)+QPoint(5,5)));
            m_iconBackend.update({{"chinese",m_chinese},{"dark",m_dark}});
            m_iconShown=GetTickCount();m_icon.window->show();m_iconWatch.start();used();
        });
}
void App::translateIconSelection()
{
    if(m_iconSelection.text.trimmed().isEmpty())return;
    auto selection=m_iconSelection;auto point=m_iconPoint;auto target=m_iconTarget;
    hideSelectionIcon();m_selectionTarget=target;
    openWord("jsel",point,selection.area);translateWord(selection.text);
}
void App::closePin()
{
    used();
    if(m_bar.window)m_bar.window->hide();if(m_status.window)m_status.window->hide();if(m_text.window)m_text.window->hide();if(m_menu.window)m_menu.window->hide();
    if(m_pin)m_pin->setShown(false);
    if(m_cancel)m_cancel->store(true);++m_epoch;m_busy=false;m_position.stop();
    if(!m_settings.value("selectionIconEnabled").toBool(true)&&(!m_word.window||!m_word.window->isVisible()))m_raw.reset();
    m_pin.reset();m_selector.reset();dispose(m_bar);dispose(m_status);dispose(m_text);dispose(m_menu);
    m_original={};m_translated={};m_blocks.clear();m_data={};
}
void App::status(QString text,bool loading)
{
    m_pinBackend.update({{"status",text},{"loading",loading},{"ready",!m_blocks.isEmpty()}});
    if(m_status.window){if(text.isEmpty())m_status.window->hide();else{place();m_status.window->show();}}
}
void App::recognizePin()
{
    m_pinBackend.update({{"loadingStage","recognize"}});
    status(m_chinese?"正在识别文字…":"Recognizing text…");auto image=m_original;auto cancel=m_cancel;auto epoch=m_epoch;
    background([image,cancel]{return recognize(image,cancel);},[this,cancel,epoch](QList<Block> blocks,QString error){
        if(!m_pin||cancel->load()||epoch!=m_epoch)return;
        if(!error.isEmpty()||blocks.isEmpty()){status(error.isEmpty()?"没有识别到文字":error,false);m_busy=false;return;}
        m_blocks=blocks;QStringList parts;for(auto b:blocks)parts<<b.text;
        m_source=detect(parts.join('\n'));m_target=targetFor(parts.join('\n'),m_settings.value("target").toString());
        m_busy=false;translatePin(m_source,m_target,parts);
    });
}
void App::translatePin(QString source,QString target,QStringList parts)
{
    if(!m_pin||m_busy)return;
    if(source.isEmpty())source=m_source;if(target.isEmpty())target=m_target;
    if(parts.isEmpty())for(auto b:m_blocks)parts<<b.text;
    m_busy=true;m_pinBackend.update({{"loadingStage","translate"},{"provider",m_chinese?"正在选择渠道…":"Selecting service…"},{"providerNote",""},{"providerFallback",false}});
    status(m_chinese?"正在翻译…":"Translating…");m_source=source;m_target=target;
    m_pinBackend.update({{"source",source},{"target",target},{"ready",true}});
    auto services=m_services;auto settings=m_settings;auto cancel=m_cancel;auto epoch=m_epoch;
    background([services,settings,parts,source,target,cancel]{return services->translate(settings,parts,source,target,cancel).json(parts);},
        [this,parts,cancel,epoch](QJsonObject data,QString error){
            if(!m_pin||cancel->load()||epoch!=m_epoch)return;m_busy=false;
            if(!error.isEmpty()){m_pinBackend.update({{"provider",m_chinese?"渠道请求失败":"Service failed"}});status(error,false);return;}
            QStringList original;for(auto b:m_blocks)original<<b.text;
            data["source"]=original.join('\n');m_data=data;
            m_pinBackend.update({{"translatedText",data.value("translated").toString()},{"sourceText",data.value("source").toString()},
                {"provider",providerLabel(data)},{"providerNote",providerNote(data)},{"providerFallback",data.value("fallback").toBool()},
                {"source",data.value("sourceLanguage").toString()},{"target",data.value("targetLanguage").toString()}});
            status("",false);render();
        });
}
void App::render()
{
    if(!m_pin)return;QStringList segments;for(auto v:m_data.value("segments").toArray())segments<<v.toString();
    QList<ws::capture::RedactLine> selectable;
    m_translated=segments.isEmpty()?m_original:translatedImage(m_original,m_blocks,segments,m_dark,&selectable);
    auto image=m_compare?comparisonImage(m_original,m_translated,m_settings.value("comparisonOrder").toString()!="source-first",m_dark,m_chinese):m_translated;
    if(m_compare){
        int shift=m_settings.value("comparisonOrder").toString()!="source-first"?30:m_original.height()+60;
        for(auto& line:selectable)line.quad.translate(0,shift);
    }
    m_pin->setPicture(image,{});m_pin->setSelectableLines(selectable);m_pinBackend.update({{"comparison",m_compare}});place();
}
void App::place()
{
    if(!m_pin||!m_bar.window)return;
    auto area=m_pin->pictureRect();auto s=screenAt(area.center());POINT c{area.center().x(),area.center().y()};auto work=monitorRect(c,true);
    m_pinBackend.update({{"captureWidth",area.width()/s->devicePixelRatio()},{"captureHeight",area.height()/s->devicePixelRatio()}});
    if(m_bar.window->screen()!=s)m_bar.window->setScreen(s);
    auto size=m_bar.window->size()*s->devicePixelRatio();
    int x=std::clamp(area.center().x()-size.width()/2,work.left()+4,std::max(work.left()+4,work.x()+work.width()-size.width()-4));
    int y=area.y()+area.height()+8;
    if(y+size.height()>work.y()+work.height())y=std::max(work.top()+4,area.y()-size.height()-8);
    auto desired=logical(QPoint(x,y),s);if(m_bar.window->position()!=desired)m_bar.window->setPosition(desired);
    auto lw=logical(work.topLeft(),s);m_pinBackend.update({{"menuLeft",lw.x()},{"menuTop",lw.y()},
        {"menuRight",lw.x()+work.width()/s->devicePixelRatio()},{"menuBottom",lw.y()+work.height()/s->devicePixelRatio()}});
    if(m_status.window){
        if(m_status.window->screen()!=s)m_status.window->setScreen(s);
        auto statusSize=m_status.window->size()*s->devicePixelRatio();
        auto point=area.center()-QPoint(statusSize.width(),statusSize.height())/2;
        if(m_status.window->property("compact").toBool()||statusSize.width()+16>area.width()||statusSize.height()+16>area.height()){
            // A word-sized capture remains unobscured: put its small indicator
            // just above it, or below when the upper screen edge has no room.
            point={area.left(),area.top()-statusSize.height()-6};
            if(point.y()<work.top()+4)point.setY(area.bottom()+7);
        }
        point.setX(std::clamp(point.x(),work.left()+4,std::max(work.left()+4,work.x()+work.width()-statusSize.width()-4)));
        point.setY(std::clamp(point.y(),work.top()+4,std::max(work.top()+4,work.y()+work.height()-statusSize.height()-4)));
        auto desiredStatus=logical(point,s);
        if(m_status.window->position()!=desiredStatus)m_status.window->setPosition(desiredStatus);
    }
}
void App::pinAction(const QString& name,const QVariant& value)
{
    try{
        if(name=="close")closePin();
        else if(name=="retry"){closePin();shot("region");}
        else if(name=="settings")showSettings();
        else if(name=="copyText"){
            auto text=value.toString();if(!text.isEmpty())m_protocol.tool("clipboard_copy",{{"text",text}});
        }
        else if(name=="copy"){
            QString text=m_data.value(value.toBool()?"source":"translated").toString();
            if(!text.isEmpty())m_protocol.tool("clipboard_copy",{{"text",text}});
        }else if(name=="compare"){m_compare=!m_compare;render();}
        else if(name=="saveImage"&&m_pin){
            auto path=qEnvironmentVariable("USERPROFILE")+"/Pictures/WinShun_Translate_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")+".png";
            if(!m_pin->image().save(path))status("图片保存失败",false);
        }
        else if(name=="source"||name=="target"){
            if(m_busy)return;auto code=value.toString();
            if(!QStringList{"auto","zh","en","ja","ko","fr","de","es","ru"}.contains(code))return;
            if(name=="source")m_source=code;else m_target=code;translatePin();
        }else if(name=="swap"&&!m_busy){
            QStringList parts;for(auto v:m_data.value("segments").toArray())parts<<v.toString();
            if(!parts.isEmpty())translatePin(m_target,m_data.value("sourceLanguage").toString(),parts);
        }else if(name=="text"){
            if(!m_text.window){
                m_text=view("qrc:/ui/qml/Text.qml",&m_pinBackend);
                auto window=m_text.window;
                adaptiveWindow(window,[window]{window->setPosition(boundedPosition(window,window->position()));});
            }
            bool showing=!m_text.window->isVisible();
            if(showing){
                m_text.window->setScreen(m_bar.window->screen());
                m_text.window->setPosition(boundedPosition(m_text.window,m_bar.window->position()+QPoint(0,m_bar.window->height()+8)));
            }
            m_text.window->setVisible(showing);
            if(showing)m_text.window->requestActivate();
            m_pinBackend.update({{"fullText",showing}});
        }
    }catch(const std::exception& e){status(failure(e),false);m_busy=false;}
}
void App::textFlow(const QString& command,bool retry)
{
    // A selector must keep a clean frozen desktop. Ordinary word requests and
    // screenshot translation otherwise have independent cancellation/state.
    if(m_selector)return;
    // A retry belongs to the original request. The pointer and foreground now
    // often belong to the popup's Retry button, not the source application.
    auto point=retry?m_wordRequestPoint:m_mouse();
    m_wordPlacementFrozen=retry;
    if(!retry)m_wordRequestPoint=point;
    // Snapshot the source HWND before creating or showing the popup. Browser
    // focus and hit-testing may otherwise refer to our own editable window.
    if(command=="jsel"){
        if(!retry){
            auto target=selectionTarget();
            // A hotkey from our editable popup still refers to its source.
            if(target||!IsWindow(m_selectionTarget))m_selectionTarget=target;
        }
        m_selectionAttempts=0;
    }
    if(command=="ocr"&&m_word.window)m_word.window->hide();
    if(command!="ocr")openWord(command,point);
    if(command=="clip"){translateWord(m_readClipboard());return;}
    if(command=="jsel"){readSelection();return;}
    auto cancel=token();if(m_wordCancel)m_wordCancel->store(true);m_wordCancel=cancel;auto epoch=++m_wordEpoch;
    // Let the old popup disappear before BitBlt, then capture before showing
    // the new one. The user's pointer and clipboard are never modified.
    QTimer::singleShot(35,this,[this,point,cancel,epoch]() mutable {
        if(epoch!=m_wordEpoch||cancel->load())return;
        POINT p{point.x(),point.y()};auto screen=screenAt(point);
        auto monitor=monitorRect(p);int height=qRound(180*screen->devicePixelRatio());
        // A narrow monitor-wide strip retains both ends of a short sentence,
        // including when the pointer rests on its first or last word.
        QRect crop(monitor.left(),point.y()-height/2,monitor.width(),height);crop=crop.intersected(monitor);
        QImage image;
        try{image=m_capture(crop);}catch(const std::exception& e){
            openWord("ocr",point);m_wordBackend.update({{"loading",false},{"status",failure(e)}});return;
        }
        openWord("ocr",point);cancel=m_wordCancel;auto activeEpoch=m_wordEpoch;
        background([image,cancel,at=point-crop.topLeft()]{return recognizeAt(image,cancel,at);},
            [this,cancel,activeEpoch,crop,point](Block word,QString error){
                if(activeEpoch!=m_wordEpoch||cancel->load())return;
                if(!error.isEmpty()||word.text.isEmpty()){
                    m_wordBackend.update({{"loading",false},{"status",error.isEmpty()?
                        QString("鼠标处没有识别到文字，请把鼠标停在短句上再试"):error}});return;
                }
                if(!m_wordPinned){
                    m_wordSelection=word.box.toAlignedRect().translated(crop.topLeft());
                    // Anchor below the pointed word, even for a long sentence.
                    int x=std::clamp(point.x(),m_wordSelection.left(),m_wordSelection.right());
                    m_wordSelection.setLeft(x);m_wordSelection.setWidth(1);placeWord();
                }
                translateWord(word.text);
            });
        QTimer::singleShot(8000,this,[this,cancel,activeEpoch]{
            if(activeEpoch!=m_wordEpoch||cancel->load())return;cancel->store(true);
            m_wordBackend.update({{"loading",false},{"status","识别超时，请重试"}});
        });
    });
}
void App::openWord(QString mode,QPoint anchor,QRect selection)
{
    hideSelectionIcon();
    used();
    if(m_selectionCancel)m_selectionCancel->store(true);
    m_selectionPendingEpoch=0;
    editWord(false);
    if(m_wordCancel)m_wordCancel->store(true);m_wordCancel=token();++m_wordEpoch;
    m_wordMode=mode;
    if(!m_wordPinned&&!m_wordPlacementFrozen){m_wordAnchor=anchor;m_wordSelection=selection;}
    m_wordSource.clear();m_lastSelection.clear();m_wordOpened=GetTickCount();
    m_followBaseline=m_wordPinned&&mode!="jsel";
    m_wordBackend.update({{"mode",mode},{"sourceText",""},{"translatedText",""},{"alignmentSource",""},{"alignmentRows",QVariantList()},
        {"providerSwitching",false},{"providerSwitchError",false},{"provider",""},{"providerNote",""},{"providerFallback",false},{"loading",true},
        {"status",mode=="jsel"?"正在读取选中文字…":mode=="ocr"?"正在识别鼠标处的短句…":"正在翻译…"}});
    publishWordProviders();
    if(!m_word.window){
        m_word=view("qrc:/ui/qml/Word.qml",&m_wordBackend);
        adaptiveWindow(m_word.window,[this]{placeWord();});
    }
    QMetaObject::invokeMethod(m_word.window,"resetInteraction");
    placeWord();m_word.window->show();ensureRaw();
}
void App::placeWord()
{
    if(!m_word.window)return;
    if(m_wordPinned||m_wordPlacementFrozen){
        // Keep the user's chosen position unless growth would move controls
        // beyond this monitor. Retrying never anchors at the Retry button.
        m_word.window->setPosition(boundedPosition(m_word.window,m_word.window->position()));
        return;
    }
    auto point=m_wordSelection.isEmpty()?m_wordAnchor:m_wordSelection.bottomLeft();
    auto s=screenAt(point);m_word.window->setScreen(s);auto work=s->availableGeometry();
    auto at=logical(point,s)+QPoint(10,10);
    if(at.y()+m_word.window->height()>work.bottom()-4){
        auto top=m_wordSelection.isEmpty()?m_wordAnchor:m_wordSelection.topLeft();
        at.setY(logical(top,s).y()-m_word.window->height()-10);
    }
    m_word.window->setPosition(boundedPosition(m_word.window,at));
}
void App::closeWord()
{
    used();
    if(m_selectionCancel)m_selectionCancel->store(true);
    m_selectionPendingEpoch=0;
    if(m_word.window)m_word.window->hide();
    editWord(false);
    if(m_wordCancel)m_wordCancel->store(true);++m_wordEpoch;
    m_wordPinned=false;m_wordPlacementFrozen=false;m_followSelection.stop();m_wordSource.clear();m_lastSelection.clear();
    m_wordProviderChoice.clear();m_wordProviderProfiles.clear();
    m_wordBackend.update({{"pinned",false},{"loading",false},{"providerSwitching",false}});
    if(!m_pin&&!m_settings.value("selectionIconEnabled").toBool(true))m_raw.reset();
}
void App::editWord(bool editing)
{
    if(m_wordEditing==editing)return;
    m_wordEditing=editing;m_wordBackend.update({{"editing",editing}});
    if(editing){
        m_followSelection.stop();
    }else if(m_wordPinned&&m_word.window&&m_word.window->isVisible())m_followSelection.start();
}
void App::wordAction(const QString& name,const QVariant& value)
{
    if(name=="close")closeWord();
    else if(name=="move")m_wordPlacementFrozen=true;
    else if(name=="edit")editWord(true);
    else if(name=="cancelEdit")editWord(false);
    else if(name=="providerMenu"){
        if(value.toBool()){
            publishWordProviders();
            if(m_word.window)if(auto menu=m_word.window->findChild<QQuickWindow*>("word-provider-menu"))roundWindow(menu);
        }
    }
    else if(name=="providerSettings")showSettings();
    else if(name=="provider"){
        if(m_wordBackend.values.value("providerSwitching").toBool())return;
        const auto request=value.toMap();const auto id=request.value("id").toString();
        publishWordProviders();
        if(!id.isEmpty()&&!m_wordProviderProfiles.contains(id))return;
        const auto text=request.value("text",m_wordSource).toString().trimmed();
        if(text.isEmpty())return;
        m_wordProviderChoice=id;editWord(false);publishWordProviders();
        m_wordBackend.update({{"providerSwitching",true},{"providerSwitchError",false}});
        translateWord(text,true);
    }
    else if(name=="submitEdit"){
        auto text=value.toString().trimmed();if(text.isEmpty())return;
        if(text==m_wordSource){editWord(false);return;}
        translateWord(text);editWord(false);
    }
    else if(name=="pin"){
        m_wordPinned=!m_wordPinned;m_wordBackend.update({{"pinned",m_wordPinned}});
        if(m_wordPinned){
            m_followBaseline=m_lastSelection.isEmpty();
            if(!m_wordEditing){m_followSelection.start();readSelection(true);}
        }else m_followSelection.stop();
    }else if(name=="retry"){
        if(m_wordSource.isEmpty())textFlow(m_wordMode,true);else translateWord(m_wordSource);
    }else if(name=="copy"){
        auto text=m_wordBackend.values.value("translatedText").toString();
        if(!text.isEmpty())m_protocol.tool("clipboard_copy",{{"text",text}});
    }
}
void App::readSelection(bool follow)
{
    if(!m_word.window||!m_word.window->isVisible()||m_wordEditing||(follow&&!m_wordPinned))return;
    // Keep a pinned window's polling out of the foreground read and its retry delays.
    if(follow&&m_wordMode=="jsel"&&m_wordSource.isEmpty()&&m_wordBackend.values.value("loading").toBool())return;
    if(m_iconReading){
        // A hotkey takes priority over the passive icon, but must wait until
        // its cancelled worker has exited before querying the same provider.
        if(!follow){
            if(m_iconCancel)m_iconCancel->store(true);
            m_selectionPendingEpoch=m_wordEpoch;
        }
        return;
    }
    if(m_selectionReading){
        // Repeated hotkeys (including close/reopen) replace the old request.
        // Wait for its worker to be reaped before starting the latest one.
        if(!follow){
            if(m_selectionCancel)m_selectionCancel->store(true);
            m_selectionPendingEpoch=m_wordEpoch;
        }
        return;
    }
    auto target=follow?selectionTarget():m_selectionTarget;
    m_selectionReading=true;auto epoch=m_wordEpoch;auto reader=m_readSelection;
    auto point=follow?m_mouse():m_wordRequestPoint;auto cancel=m_wordCancel;
    m_selectionCancel=token();auto selectionCancel=m_selectionCancel;
    background([reader,point,target,selectionCancel]{return reader(POINT{point.x(),point.y()},target,selectionCancel);},
        [this,follow,epoch,cancel,selectionCancel](Selection selection,QString error){
            m_selectionReading=false;
            auto pending=m_selectionPendingEpoch;m_selectionPendingEpoch=0;
            if(pending&&pending==m_wordEpoch){readSelection();return;}
            if(epoch!=m_wordEpoch||selectionCancel->load()||(!follow&&cancel->load())||m_wordEditing||!m_word.window->isVisible())return;
            auto text=selection.text.trimmed();
            if(follow){
                if(m_followBaseline){m_followBaseline=false;m_lastSelection=text;return;}
                if(!error.isEmpty()||text.isEmpty())return;
                if(text==m_lastSelection||text==m_wordSource)return;
            }else if(!error.isEmpty()||text.isEmpty()){
                // A provider may publish the selection just after the hotkey.
                // Keep the original target and do not repeat timeouts or access errors.
                if(error.isEmpty()&&m_selectionAttempts<2){
                    const auto attempt=++m_selectionAttempts;
                    QTimer::singleShot(150*attempt,this,[this,epoch,selectionCancel]{
                        if(epoch==m_wordEpoch&&!selectionCancel->load()&&m_word.window&&m_word.window->isVisible())readSelection();
                    });
                    return;
                }
                m_wordBackend.update({{"loading",false},{"status",error.isEmpty()?
                    QString("快捷键已响应，但没有读到选中文字。请回到原窗口保留选区后重试；也可用鼠标 OCR 热键 %1").arg(m_settings.value("wordOcrHotkey").toString()):error}});
                return;
            }
            m_lastSelection=text;
            if(!m_wordPinned){m_wordSelection=selection.area;placeWord();}
            translateWord(text);
        });
}
void App::publishWordProviders()
{
    QVariantList rows{QVariantMap{{"id",""},{"name",m_chinese?"自动 · 跟随设置":"Automatic · follow settings"},
        {"detail",m_chinese?"按已设置的优先级和降级策略翻译":"Use configured priority and fallback"},{"selectable",true}}};
    m_wordProviderProfiles.clear();
    for(auto value:m_services->queue(m_settings)){
        const auto row=value.toObject();const auto provider=row.value("provider").toString();
        auto profile=m_services->profile(provider);profile["provider"]=provider;
        auto choices=m_services->candidates(profile,m_settings);
        if(choices.isEmpty())choices<<profile;
        for(auto candidate:choices){
            auto name=names.value(providers.indexOf(provider),provider);
            auto id=provider;
            if(provider=="ai"&&!candidate.value("model").toString().isEmpty()){
                name=candidate.value("channelName").toString()+" / "+candidate.value("model").toString();
                id="ai:"+QString::fromLatin1(QJsonDocument(QJsonArray{candidate.value("channelId"),candidate.value("model")}).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
            }
            const auto problem=m_services->configurationProblem(candidate);
            const bool enabled=candidate.value("enabled").toBool(true)&&row.value("enabled").toBool(true);
            auto detail=!enabled?(m_chinese?QString("已停用"):QString("Disabled")):problem;
            if(detail.isEmpty()&&row.value("cooling").toBool())detail=m_chinese?"冷却中；不会跳过限流保护":"Cooling down; rate limits remain active";
            rows<<QVariantMap{{"id",id},{"name",name},{"detail",detail},{"selectable",enabled&&problem.isEmpty()}};
            if(enabled&&problem.isEmpty())m_wordProviderProfiles[id]=candidate;
        }
    }
    if(!m_wordProviderChoice.isEmpty()&&!m_wordProviderProfiles.contains(m_wordProviderChoice))m_wordProviderChoice.clear();
    QString name=m_chinese?"自动选择渠道":"Automatic service";
    for(auto value:rows){auto row=value.toMap();if(row.value("id").toString()==m_wordProviderChoice){name=row.value("name").toString();break;}}
    m_wordBackend.update({{"providerOptions",rows},{"providerChoice",m_wordProviderChoice},{"providerChoiceLabel",name}});
}
void App::translateWord(QString text,bool retainResult)
{
    text=text.trimmed();if(m_wordCancel)m_wordCancel->store(true);m_wordCancel=token();auto cancel=m_wordCancel;auto epoch=++m_wordEpoch;
    m_wordSource=text;m_wordBackend.update({{"sourceText",text},
        {"loading",!text.isEmpty()},{"status",text.isEmpty()?"没有读取到剪贴板文字":"正在翻译…"}});
    if(!retainResult)m_wordBackend.update({{"translatedText",""},{"alignmentSource",""},{"alignmentRows",QVariantList()},
        {"providerSwitching",false},{"providerSwitchError",false},{"provider",""},{"providerNote",""},{"providerFallback",false}});
    if(text.isEmpty())return;
    auto services=m_services;auto settings=m_settings;auto command=m_wordMode;
    if(!m_wordProviderChoice.isEmpty()&&m_wordProviderProfiles.contains(m_wordProviderChoice)){
        const auto profile=m_wordProviderProfiles.value(m_wordProviderChoice);
        settings["provider"]=profile.value("provider");settings["fallbackEnabled"]=false;
        settings["requestedChannelId"]=profile.value("channelId");settings["requestedModel"]=profile.value("model");
    }
    const auto parts=sentenceParts(text);
    auto publish=[this,text,parts](const QJsonObject& data){
        QVariantList rows;int sourceOffset=0,targetOffset=0;const auto segments=data.value("segments").toArray();
        for(int i=0;i<parts.size()&&i<segments.size();++i){
            const auto translated=segments[i].toString();const auto at=text.indexOf(parts[i],sourceOffset);
            if(at>=0&&!translated.isEmpty())rows<<QVariantMap{{"sourceStart",at},{"sourceEnd",at+parts[i].size()},
                {"targetStart",targetOffset},{"targetEnd",targetOffset+translated.size()}};
            if(at>=0)sourceOffset=at+parts[i].size();targetOffset+=translated.size()+1;
        }
        m_wordBackend.update({{"translatedText",data.value("translated").toString()},{"alignmentSource",text},{"alignmentRows",rows},
            {"provider",providerLabel(data)},{"providerNote",providerNote(data)},{"providerFallback",data.value("fallback").toBool()}});
    };
    background([this,services,settings,text,parts,cancel,epoch,publish]{
        return services->translate(settings,parts,"auto",targetFor(text,settings.value("target").toString()),cancel,{},
            [this,parts,cancel,epoch,publish](const Translation& result,int done,int total){
                auto data=result.json(parts);
                QMetaObject::invokeMethod(this,[this,data,cancel,epoch,done,total,publish]{
                    if(epoch!=m_wordEpoch||cancel->load())return;
                    publish(data);m_wordBackend.update({{"status",QString("已翻译 %1 / %2 段").arg(done).arg(total)}});
                },Qt::QueuedConnection);
            }).json(parts);
    },[this,epoch,cancel,command,publish](QJsonObject data,QString error){
        if(epoch!=m_wordEpoch||cancel->load())return;
        if(error.isEmpty())publish(data);
        const bool switchFailed=m_wordBackend.values.value("providerSwitching").toBool()&&!error.isEmpty();
        m_wordBackend.update({{"loading",false},{"status",error},{"providerSwitching",false},{"providerSwitchError",switchFailed}});
        if(error.isEmpty())m_cache[command]={QDateTime::currentSecsSinceEpoch(),results(data,command)};
    });

}
void App::showPanel(const QString& text)
{
    used();
    if(!m_panel.window)m_panel=view("qrc:/ui/qml/Panel.qml",&m_panelBackend);
    auto screen=QGuiApplication::screenAt(m_mouse());if(!screen)screen=QGuiApplication::primaryScreen();
    auto area=screen->availableGeometry();m_panel.window->setScreen(screen);
    m_panel.window->setMaximumSize(QSize(std::min(1280,area.width()),std::min(1000,area.height())));
    if(!m_panel.window->isVisible()){
        m_panel.window->resize(std::min(960,area.width()-24),std::min(760,area.height()-40));
        m_panel.window->setPosition(area.center()-QPoint(m_panel.window->width()/2,m_panel.window->height()/2));
    }
    m_panelBackend.update({{"menuLeft",area.left()},{"menuRight",area.right()},{"menuTop",area.top()},{"menuBottom",area.bottom()}});
    if(!text.isEmpty())panelAction("input",text);
    m_panel.window->show();m_panel.window->requestActivate();
}
void App::panelAction(const QString& name,const QVariant& value)
{
    used();
    if(name=="close"){
        if(m_panelCancel)m_panelCancel->store(true);++m_panelEpoch;
        if(m_panel.window)m_panel.window->hide();dispose(m_panel);m_panelBackend.update({{"loading",false}});return;
    }
    if(name=="settings"){showSettings();return;}
    if(name=="copy"){QGuiApplication::clipboard()->setText(m_panelBackend.values.value(value.toString()).toString());return;}
    if(name=="stop"){
        if(m_panelCancel)m_panelCancel->store(true);++m_panelEpoch;
        m_panelBackend.update({{"loading",false},{"status","已停止，已完成译文已保留"}});return;
    }
    if(name=="translate"){translatePanel();return;}
    if(name=="panelMode"){
        if(!QStringList{"auto","translate","lookup"}.contains(value.toString()))return;
        m_panelBackend.update({{"panelMode",value}});translatePanel(true);return;
    }
    if(name=="lookupCandidate"){
        auto word=value.toString().trimmed();if(!dictionaryText(word)||word.isEmpty())return;
        auto previous=m_panelBackend.values.value("lookupBack").toString();
        if(previous.isEmpty())previous=m_panelBackend.values.value("sourceText").toString();
        panelAction("input",word);m_panelBackend.update({{"lookupBack",previous},{"panelMode","lookup"}});translatePanel();return;
    }
    if(name=="lookupBack"){
        auto text=m_panelBackend.values.value("lookupBack").toString();if(text.isEmpty())return;
        panelAction("input",text);m_panelBackend.update({{"panelMode","lookup"}});translatePanel();return;
    }
    if(name=="menu")return;
    if(!QStringList{"input","source","target","autoTranslate","explain","sentenceMode"}.contains(name))return;
    if(m_panelCancel)m_panelCancel->store(true);++m_panelEpoch;
    m_panelBackend.update({{name=="input"?"sourceText":name,value},{"loading",false},{"status",""},{"definitionStatus",""},
        {"translatedText",""},{"definition",""},{"provider",""},{"definitionProvider",""},{"detected",""},
        {"sentenceRows",QVariantList()},{"lookupRows",QVariantList()},{"lookupRelated",QVariantList()},{"lookupBack",""},{"lookupRelatedMessage",""},{"lookupMessage",""},{"lookupText",""},{"translationText",""},{"lookupTotal",0},{"lookupFound",0},{"lookupTruncated",false}});
}
void App::translatePanel(bool reuse)
{
    auto text=m_panelBackend.values.value("sourceText").toString().trimmed();
    if(m_panelCancel)m_panelCancel->store(true);m_panelCancel=token();auto cancel=m_panelCancel;auto epoch=++m_panelEpoch;
    auto mode=m_panelBackend.values.value("panelMode").toString();auto words=dictionaryWords(text,2);
    bool lookup=mode=="lookup"||(mode=="auto"&&(chineseDictionaryText(text,8)||(dictionaryText(text)&&words.size()==1&&words[0].toObject().value("count").toInt()==1)));
    m_panelBackend.update({{"panelPage",lookup?"lookup":"translate"},{"loading",false},{"status",""}});
    if(text.isEmpty())return;

    const bool cachedTranslation=reuse&&!lookup&&m_panelBackend.values.value("translationText").toString()==text;
    if(reuse&&lookup&&m_panelBackend.values.value("lookupText").toString()==text)return;
    if(cachedTranslation&&(!m_panelBackend.values.value("explain").toBool()||m_panelBackend.values.value("definitionStatus")!="正在查询释义…"))return;
    if(lookup){
        auto services=m_services;auto settings=m_settings;
        m_panelBackend.update({{"loading",true},{"lookupRows",QVariantList()},{"lookupRelated",QVariantList()},{"lookupRelatedMessage",""},{"lookupMessage",""},{"lookupTotal",0},{"lookupFound",0}});
        auto publish=[this,cancel,epoch](QJsonObject data){
            if(cancel->load()||epoch!=m_panelEpoch)return;
            QStringList copied;for(auto value:data.value("rows").toArray()){
                auto row=value.toObject();if(!row.value("meaning").toString().isEmpty())copied<<row.value("word").toString()+"\n"+row.value("meaning").toString();
            }
            for(auto value:data.value("related").toArray()){
                auto group=value.toObject();QStringList words;
                for(auto word:group["words"].toArray())words<<word.toString();
                for(auto word:group["entries"].toArray())words<<word.toObject()["word"].toString()+" "+word.toObject()["meaning"].toString();
                copied<<group["label"].toString()+" "+group["pos"].toString()+" "+group["meaning"].toString()+"\n"+words.join("; ");
            }
            m_panelBackend.update({{"lookupRows",data.value("rows").toArray().toVariantList()},{"lookupTotal",data.value("total").toInt()},
                {"lookupFound",data.value("found").toInt()},{"lookupTruncated",data.value("truncated").toBool()},{"lookupMessage",data.value("message").toString()},{"lookupCopy",copied.join("\n\n")},
                {"lookupRelated",data.value("related").toArray().toVariantList()},{"lookupRelatedMessage",data.value("relatedMessage").toString()},{"lookupDirection",data.value("direction").toString()}});
        };
        auto original=m_panelBackend.values.value("sourceText").toString();
        background([this,services,settings,original,cancel,publish]{
            return services->lookupWords(settings,original,cancel,[this,publish](QJsonObject data){QMetaObject::invokeMethod(this,[publish,data]{publish(data);},Qt::QueuedConnection);});
        },[this,cancel,epoch,text,publish](QJsonObject data,QString error){
            if(cancel->load()||epoch!=m_panelEpoch)return;
            publish(data);m_panelBackend.update({{"loading",false},{"status",error},{"lookupText",error.isEmpty()?text:QString()}});
        });return;
    }
    auto source=m_panelBackend.values.value("source").toString();auto target=targetFor(text,m_panelBackend.values.value("target").toString());
    auto settings=m_settings;auto services=m_services;
    if(!cachedTranslation){
      m_panelBackend.update({{"loading",true},{"status","正在翻译…"},{"translatedText",""},{"definition",""},{"definitionStatus",""},
        {"provider",""},{"definitionProvider",""},{"detected",source=="auto"?detect(text):source},{"actualTarget",target}});
      const bool sentenceMode=m_panelBackend.values.value("sentenceMode").toBool();
      auto parts=sentenceMode?sentenceParts(text):QStringList{text};
      auto published=std::make_shared<QElapsedTimer>();published->start();
      auto publish=[this,cancel,epoch,parts,sentenceMode,published](QJsonObject data,int done,int total){
          if(cancel->load()||epoch!=m_panelEpoch)return;
          if(done<total&&published->elapsed()<120)return;published->restart();
          QVariantList rows;auto segments=data.value("segments").toArray();
          if(sentenceMode)for(int i=0;i<parts.size();++i)rows<<QVariantMap{{"original",parts[i]},{"translated",(i<segments.size()?segments[i].toString():QString())}};
          m_panelBackend.update({{"translatedText",data.value("translated").toString()},{"sentenceRows",rows},
              {"status",QString("已翻译 %1 / %2 段").arg(done).arg(total)},
              {"provider",providerLabel(data)},{"providerNote",providerNote(data)},{"providerFallback",data.value("fallback").toBool()}});
      };
      background([this,services,settings,parts,source,target,cancel,publish]{
          return services->translate(settings,parts,source,target,cancel,{},[this,publish,parts](const Translation& result,int done,int total){
              auto data=result.json(parts);QMetaObject::invokeMethod(this,[publish,data,done,total]{publish(data,done,total);},Qt::QueuedConnection);
          }).json(parts);
      },[this,cancel,epoch,text,publish,parts](QJsonObject data,QString error){
          if(cancel->load()||epoch!=m_panelEpoch)return;
          if(error.isEmpty())publish(data,parts.size(),parts.size());
          m_panelBackend.update({{"loading",false},{"status",error},{"translationText",error.isEmpty()?text:QString()}});
          if(error.isEmpty())m_panelBackend.update({{"detected",data.value("sourceLanguage").toString()}});
      });
    }
    if(m_panelBackend.values.value("explain").toBool()){
        settings["definition"]=true;settings["fallbackEnabled"]=true;
        // Prefer the offline dictionary for words, otherwise use the configured eligible service.
        if(dictionaryText(text)&&m_services->profile("ecdict").value("enabled").toBool(true)&&dictionaryAvailable(m_services->directory()))settings["provider"]="ecdict";
        m_panelBackend.update({{"definitionStatus","正在查询释义…"}});
        background([services,settings,text,source,target,cancel]{return services->translate(settings,{text},source,target,cancel).json({text});},
            [this,cancel,epoch](QJsonObject data,QString error){
                if(cancel->load()||epoch!=m_panelEpoch)return;
                m_panelBackend.update({{"definition",data.value("translated").toString()},{"definitionStatus",error},
                    {"definitionProvider",providerLabel(data)},{"definitionFallback",data.value("fallback").toBool()}});
            });
    }
}
void App::publishAi()
{
    auto channels=m_drafts.value("ai").toObject().value("channels").toArray();
    m_aiCurrent=std::clamp(m_aiCurrent,0,std::max(0,int(channels.size())-1));
    m_configBackend.update({{"aiChannels",channels.toVariantList()},{"aiCurrent",m_aiCurrent}});
}
void App::aiAction(const QString& name,const QVariant& value)
{
    auto ai=m_drafts.value("ai").toObject();auto channels=ai.value("channels").toArray();auto input=value.toMap();
    if(name=="aiSelect"){m_aiCurrent=value.toInt();publishAi();return;}
    if(name=="aiAdd"){
        if(channels.size()>=32)throw std::runtime_error("AI 渠道最多 32 个");
        channels.append(QJsonObject{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"name",QString("AI 渠道 %1").arg(channels.size()+1)},
            {"endpoint",""},{"apiKey",""},{"enabled",true},{"models",QJsonArray{QJsonObject{{"name",""},{"enabled",true}}}}});m_aiCurrent=channels.size()-1;
    }else if(name=="aiRemove"){int index=value.toInt();if(index>=0&&index<channels.size())channels.removeAt(index);}
    else if(name=="aiMove"){
        int from=input.value("from").toInt(),to=input.value("to").toInt();
        if(from<0||from>=channels.size()||to<0||to>=channels.size())return;
        auto c=channels.takeAt(from);channels.insert(to,c);m_aiCurrent=to;
    }else{
        if(m_aiCurrent<0||m_aiCurrent>=channels.size())return;auto channel=channels[m_aiCurrent].toObject();
        if(name=="aiField"){
            auto key=input.value("key").toString();if(!QStringList{"name","endpoint","apiKey","enabled"}.contains(key))return;
            channel[key]=QJsonValue::fromVariant(input.value("value"));
        }else{
            auto models=channel.value("models").toArray();int index=input.value("index").toInt();
            if(name=="aiModelAdd"){if(models.size()>=32)throw std::runtime_error("每个渠道最多 32 个模型");models.append(QJsonObject{{"name",""},{"enabled",true}});}
            else if(index>=0&&index<models.size()){
                if(name=="aiModelRemove")models.removeAt(index);
                else if(name=="aiModelMove"){
                    int to=input.value("to").toInt();if(to<0||to>=models.size())return;auto m=models.takeAt(index);models.insert(to,m);
                }else if(name=="aiModelField"){
                    auto key=input.value("key").toString();if(key!="name"&&key!="enabled")return;
                    auto m=models[index].toObject();m[key]=QJsonValue::fromVariant(input.value("value"));models[index]=m;
                }
            }
            channel["models"]=models;
        }channels[m_aiCurrent]=channel;
    }
    ai["channels"]=channels;m_drafts["ai"]=ai;publishAi();
    for(auto it=m_tests.begin();it!=m_tests.end();)if(it->first=="ai"||it->first.startsWith("ai:"))it=m_tests.erase(it);else ++it;
    publishTests();
}
void App::showSettings()
{
    used();
    if(m_config.window){m_config.window->show();m_config.window->requestActivate();return;}
    ++m_configEpoch;m_drafts={};m_tests.clear();m_batch=false;
    m_routingDraft=m_services->routing();
    for(auto p:providers)m_drafts[p]=m_services->profile(p);
    auto ai=m_drafts.value("ai").toObject();
    if(!ai.contains("channels"))ai["channels"]=QJsonArray{QJsonObject{{"id","legacy"},{"name","默认 AI"},{"enabled",true},
        {"endpoint",ai.value("endpoint")},{"apiKey",ai.value("apiKey")},{"models",QJsonArray{QJsonObject{{"name",ai.value("model")},{"enabled",true}}}}}};
    m_drafts["ai"]=ai;m_aiCurrent=0;publishAi();
    m_configBackend.update({{"testResults",QVariantList()},{"showTestResults",false},{"testing",false},{"batch",false},{"message",""}});
    configSelect(m_settings.value("provider").toString("ecdict"));
    m_configBackend.update({{"home",true}});publishTests();
    m_config=view("qrc:/ui/Settings.qml",&m_configBackend);m_config.window->show();m_config.window->requestActivate();
}
void App::configSelect(const QString& p)
{
    if(!providers.contains(p))return;m_configProvider=p;
    if(!m_drafts.contains(p))m_drafts[p]=m_services->profile(p);
    auto draft=m_drafts.value(p).toObject();QVariantMap state=draft.toVariantMap();
    state["current"]=providers.indexOf(p);state["home"]=false;state["endpointHint"]=endpoints[providers.indexOf(p)];
    state["modelHint"]="填写服务商提供的模型名 / 接入点 ID";
    if(p=="youdao"&&!draft.value("appId").toString().isEmpty())state["endpointHint"]="https://openapi.youdao.com/api";
    state["today"]=QString("%1 / 5,000").arg(m_services->today());state["showTestResults"]=false;state["message"]=m_tests[p].value("detail","");
    m_configBackend.update(state);publishDictionary();
}
void App::publishDictionary()
{
    m_configBackend.update({{"dictionaryReady",dictionaryAvailable(m_services->directory())},
        {"dictionaryDownloading",bool(m_dictionaryCancel)},{"dictionaryReceived",m_dictionaryReceived},
        {"dictionarySize",m_dictionarySource.size},{"dictionaryMessage",m_dictionaryMessage}});
}
void App::startDictionaryDownload()
{
    if(m_dictionaryCancel)return;
    used();auto cancel=token();m_dictionaryCancel=cancel;m_dictionaryReceived=0;m_dictionaryMessage.clear();publishDictionary();
    auto directory=m_services->directory();auto proxy=m_settings.value("proxy").toString();auto source=m_dictionarySource;
    QPointer<App> self(this);
    background([directory,proxy,cancel,source,self]{
        QElapsedTimer throttle;throttle.start();
        downloadDictionary(directory,proxy,cancel,[&](qint64 received,qint64 total){
            if(received!=total&&throttle.elapsed()<100)return;throttle.restart();
            QMetaObject::invokeMethod(qApp,[self,cancel,received]{
                if(!self||self->m_dictionaryCancel!=cancel)return;
                self->m_dictionaryReceived=received;self->publishDictionary();
            },Qt::QueuedConnection);
        },source);return true;
    },[self,cancel](bool,QString error){
        if(!self||self->m_dictionaryCancel!=cancel)return;
        self->m_dictionaryCancel.reset();self->m_dictionaryMessage=error;
        self->m_tests.erase("ecdict");self->publishDictionary();self->publishTests();self->used();
    });
}
void App::configAction(const QString& name,const QVariant& value)
{
    try{
        if(name=="openPanel"){showPanel();return;}
        if(name.startsWith("ai")){aiAction(name,value);return;}
        if(name=="enabled"){
            auto data=value.toMap();auto provider=data.value("provider").toString();if(!providers.contains(provider))return;
            auto profile=m_drafts.value(provider).toObject();profile["enabled"]=data.value("enabled").toBool();m_drafts[provider]=profile;
            for(auto it=m_tests.begin();it!=m_tests.end();)if(it->first==provider||(provider=="ai"&&it->first.startsWith("ai:")))it=m_tests.erase(it);else ++it;
            if(provider==m_configProvider)m_configBackend.update({{"enabled",profile["enabled"].toBool()}});
            publishTests();return;
        }
        if(name=="select")configSelect(providers.value(value.toInt()));
        else if(name=="selectProvider")configSelect(value.toString());
        else if(name=="home"){m_configBackend.update({{"home",true}});publishTests();}
        else if(name=="refreshQueue")publishTests();
        else if(name=="downloadDictionary")startDictionaryDownload();
        else if(name=="cancelDictionary"){
            if(m_dictionaryCancel)m_dictionaryCancel->store(true);
        }
        else if(name=="selectionIconEnabled"){
            m_routingDraft["selectionIconEnabled"]=value.toBool();publishTests();
        }
        else if(name=="routingMode"){
            if(value.toString()!="manual"&&value.toString()!="auto")return;
            m_routingDraft["mode"]=value.toString();publishTests();
        }else if(name=="adjustProviderWeight"){
            if(m_routingDraft.value("mode")!="auto")return;
            auto move=value.toMap();const auto provider=move.value("provider").toString();
            if(!providers.contains(provider)||provider==canonicalProvider(m_settings.value("provider").toString("ecdict")))return;
            auto adjustments=m_routingDraft.value("adjustments").toObject();
            const int adjustment=std::clamp(move.value("adjustment").toInt(),-10000,10000);
            if(adjustment)adjustments[provider]=adjustment;else adjustments.remove(provider);
            m_routingDraft["adjustments"]=adjustments;publishTests();
        }else if(name=="moveProvider"){
            if(m_routingDraft.value("mode")!="manual")return;
            auto move=value.toMap();auto from=move.value("provider").toString(),before=move.value("before").toString();
            if(!providers.contains(from)||from==m_settings.value("provider").toString()||before==m_settings.value("provider").toString())return;
            if(!before.isEmpty()&&!providers.contains(before))return;
            QStringList order;for(auto p:m_routingDraft.value("order").toArray())order<<p.toString();
            if(from==before)return;order.removeAll(from);int index=order.indexOf(before);order.insert(index<0?order.size():index,from);
            m_routingDraft["order"]=QJsonArray::fromStringList(order);publishTests();
        }
        else if(QStringList{"apiKey","endpoint","model","region","appId"}.contains(name)){
            auto draft=m_drafts.value(m_configProvider).toObject();auto next=QJsonValue::fromVariant(value);
            if(draft.value(name)!=next)m_tests.erase(m_configProvider);
            draft[name]=next;m_drafts[m_configProvider]=draft;
            if(name=="appId")m_configBackend.update({{"endpointHint",value.toString().trimmed().isEmpty()?"https://dict.youdao.com/jsonapi":"https://openapi.youdao.com/api"}});
        }else if(name=="save"){
            m_services->save(m_drafts,m_routingDraft);m_cache.clear();
            m_settings["selectionIconEnabled"]=m_services->routing().value("selectionIconEnabled").toBool(true);updateSelectionMonitor();
            publishTests();
            const auto saved=m_chinese?QString("配置已保存"):QString("Settings saved");
            m_configBackend.update({{"message",saved}});
            m_protocol.notify("winshun/toast",{{"message",saved}});
        }
        else if(name=="test"||name=="testAll")testServices(name=="testAll");
        else if(name=="testService")testServices(false,value.toString());
        else if(name=="showTests")configAction("home",{});
        else if(name=="close"){
            used();
            if(m_config.window)m_config.window->hide();++m_configEpoch;
            for(auto c:m_testCancels)c->store(true);m_testCancels.clear();m_testQueue.clear();m_testRunning=0;
            dispose(m_config);m_drafts={};m_testProfiles={};m_configBackend.update({{"apiKey",""},{"aiChannels",QVariantList()}});
        }
    }catch(const std::exception& e){m_configBackend.update({{"message",failure(e)}});}
}
void App::testServices(bool all,const QString& provider)
{
    if(m_testRunning||!m_testQueue.isEmpty())return;
    auto single=provider.isEmpty()?m_configProvider:provider;if(!all&&!providers.contains(single))return;
    m_batch=all;if(all)m_tests.clear();m_testQueue.clear();m_testProfiles={};
    if(!all)for(auto it=m_tests.begin();it!=m_tests.end();)if(it->first==single||(single=="ai"&&it->first.startsWith("ai:")))it=m_tests.erase(it);else ++it;
    for(auto p:all?providers:QStringList{single}){
        if(!m_drafts.contains(p))m_drafts[p]=m_services->profile(p);
        auto profile=m_drafts.value(p).toObject();auto candidates=m_services->candidates(profile,m_settings,m_routingDraft);
        if(candidates.isEmpty()){m_tests[p]={{"status","skipped"},{"detail","已禁用或没有启用的模型"}};continue;}
        for(auto config:candidates){
            auto id=p=="ai"?"ai:"+config.value("channelId").toString()+":"+config.value("model").toString():p;
            m_testProfiles[id]=config;m_testQueue<<id;m_tests[id]={{"status","waiting"},{"detail","等待测试"},
                {"name",p=="ai"?config.value("channelName").toString()+" / "+config.value("model").toString():p}};
        }
    }pumpTests();
}
void App::pumpTests()
{
    while(m_testRunning<3&&!m_testQueue.isEmpty()){
        auto p=m_testQueue.takeFirst();auto draft=m_testProfiles.value(p).toObject();
        auto problem=m_services->configurationProblem(draft);
        if(!problem.isEmpty()){
            m_tests[p]={{"status","skipped"},{"detail",problem+"，已跳过"}};continue;
        }
        ++m_testRunning;m_tests[p]={{"status","running"},{"detail","测试中…"}};
        auto services=m_services;auto settings=m_settings;auto cancel=token();m_testCancels.push_back(cancel);auto epoch=m_configEpoch;
        auto started=std::make_shared<QElapsedTimer>();started->start();
        background([services,settings,draft,cancel]{return services->translate(settings,{"hello"},"en","zh",cancel,draft);},
            [this,p,cancel,epoch,started,draft](Translation data,QString error){
                if(epoch!=m_configEpoch||cancel->load())return;--m_testRunning;
                if(m_testProfiles.value(p).toObject()==draft)m_tests[p]={{"status",error.isEmpty()?"success":"failed"},{"elapsedMs",started->elapsed()},{"checkedAt",QDateTime::currentMSecsSinceEpoch()},
                    {"detail",error.isEmpty()?"连接成功："+data.segments.join('\n'):error}};
                pumpTests();
            });
    }publishTests();
}
void App::publishTests()
{
    publishDictionary();
    QVariantList aiTests;int aiComplete=0,aiSuccess=0,aiFailed=0;bool aiRunning=false,aiWaiting=false;qint64 aiChecked=0;
    for(auto& [id,test]:m_tests)if(id.startsWith("ai:")){
        auto r=test;auto config=m_testProfiles.value(id).toObject();r["name"]=config.value("channelName").toString()+" / "+config.value("model").toString();aiTests<<r;
        auto status=test.value("status").toString();aiRunning|=status=="running";aiWaiting|=status=="waiting";
        if(status!="waiting"&&status!="running")++aiComplete;
        if(status=="success")++aiSuccess;if(status=="failed")++aiFailed;
        aiChecked=std::max(aiChecked,test.value("checkedAt").toLongLong());
    }
    if(!aiTests.isEmpty()){
        QVariantMap summary{{"status",aiRunning?"running":aiWaiting?"waiting":aiSuccess?"success":aiFailed?"failed":"skipped"},
            {"detail",QString("已测试 %1/%2 个模型，%3 个可用").arg(aiComplete).arg(aiTests.size()).arg(aiSuccess)}};
        if(!aiRunning&&!aiWaiting&&aiChecked)summary["checkedAt"]=aiChecked;m_tests["ai"]=summary;
    }
    QVariantList rows;int completed=0,total=0;
    for(auto& [id,test]:m_tests){
        if(id=="ai"&&!aiTests.isEmpty())continue;++total;
        if(test.value("status")!="waiting"&&test.value("status")!="running")++completed;
    }
    for(auto p:providers){
        auto it=m_tests.find(p);if(it==m_tests.end())continue;
        auto row=it->second;row["provider"]=p;row["name"]=m_configBackend.values["names"].toStringList().value(providers.indexOf(p));
        rows.append(row);
    }
    QVariantList queue;int ready=0,cooling=0,unconfigured=0;
    for(auto value:m_services->queue(m_settings,m_drafts,m_routingDraft)){
        auto row=value.toObject().toVariantMap();auto p=row["provider"].toString();
        row["name"]=m_configBackend.values["names"].toStringList().value(providers.indexOf(p));
        if(!row["enabled"].toBool()){}else if(!row["configured"].toBool())++unconfigured;else if(row["cooling"].toBool())++cooling;else ++ready;
        if(auto it=m_tests.find(p);it!=m_tests.end()){
            if(!it->second.contains("checkedAt")||it->second.value("checkedAt").toLongLong()>=row.value("checkedAt").toLongLong())
                for(auto entry=it->second.begin();entry!=it->second.end();++entry)row[entry.key()]=entry.value();
        }
        queue.append(row);
    }
    m_configBackend.update({{"aiTests",aiTests},{"testResults",rows},{"testing",m_testRunning>0||!m_testQueue.isEmpty()},{"batch",m_batch},
        {"queue",queue},{"routingMode",m_routingDraft.value("mode").toString("manual")},{"readyCount",ready},{"coolingCount",cooling},{"unconfiguredCount",unconfigured},
        {"selectionIconEnabled",m_routingDraft.value("selectionIconEnabled").toBool(true)},
        {"fallbackEnabled",m_settings.value("fallbackEnabled").toBool(true)},{"recoveryMinutes",std::clamp(m_settings.value("fallbackRecoveryMinutes").toDouble(60),1.0,10080.0)},
        {"message",m_batch?QString("已测试 %1/%2").arg(completed).arg(total):m_tests.contains(m_configProvider)?m_tests.at(m_configProvider).value("detail").toString():QString()}});
}
}

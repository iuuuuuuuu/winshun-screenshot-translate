#include "App.h"
#include "CaptureKeys.h"
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QScreen>
#include <QJsonDocument>
#include <QPointer>
#include <QKeySequence>
#include <QUrl>
#include <QDebug>
#include <dwmapi.h>
#include <thread>
#include <cstdio>
namespace st {
namespace {
QJsonObject row(QString id,QString title,QString subtitle={},bool valid=true)
{return {{"id",id},{"title",title},{"subtitle",subtitle},{"valid",valid},{"icon","glyph:U+E8C1"}};}
QJsonObject effect(const QString& type){return {{"type",type}};}
bool hasId(const QJsonValue& id){return !id.isNull()&&!id.isUndefined();}
Cancel token(){return std::make_shared<std::atomic_bool>(false);}
QRect rect(RECT r){return {r.left,r.top,r.right-r.left,r.bottom-r.top};}
QString failure(const std::exception& e){return QString::fromUtf8(e.what());}
template<class Work,class Done> void background(Work work,Done done)
{
    std::thread([work=std::move(work),done=std::move(done)]() mutable {
        using Result=decltype(work());Result value{};QString error;
        try{value=work();}catch(const std::exception& e){error=failure(e);}catch(...){error="操作失败";}
        QMetaObject::invokeMethod(qApp,[done=std::move(done),value=std::move(value),error]()mutable{done(value,error);},Qt::QueuedConnection);
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
}
}
App::App(ws::captureplugin::Protocol& protocol,QString appDir,QJsonObject initial):m_appDir(std::move(appDir)),m_protocol(protocol)
{
    defaults(initial.value("settings").toObject());m_hostHotkeys=initial.value("capabilities").toObject().value("hostHotkeys").toBool();
    QString dir=qEnvironmentVariable("WS_DATA_DIR",initial.value("dataDir").toString());
    if(dir.isEmpty())dir=qEnvironmentVariable("LOCALAPPDATA")+"/WinShun/plugins/iuuuuuuuu.screenshot-translate.data";
    m_services=std::make_shared<Services>(dir);
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
    m_wordBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"pinned",false},{"loading",false},
        {"sourceText",""},{"translatedText",""},{"status",""},{"provider",""},{"mode","clip"}});
    QObject::connect(&m_wordBackend,&Backend::invoked,this,&App::wordAction);
    m_followSelection.setInterval(450);
    QObject::connect(&m_followSelection,&QTimer::timeout,this,[this]{readSelection(true);});
    registerKeys();
}
void App::defaults(const QJsonObject& settings)
{
    m_settings={{"provider","mymemory"},{"proxy",""},{"target","auto"},{"fallbackEnabled",true},{"fallbackRecoveryMinutes",10},
        {"comparisonOrder","translation-first"},{"closeOnOutsideClick",true},{"shotMode","multi"},{"shotHotkey","Alt+Shift+T"},{"clipHotkey","Alt+Shift+C"},{"selectionHotkey","Alt+Shift+S"},
        {"wordOcrHotkey","Alt+Shift+O"}};
    for(auto it=settings.begin();it!=settings.end();++it)m_settings[it.key()]=it.value();
}
void App::appearance(const QJsonObject& params)
{
    if(params.contains("locale"))m_chinese=!params.value("locale").toString().startsWith("en");
    if(params.contains("theme"))m_dark=params.value("theme").toString()=="dark";
    QStringList labels=m_chinese?QStringList{"自动","中文","英文","日文","韩文","法文","德文","西班牙文","俄文"}:
        QStringList{"Automatic","Chinese","English","Japanese","Korean","French","German","Spanish","Russian"};
    m_pinBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"sourceLabels",labels}});
    auto serviceNames=names;if(m_chinese){serviceNames[4]="微软";serviceNames[8]="豆包";serviceNames[9]="混元";}
    m_configBackend.update({{"chinese",m_chinese},{"dark",m_dark},{"names",serviceNames}});
    m_wordBackend.update({{"chinese",m_chinese},{"dark",m_dark}});
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
    if(v.window)v.window->hide();
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
        m_protocol.tool("register_hotkeys",args,[this,revision,count=keys.size()](QJsonValue result,QString error){
            if(revision!=m_keyRevision)return;
            if(!error.isEmpty()){m_hostHotkeys=false;registerKeys();return;}
            auto failures=result.toObject().value("errors").toObject();
            std::fprintf(stderr,"[screenshot-translate] hotkeys=host registered=%lld failed=%lld\n",
                static_cast<long long>(count-failures.size()),static_cast<long long>(failures.size()));
            if(!failures.isEmpty())m_protocol.notify("winshun/notify",{{"message","部分截图翻译热键注册失败；请检查快捷键冲突"}});
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
        auto item=row(field,data.value(field).toString(),data.value("sourceLanguage").toString()+" → "+data.value("targetLanguage").toString());
        item["copy"]=data.value(field);item["data"]=d;item["actions"]=actions;items.append(item);
    }return items;
}
void App::query(const QJsonValue& id,const QJsonObject& params)
{
    QString command=params.value("command").toString(),text=params.value("text").toString().trimmed();
    int qid=params.value("queryId").toInt();QJsonArray items;
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
            if(action.startsWith("copy-")){
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
            defaults(params.value("settings").toObject());registerKeys();
            if(m_pin){render();m_pinOpened=GetTickCount();}
        }else if(method=="appearance")appearance(params);
        else if(method=="shutdown"){
            if(hasId(id))m_protocol.respond(id,QJsonObject());closeWord();closePin();dispose(m_config);ExitProcess(0);
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
    closePin();m_busy=true;HWND foreground=GetForegroundWindow();auto epoch=++m_epoch;
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
    m_original=image;m_translated=image;m_blocks.clear();m_data={};m_compare=false;m_fullText=false;m_cancel=token();m_pinOpened=GetTickCount();
    auto provider=m_settings.value("provider").toString("mymemory");
    auto captureScreen=screenAt(area.center());
    m_pinBackend.update({{"translatedText",""},{"sourceText",""},{"ready",false},{"loading",true},{"comparison",false},{"fullText",false},
        {"provider",names.value(providers.indexOf(provider),provider)},{"providerNote",""},{"source","auto"},{"target","zh"},
        {"captureWidth",area.width()/captureScreen->devicePixelRatio()},{"captureHeight",area.height()/captureScreen->devicePixelRatio()},
        {"loadingStage","recognize"},{"status",""}});
    ws::capture::PinWindow::Host host;
    host.closed=[this](auto*){QTimer::singleShot(0,this,[this]{closePin();});};
    host.copy=[this](auto*){pinAction("copy",false);};
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
            if(m_pin&&contains(m_pin->hwnd()))return;
            for(auto v:{&m_bar,&m_status,&m_config,&m_text,&m_menu,&m_word}){
                if(!v->engine)continue;
                for(auto window:v->engine->findChildren<QQuickWindow*>())
                    if(window->isVisible()&&contains(HWND(window->winId())))return;
            }
            DWORD pid=0;auto clicked=WindowFromPoint(point);GetWindowThreadProcessId(clicked,&pid);
            if(pid==GetCurrentProcessId())return;
            if(m_word.window&&m_word.window->isVisible()&&!m_wordPinned&&m_settings.value("closeOnOutsideClick").toBool(true)&&LONG(timestamp-m_wordOpened)>=150)closeWord();
            if(m_pin&&m_settings.value("closeOnOutsideClick").toBool(true)&&LONG(timestamp-m_pinOpened)>=150)closePin();
        },Qt::QueuedConnection);
    });
}
void App::closePin()
{
    if(m_bar.window)m_bar.window->hide();if(m_status.window)m_status.window->hide();if(m_text.window)m_text.window->hide();if(m_menu.window)m_menu.window->hide();
    if(m_pin)m_pin->setShown(false);
    if(m_cancel)m_cancel->store(true);++m_epoch;m_busy=false;m_position.stop();
    if(!m_word.window||!m_word.window->isVisible())m_raw.reset();
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
    m_busy=true;m_pinBackend.update({{"loadingStage","translate"}});
    status(m_chinese?"正在翻译…":"Translating…");m_source=source;m_target=target;
    m_pinBackend.update({{"source",source},{"target",target},{"ready",true}});
    auto services=m_services;auto settings=m_settings;auto cancel=m_cancel;auto epoch=m_epoch;
    background([services,settings,parts,source,target,cancel]{return services->translate(settings,parts,source,target,cancel).json(parts);},
        [this,parts,cancel,epoch](QJsonObject data,QString error){
            if(!m_pin||cancel->load()||epoch!=m_epoch)return;m_busy=false;
            if(!error.isEmpty()){status(error,false);return;}
            QStringList original;for(auto b:m_blocks)original<<b.text;
            data["source"]=original.join('\n');m_data=data;
            auto p=data.value("provider").toString(),label=names.value(providers.indexOf(p),p);
            QString note=data.value("fallback").toBool()?"临时使用 "+label+"；间隔到后自动重试主渠道":label;
            m_pinBackend.update({{"translatedText",data.value("translated").toString()},{"sourceText",data.value("source").toString()},
                {"provider",label},{"providerNote",note},{"source",data.value("sourceLanguage").toString()},{"target",data.value("targetLanguage").toString()}});
            status("",false);render();
        });
}
void App::render()
{
    if(!m_pin)return;QStringList segments;for(auto v:m_data.value("segments").toArray())segments<<v.toString();
    m_translated=segments.isEmpty()?m_original:translatedImage(m_original,m_blocks,segments,m_dark);
    auto image=m_compare?comparisonImage(m_original,m_translated,m_settings.value("comparisonOrder").toString()!="source-first",m_dark,m_chinese):m_translated;
    m_pin->setPicture(image,{});m_pinBackend.update({{"comparison",m_compare}});place();
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
            if(!m_text.window)m_text=view("qrc:/ui/qml/Text.qml",&m_pinBackend);
            bool showing=!m_text.window->isVisible();m_text.window->setVisible(showing);
            if(showing){m_text.window->setPosition(m_bar.window->position()+QPoint(0,54));m_text.window->requestActivate();}
            m_pinBackend.update({{"fullText",showing}});
        }
    }catch(const std::exception& e){status(failure(e),false);m_busy=false;}
}
void App::textFlow(const QString& command)
{
    // A selector must keep a clean frozen desktop. Ordinary word requests and
    // screenshot translation otherwise have independent cancellation/state.
    if(m_selector)return;
    auto point=m_mouse();
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
        int width=qRound(560*screen->devicePixelRatio()),height=qRound(180*screen->devicePixelRatio());
        QRect crop(point-QPoint(width/2,height/2),QSize(width,height));crop=crop.intersected(monitorRect(p));
        QImage image;
        try{image=m_capture(crop);}catch(const std::exception& e){
            openWord("ocr",point);m_wordBackend.update({{"loading",false},{"status",failure(e)}});return;
        }
        openWord("ocr",point);cancel=m_wordCancel;auto activeEpoch=m_wordEpoch;
        background([image,cancel,at=point-crop.topLeft()]{return wordAt(recognize(image,cancel,false),at);},
            [this,cancel,activeEpoch,crop](Block word,QString error){
                if(activeEpoch!=m_wordEpoch||cancel->load())return;
                if(!error.isEmpty()||word.text.isEmpty()){
                    m_wordBackend.update({{"loading",false},{"status",error.isEmpty()?
                        QString("鼠标处没有识别到单词，请把鼠标停在文字上再试"):error}});return;
                }
                if(!m_wordPinned){m_wordSelection=word.box.toAlignedRect().translated(crop.topLeft());placeWord();}
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
    if(m_wordCancel)m_wordCancel->store(true);m_wordCancel=token();++m_wordEpoch;
    m_wordMode=mode;
    if(!m_wordPinned){m_wordAnchor=anchor;m_wordSelection=selection;}
    m_wordSource.clear();m_lastSelection.clear();m_wordOpened=GetTickCount();
    m_followBaseline=m_wordPinned&&mode!="jsel";
    m_wordBackend.update({{"mode",mode},{"sourceText",""},{"translatedText",""},{"provider",""},{"loading",true},
        {"status",mode=="jsel"?"正在读取选中文字…":mode=="ocr"?"正在识别鼠标处的单词…":"正在翻译…"}});
    if(!m_word.window){
        m_word=view("qrc:/ui/qml/Word.qml",&m_wordBackend);
        QObject::connect(m_word.window,&QWindow::heightChanged,this,[this]{placeWord();});
    }
    placeWord();m_word.window->show();ensureRaw();
}
void App::placeWord()
{
    if(!m_word.window||m_wordPinned)return;
    auto point=m_wordSelection.isEmpty()?m_wordAnchor:m_wordSelection.bottomLeft();
    auto s=screenAt(point);m_word.window->setScreen(s);auto work=s->availableGeometry();
    auto at=logical(point,s)+QPoint(10,10);
    if(at.y()+m_word.window->height()>work.bottom()-4){
        auto top=m_wordSelection.isEmpty()?m_wordAnchor:m_wordSelection.topLeft();
        at.setY(logical(top,s).y()-m_word.window->height()-10);
    }
    at.setX(std::clamp(at.x(),work.left()+4,std::max(work.left()+4,work.right()-m_word.window->width()-4)));
    at.setY(std::clamp(at.y(),work.top()+4,std::max(work.top()+4,work.bottom()-m_word.window->height()-4)));
    m_word.window->setPosition(at);
}
void App::closeWord()
{
    if(m_word.window)m_word.window->hide();
    if(m_wordCancel)m_wordCancel->store(true);++m_wordEpoch;
    m_wordPinned=false;m_followSelection.stop();m_wordSource.clear();m_lastSelection.clear();
    m_wordBackend.update({{"pinned",false},{"loading",false}});
    if(!m_pin)m_raw.reset();
}
void App::wordAction(const QString& name,const QVariant&)
{
    if(name=="close")closeWord();
    else if(name=="pin"){
        m_wordPinned=!m_wordPinned;m_wordBackend.update({{"pinned",m_wordPinned}});
        if(m_wordPinned){
            m_followBaseline=m_lastSelection.isEmpty();
            m_followSelection.start();readSelection(true);
        }else m_followSelection.stop();
        m_wordOpened=GetTickCount();
    }else if(name=="retry"){
        if(m_wordSource.isEmpty())textFlow(m_wordMode);else translateWord(m_wordSource);
    }else if(name=="copy"){
        auto text=m_wordBackend.values.value("translatedText").toString();
        if(!text.isEmpty())m_protocol.tool("clipboard_copy",{{"text",text}});
    }
}
void App::readSelection(bool follow)
{
    if(!m_word.window||!m_word.window->isVisible()||(follow&&!m_wordPinned))return;
    if(m_selectionReading){
        if(!follow)m_wordBackend.update({{"loading",false},{"status","上一次选区读取尚未结束，请稍后重试；也可用鼠标 OCR"}});
        return;
    }
    m_selectionReading=true;auto epoch=m_wordEpoch;auto reader=m_readSelection;auto point=m_mouse();auto cancel=m_wordCancel;
    background([reader,point]{return reader(POINT{point.x(),point.y()});},
        [this,follow,epoch,cancel](Selection selection,QString error){
            m_selectionReading=false;
            if(epoch!=m_wordEpoch||(!follow&&cancel->load())||!m_word.window->isVisible())return;
            auto text=selection.text.trimmed();
            if(follow){
                if(m_followBaseline){m_followBaseline=false;m_lastSelection=text;return;}
                if(!error.isEmpty()||text.isEmpty())return;
                if(text==m_lastSelection||text==m_wordSource)return;
            }else if(!error.isEmpty()||text.isEmpty()){
                m_wordBackend.update({{"loading",false},{"status",error.isEmpty()?
                    QString("没有读到选区。此应用可能不支持划词，请用鼠标 OCR 热键 %1").arg(m_settings.value("wordOcrHotkey").toString()):error}});
                return;
            }
            m_lastSelection=text;
            if(!m_wordPinned){m_wordSelection=selection.area;placeWord();}
            translateWord(text);
        });
    if(!follow)QTimer::singleShot(6000,this,[this,epoch,cancel]{
        if(epoch!=m_wordEpoch||cancel->load())return;cancel->store(true);
        m_wordBackend.update({{"loading",false},{"status","选区读取超时，请尝试鼠标 OCR"}});
    });
}
void App::translateWord(QString text)
{
    text=text.trimmed();if(m_wordCancel)m_wordCancel->store(true);m_wordCancel=token();auto cancel=m_wordCancel;auto epoch=++m_wordEpoch;
    m_wordSource=text;m_wordBackend.update({{"sourceText",text},{"translatedText",""},{"provider",""},
        {"loading",!text.isEmpty()},{"status",text.isEmpty()?"没有读取到剪贴板文字":"正在翻译…"}});
    if(text.isEmpty())return;
    auto services=m_services;auto settings=m_settings;auto command=m_wordMode;
    background([services,settings,text,cancel]{
        return services->translate(settings,{text},"auto",targetFor(text,settings.value("target").toString()),cancel).json({text});
    },[this,epoch,cancel,command](QJsonObject data,QString error){
        if(epoch!=m_wordEpoch||cancel->load())return;
        auto p=data.value("provider").toString(),label=names.value(providers.indexOf(p),p);
        m_wordBackend.update({{"translatedText",data.value("translated").toString()},{"loading",false},{"status",error},
            {"provider",data.value("fallback").toBool()?"临时使用 "+label:label}});
        if(error.isEmpty())m_cache[command]={QDateTime::currentSecsSinceEpoch(),results(data,command)};
    });
    QTimer::singleShot(25000,this,[this,epoch,cancel]{
        if(epoch!=m_wordEpoch||cancel->load()||!m_wordBackend.values.value("loading").toBool())return;
        cancel->store(true);m_wordBackend.update({{"loading",false},{"status","翻译超时，请重试或更换渠道"}});
    });
}
void App::showSettings()
{
    if(m_config.window){m_config.window->show();m_config.window->requestActivate();return;}
    ++m_configEpoch;m_drafts={};m_tests.clear();m_batch=false;
    m_configBackend.update({{"testResults",QVariantList()},{"showTestResults",false},{"testing",false},{"batch",false},{"message",""}});
    configSelect(m_settings.value("provider").toString("mymemory"));
    m_config=view("qrc:/ui/Settings.qml",&m_configBackend);m_config.window->show();m_config.window->requestActivate();
}
void App::configSelect(const QString& p)
{
    if(!providers.contains(p))return;m_configProvider=p;
    if(!m_drafts.contains(p))m_drafts[p]=m_services->profile(p);
    auto draft=m_drafts.value(p).toObject();QVariantMap state=draft.toVariantMap();
    state["current"]=providers.indexOf(p);state["endpointHint"]=endpoints[providers.indexOf(p)];
    state["modelHint"]=p=="deepseek"?"deepseek-flash":p=="hunyuan"?"hunyuan-turbos-latest":p=="doubao"?"模型 / 接入点 ID":"gpt-4o-mini";
    state["today"]=QString("%1 / 5,000").arg(m_services->today());state["showTestResults"]=false;state["message"]=m_tests[p].value("detail","");
    m_configBackend.update(state);
}
void App::configAction(const QString& name,const QVariant& value)
{
    try{
        if(name=="select")configSelect(providers.value(value.toInt()));
        else if(QStringList{"apiKey","endpoint","model","region"}.contains(name)){
            auto draft=m_drafts.value(m_configProvider).toObject();draft[name]=QJsonValue::fromVariant(value);m_drafts[m_configProvider]=draft;
        }else if(name=="save"){m_services->save(m_drafts);configAction("close",{});}
        else if(name=="test"||name=="testAll")testServices(name=="testAll");
        else if(name=="showTests")m_configBackend.update({{"showTestResults",true}});
        else if(name=="close"){
            if(m_config.window)m_config.window->hide();++m_configEpoch;
            for(auto c:m_testCancels)c->store(true);m_testCancels.clear();m_testQueue.clear();m_testRunning=0;
            dispose(m_config);m_drafts={};
        }
    }catch(const std::exception& e){m_configBackend.update({{"message",failure(e)}});}
}
void App::testServices(bool all)
{
    if(m_testRunning||!m_testQueue.isEmpty())return;
    m_batch=all;m_tests.clear();m_testQueue=all?providers:QStringList{m_configProvider};
    for(auto p:m_testQueue){
        if(!m_drafts.contains(p))m_drafts[p]=m_services->profile(p);
        m_tests[p]={{"status","waiting"},{"detail","等待测试"}};
    }m_configBackend.update({{"showTestResults",all}});pumpTests();
}
void App::pumpTests()
{
    while(m_testRunning<3&&!m_testQueue.isEmpty()){
        auto p=m_testQueue.takeFirst();auto draft=m_drafts.value(p).toObject();
        if((providers.indexOf(p)>=4&&draft.value("apiKey").toString().trimmed().isEmpty())||
            (p=="doubao"&&draft.value("model").toString().trimmed().isEmpty())){
            m_tests[p]={{"status","skipped"},{"detail","未配置密钥 / 模型，已跳过"}};continue;
        }
        ++m_testRunning;m_tests[p]={{"status","running"},{"detail","测试中…"}};
        auto services=m_services;auto settings=m_settings;auto cancel=token();m_testCancels.push_back(cancel);auto epoch=m_configEpoch;
        background([services,settings,draft,cancel]{return services->translate(settings,{"Hello world"},"en","zh",cancel,draft);},
            [this,p,cancel,epoch](Translation data,QString error){
                if(epoch!=m_configEpoch||cancel->load())return;--m_testRunning;
                m_tests[p]={{"status",error.isEmpty()?"success":"failed"},{"detail",error.isEmpty()?"连接成功："+data.segments.join('\n'):error}};pumpTests();
            });
    }publishTests();
}
void App::publishTests()
{
    QVariantList rows;int completed=0;
    for(auto p:providers){
        auto it=m_tests.find(p);if(it==m_tests.end())continue;
        auto row=it->second;row["provider"]=p;row["name"]=m_configBackend.values["names"].toStringList().value(providers.indexOf(p));
        rows.append(row);if(row["status"]!="waiting"&&row["status"]!="running")++completed;
    }
    m_configBackend.update({{"testResults",rows},{"testing",m_testRunning>0||!m_testQueue.isEmpty()},{"batch",m_batch},
        {"message",m_batch?QString("已测试 %1/%2").arg(completed).arg(rows.size()):m_tests[m_configProvider].value("detail").toString()}});
}
}

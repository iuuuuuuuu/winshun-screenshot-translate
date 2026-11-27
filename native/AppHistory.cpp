#include "App.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QScreen>
#include <QGuiApplication>
#include <QClipboard>
#include <QPointer>
namespace st {
void App::stopSpeech()
{
    ++m_speechEpoch;if(m_speech)m_speech->stop();
    for(auto backend:{&m_wordBackend,&m_panelBackend,&m_pinBackend})backend->update({{"speaking",false}});
}
void App::readAloud(Backend& backend,bool source)
{
    const auto text=backend.values.value(source?"sourceText":"translatedText").toString();if(text.trimmed().isEmpty())return;
    stopSpeech();if(!m_speech)m_speech=std::make_unique<Speech>();
    const auto epoch=m_speechEpoch;QPointer<App> guard(this);auto feedback=&backend;
    feedback->update({{"speaking",true}});
    m_speech->say(text,detect(text.left(4096)),[guard,feedback,epoch](bool active,const QString& error){
        if(!guard)return;
        QMetaObject::invokeMethod(guard,[guard,feedback,epoch,active,error]{
            if(!guard||epoch!=guard->m_speechEpoch)return;feedback->update({{"speaking",active}});
            if(!error.isEmpty())feedback->update({{"status",error}});
        },Qt::QueuedConnection);
    });
}
HistoryStore& App::history()
{if(!m_history)m_history=std::make_unique<HistoryStore>(m_services->directory());return *m_history;}
void App::remember(const QJsonObject& data,bool favorite,Backend& feedback)
{
    try{
        if(data.isEmpty())return;
        auto id=history().remember(data,favorite);
        if(favorite&&!id.isEmpty())feedback.update({{"notice",m_chinese?"已收藏，可在历史中查看":"Saved to favorites"}});
        if(m_historyView.window)publishHistory();
    }catch(const std::exception& e){feedback.update({{"status",QString::fromUtf8(e.what())}});}
}
void App::publishHistory()
{
    auto& store=history();auto rows=store.rows(m_historyBackend.values.value("search").toString(),m_historyBackend.values.value("favoritesOnly").toBool());
    // The list renders previews; opening and exporting read the complete entry.
    // Do not lay out every character of a long document in a three-line card.
    for(int i=0;i<rows.size();++i){auto row=rows[i].toObject();
        for(auto key:{"source","translated"}){auto text=row[key].toString();if(text.size()>1200)row[key]=text.left(1200)+QStringLiteral("…");}rows[i]=row;
    }
    m_historyBackend.update({{"rows",rows.toVariantList()},{"recording",store.enabled()}});
}
void App::showHistory()
{
    used();
    try{publishHistory();}catch(const std::exception& e){m_historyBackend.update({{"status",QString::fromUtf8(e.what())}});}
    if(!m_historyView.window)m_historyView=view("qrc:/ui/qml/History.qml",&m_historyBackend);
    auto screen=QGuiApplication::screenAt(m_mouse());if(!screen)screen=QGuiApplication::primaryScreen();auto area=screen->availableGeometry();
    auto window=m_historyView.window;window->setScreen(screen);window->resize(std::min(860,area.width()-24),std::min(680,area.height()-40));
    window->setPosition(area.center()-QPoint(window->width()/2,window->height()/2));window->show();window->requestActivate();
}
void App::historyAction(const QString& name,const QVariant& value)
{
    used();
    if(name=="close"){dispose(m_historyView);return;}
    try{
        auto& store=history();m_historyBackend.update({{"status",""}});
        if(name=="search"||name=="favoritesOnly")m_historyBackend.update({{name,value}});
        else if(name=="recording")store.setEnabled(value.toBool());
        else if(name=="favorite"){auto args=value.toMap();store.favorite(args["id"].toString(),args["enabled"].toBool());}
        else if(name=="remove")store.remove(value.toString());
        else if(name=="clear")store.clearRecent();
        else if(name=="open"){
            auto data=store.get(value.toString());if(data.isEmpty())return;
            if(m_panelCancel)m_panelCancel->store(true);++m_panelEpoch;
            m_panelBackend.update({{"autoTranslate",false}});showPanel();
            m_panelRecord=data;m_panelBackend.update({{"sourceText",data["source"].toString()},
                {"translatedText",data["translated"].toString()},{"source",data["sourceLanguage"].toString()},
                {"target",data["targetLanguage"].toString()},{"actualTarget",data["targetLanguage"].toString()},
                {"panelMode","translate"},{"panelPage","translate"},{"loading",false},{"explain",false},{"sentenceMode",false},
                {"provider",providerLabel(data)},{"status",""},{"notice",m_chinese?"已打开历史记录，编辑后可重新翻译":"History loaded. Edit to translate again."}});return;
        }else if(name=="export"){
            bool anki=value.toString()=="anki";
            auto root=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/WinShun translations";
            auto path=root+"/translations-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+(anki?".tsv":".csv");
            store.exportFile(path,anki,m_historyBackend.values.value("search").toString(),m_historyBackend.values.value("favoritesOnly").toBool());
            m_historyBackend.update({{"status",(m_chinese?"已导出：":"Exported: ")+QDir::toNativeSeparators(path)},{"exportFolder",root}});
        }else if(name=="folder"){
            auto folder=m_historyBackend.values.value("exportFolder").toString();if(!folder.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        }
        publishHistory();
    }catch(const std::exception& e){m_historyBackend.update({{"status",QString::fromUtf8(e.what())}});}
}
}

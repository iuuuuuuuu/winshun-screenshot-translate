#pragma once
#include "Desktop.h"
#include "PinWindow.h"
#include "Protocol.h"
#include "Dictionary.h"
#include "History.h"
#include "Speech.h"
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>
#include <QJsonArray>
#include <QProcess>
#include <map>
namespace st {
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQmlPropertyMap* state READ state CONSTANT)
public:
    using QObject::QObject;
    QQmlPropertyMap* state() {return &m_state;}
    void update(const QVariantMap& v){
        QVariantHash modified;
        for(auto it=v.begin();it!=v.end();++it)if(!values.contains(it.key())||values.value(it.key())!=it.value()){
            values[it.key()]=it.value();modified.insert(it.key(),it.value());
        }
        // QML subscribes to each field, so typing or a status message does not
        // invalidate every provider row and every hidden settings page.
        if(!modified.isEmpty()){m_state.insert(modified);emit changed();}
    }
    Q_INVOKABLE void action(const QString& name,const QVariant& value){emit invoked(name,value);}
    QVariantMap values;
signals:
    void changed();
    void invoked(QString,QVariant);
private:
    QQmlPropertyMap m_state;
};
struct View {
    std::unique_ptr<QQmlEngine> engine;
    QQuickWindow* window=nullptr;
};
class App : public QObject {
    Q_OBJECT
    friend class Probe;
public:
    App(ws::captureplugin::Protocol& protocol,QString appDir,QJsonObject initial);
    void dispatch(const QJsonValue& id,const QString& method,const QJsonObject& params);
private:
    View view(const QString& file,Backend* backend);
    void defaults(const QJsonObject& settings);
    void appearance(const QJsonObject& params);
    void registerKeys(bool retry=false);
    static LRESULT CALLBACK hotkeyProc(HWND,UINT,WPARAM,LPARAM);
    void query(const QJsonValue& id,const QJsonObject& params);
    QJsonArray results(const QJsonObject& data,const QString& command);
    void shot(const QString& kind,int delay=0);
    void openPin(QImage image,QRect area);
    void closePin();
    void recognizePin();
    void translatePin(QString source={},QString target={},QStringList parts={});
    void status(QString text,bool loading=true);
    void render();
    void place();
    void pinAction(const QString& name,const QVariant& value);
    void textFlow(const QString& command,bool retry=false);
    void openWord(QString mode,QPoint anchor,QRect selection={});
    void closeWord(bool animated=false);
    void wordAction(const QString& name,const QVariant& value);
    void editWord(bool editing);
    void translateWord(QString text,bool retainResult=false);
    void publishWordProviders();
    void publishProviders(Backend& backend,QString& choice,QHash<QString,QJsonObject>& profiles);
    void readSelection(bool follow=false);
    void readCopiedSelection();
    void placeWord();
    void ensureRaw();
    void updateSelectionMonitor();
    void loadSelectionSettings();
    void startSelectionPrimer();
    void selectionGesture(POINT point,DWORD timestamp,USHORT flags);
    void readIconSelection();
    void hideSelectionIcon();
    void translateIconSelection();
    void showSettings();
    void showPanel(const QString& text = {});
    void panelAction(const QString& name,const QVariant& value);
    void translatePanel(bool reuse=false);
    void saveTextTools();
    HistoryStore& history();
    void showHistory();
    void publishHistory();
    void historyAction(const QString& name,const QVariant& value);
    void remember(const QJsonObject& data,bool favorite,Backend& feedback);
    void readAloud(Backend& backend,bool source);
    void stopSpeech();
    void publishAi();
    void aiAction(const QString& name,const QVariant& value);
    void configSelect(const QString& provider);
    void configAction(const QString& name,const QVariant& value);
    void testServices(bool all, const QString& provider = {});
    void pumpTests();
    void publishTests();
    void publishDictionary();
    void startDictionaryDownload();
    void dispose(View& view);
    void used();
    bool idleReady() const;
    void reclaimIdle();
    QString providerLabel(const QJsonObject& data) const;
    QString providerNote(const QJsonObject& data) const;
    QTimer m_idleTimer;
    quint64 m_idleSince=0,m_lastTick=0,m_lastAwake=0;
    bool m_holdAfterResume=false,m_idleReleased=false;
    std::shared_ptr<std::atomic_bool> m_trimRunning=std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::atomic_uint64_t> m_activityRevision=std::make_shared<std::atomic_uint64_t>(0);
    std::shared_ptr<std::atomic_uint> m_trimCount=std::make_shared<std::atomic_uint>(0);
    bool m_busy=false,m_chinese=true,m_dark=false,m_hostHotkeys=false,m_compare=false,m_fullText=false;
    QString m_appDir,m_source="auto",m_target="zh",m_configProvider;
    QJsonObject m_settings,m_data,m_drafts,m_routingDraft;
    std::shared_ptr<Services> m_services;
    ws::captureplugin::Protocol& m_protocol;
    Backend m_pinBackend,m_configBackend,m_wordBackend,m_panelBackend;
    View m_bar,m_status,m_config,m_text,m_menu,m_word,m_panel;
    Backend m_historyBackend;
    View m_historyView;
    std::unique_ptr<HistoryStore> m_history;
    QJsonObject m_wordRecord,m_panelRecord;
    std::unique_ptr<Speech> m_speech;
    quint64 m_speechEpoch=0;
    Cancel m_panelCancel;
    quint64 m_panelEpoch=0;
    int m_aiCurrent=0;
    QJsonObject m_testProfiles;
    QTimer m_followSelection;
    QTimer m_copySelectionTimer;
    DWORD m_selectionClipboardBaseline=0;
    quint64 m_copySelectionDeadline=0;
    Cancel m_wordCancel,m_selectionCancel;
    quint64 m_wordEpoch=0,m_selectionPendingEpoch=0;
    int m_selectionAttempts=0;
    bool m_wordPinned=false,m_wordEditing=false,m_selectionReading=false,m_followBaseline=false,m_wordPlacementFrozen=false;
    DWORD m_wordOpened=0;
    QPoint m_wordAnchor,m_wordRequestPoint;
    QRect m_wordSelection;
    QString m_wordMode,m_wordSource,m_lastSelection;
    QString m_wordProviderChoice;
    QHash<QString,QJsonObject> m_wordProviderProfiles;
    QString m_pinProviderChoice;
    QStringList m_pinRequestParts;
    QHash<QString,QJsonObject> m_pinProviderProfiles;
    HWND m_selectionTarget=nullptr;
    std::function<Selection(POINT,HWND,Cancel)> m_readSelection=[](POINT p,HWND target,Cancel cancel){return isolatedSelection(p,target,cancel);};
    std::function<QString()> m_readClipboard;
    std::function<DWORD()> m_clipboardSequence=[] {return GetClipboardSequenceNumber();};
    std::function<DWORD()> m_clipboardOwnerProcess=[] {DWORD pid=0;GetWindowThreadProcessId(GetClipboardOwner(),&pid);return pid;};
    std::function<bool(HWND)> m_needsSelectionCopy=[](HWND target){
        wchar_t name[128]{};GetClassNameW(GetAncestor(target,GA_ROOT),name,128);
        return QString::fromWCharArray(name)=="FLUTTER_RUNNER_WIN32_WINDOW";
    };
    std::function<QPoint()> m_mouse;
    std::function<HWND()> m_foreground=[] {return GetForegroundWindow();};
    std::unique_ptr<ws::capture::PinWindow> m_pin;
    std::unique_ptr<Selector> m_selector;
    std::unique_ptr<RawClicks> m_raw;
    Backend m_iconBackend;
    View m_icon;
    QTimer m_iconDelay,m_iconWatch;
    QTimer m_primerWatchdog,m_primerRestart;
    QProcess m_selectionPrimer; // destroyed before the timers used by its callbacks
    Cancel m_iconCancel;
    Selection m_iconSelection;
    QPoint m_iconDown,m_iconPoint,m_iconPreviousClick;
    HWND m_iconTarget=nullptr;
    DWORD m_iconClickTime=0,m_iconShown=0;
    quint64 m_iconEpoch=0;
    bool m_iconPressed=false,m_iconDouble=false,m_iconReading=false;
    QTimer m_position;
    QImage m_original,m_translated;
    std::function<QImage(const QRect&)> m_capture=capture;
    QList<Block> m_blocks;
    Cancel m_cancel;
    quint64 m_epoch=0,m_configEpoch=0;
    DWORD m_pinOpened=0;
    std::map<int,std::pair<QJsonValue,Cancel>> m_queries;
    std::map<QString,std::pair<qint64,QJsonArray>> m_cache;
    std::map<QString,QVariantMap> m_tests;
    QStringList m_testQueue;
    std::vector<Cancel> m_testCancels;
    int m_testRunning=0;
    bool m_batch=false;
    Cancel m_dictionaryCancel;
    QString m_dictionaryMessage;
    qint64 m_dictionaryReceived=0;
    DictionarySource m_dictionarySource=dictionarySource();
    HWND m_keyWindow=nullptr;
    std::map<int,QString> m_nativeKeys;
    quint64 m_keyRevision=0;
};
}

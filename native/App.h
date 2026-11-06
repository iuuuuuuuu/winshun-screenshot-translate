#pragma once
#include "Desktop.h"
#include "PinWindow.h"
#include "Protocol.h"
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>
#include <QJsonArray>
#include <map>
namespace st {
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
public:
    using QObject::QObject;
    QVariantMap state() const {return values;}
    void update(const QVariantMap& v){bool modified=false;for(auto it=v.begin();it!=v.end();++it)if(values.value(it.key())!=it.value()){values[it.key()]=it.value();modified=true;}if(modified)emit changed();}
    Q_INVOKABLE void action(const QString& name,const QVariant& value){emit invoked(name,value);}
    QVariantMap values;
signals:
    void changed();
    void invoked(QString,QVariant);
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
    void textFlow(const QString& command);
    void showSettings();
    void configSelect(const QString& provider);
    void configAction(const QString& name,const QVariant& value);
    void testServices(bool all);
    void pumpTests();
    void publishTests();
    void dispose(View& view);
    bool m_busy=false,m_chinese=true,m_dark=false,m_hostHotkeys=false,m_compare=false,m_fullText=false;
    QString m_appDir,m_source="auto",m_target="zh",m_configProvider;
    QJsonObject m_settings,m_data,m_drafts;
    std::shared_ptr<Services> m_services;
    ws::captureplugin::Protocol& m_protocol;
    Backend m_pinBackend,m_configBackend;
    View m_bar,m_status,m_config,m_text,m_menu;
    std::unique_ptr<ws::capture::PinWindow> m_pin;
    std::unique_ptr<Selector> m_selector;
    std::unique_ptr<RawClicks> m_raw;
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
    HWND m_keyWindow=nullptr;
    std::map<int,QString> m_nativeKeys;
    quint64 m_keyRevision=0;
};
}

#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
namespace st {
class Speech {
public:
    using Callback=std::function<void(bool,const QString&)>;
    explicit Speech(bool silent=false);
    ~Speech();
    void say(QString text,QString language,Callback callback);
    void stop();
    static QStringList languages();
private:
    void run(std::stop_token stop);
    bool m_silent;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    quint64 m_revision=0;
    QString m_text,m_language;
    Callback m_callback;
    std::jthread m_thread;
};
}

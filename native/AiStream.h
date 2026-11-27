#pragma once
#include <QByteArray>
#include <QStringList>
#include <functional>
namespace st {
// A preview is deliberately separate from the validated final result.
class AiStream {
public:
    explicit AiStream(std::function<void(const QStringList&)> preview={}):m_preview(std::move(preview)){}
    void feed(const char* data,qint64 size);
    QStringList finish(int expected);
private:
    void event();
    QByteArray m_pending,m_event,m_raw;
    QString m_content,m_reason;
    bool m_sse=false,m_done=false;
    std::function<void(const QStringList&)> m_preview;
};
QStringList aiArrayPreview(const QString& content);
}

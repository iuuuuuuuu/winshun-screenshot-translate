#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QMutex>
#include <atomic>
#include <memory>

namespace st {
inline const QStringList providers{"mymemory","google","edge","zvo","sogou","youdao","ecdict","microsoft","deepl","ai"};
inline const QStringList names{"MyMemory","Google","Edge","translate.js","搜狗","有道","ECDICT","Microsoft","DeepL","AI 翻译"};
inline const QStringList legacyAi{"openai","deepseek","doubao","hunyuan"};
inline const QStringList endpoints{
    "https://api.mymemory.translated.net/get","https://translate.googleapis.com/translate_a/single",
    "https://api-edge.cognitive.microsofttranslator.com/translate","https://api.translate.zvo.cn/translate.json",
    "https://fanyi.sogou.com/text","https://dict.youdao.com/jsonapi","",
    "https://api.cognitive.microsofttranslator.com","https://api-free.deepl.com","https://api.openai.com/v1"};
QString canonicalProvider(const QString& provider);
QString configurationError(const QJsonObject& profile);
using Cancel = std::shared_ptr<std::atomic_bool>;
struct Translation {
    QStringList segments;
    QString source, target, provider, preferred;
    bool fallback = false;
    QJsonObject json(const QStringList& parts) const;
};
QString detect(const QString& text);
QString targetFor(const QString& text, const QString& setting);
bool validUrl(const QString& value);
QByteArray crypt(const QByteArray& data, bool decrypt = false);
void atomicJson(const QString& file, const QJsonObject& value);
class Services {
public:
    explicit Services(QString directory);
    QJsonObject profile(const QString& provider);
    QString configurationProblem(const QJsonObject& profile) const;
    QString directory() const {return m_directory;}
    void migrateAi(const QJsonObject& settings);
    void save(const QJsonObject& drafts, const QJsonObject& routing = {});
    QJsonObject routing();
    QJsonArray queue(const QJsonObject& settings, const QJsonObject& drafts = {}, const QJsonObject& routing = {});
    int today();
    Translation translate(const QJsonObject& settings, const QStringList& parts,
        const QString& source, const QString& target, Cancel cancel, QJsonObject draft = {});
private:
    Translation route(const QJsonObject& profile, const QStringList& parts,
        const QString& source, const QString& target, Cancel cancel, int budget);
    void usage(const QString& text);
    void recordOutcome(const QJsonObject& profile, bool success, qint64 elapsed, double recovery);
    QString m_directory;
    QMutex m_lock;
    QJsonObject m_stored, m_health, m_routing;
    QString m_day;
    int m_submitted = 0;
    bool m_usageValid = true;
    QString m_edgeToken,m_edgeProxy;
    qint64 m_edgeExpiry=0;
};
}

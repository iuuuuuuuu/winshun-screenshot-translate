#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
namespace st {
class HistoryStore {
public:
    explicit HistoryStore(QString directory);
    bool enabled() const{return m_enabled;}
    void setEnabled(bool enabled);
    QJsonArray rows(QString search={},bool favorites=false) const;
    QJsonObject get(const QString& id) const;
    QString remember(const QJsonObject& translation,bool favorite=false);
    void favorite(const QString& id,bool enabled);
    void remove(const QString& id);
    void clearRecent();
    void exportFile(const QString& file,bool anki,QString search={},bool favorites=false) const;
private:
    void save(QJsonArray rows,bool enabled);
    QString m_file;
    QJsonArray m_rows;
    bool m_enabled=true;
};
}

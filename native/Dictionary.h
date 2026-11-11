#pragma once
#include <QString>
#include "Services.h"
#include <functional>

namespace st {
// Returns empty for an unknown word; the caller may try another service.
struct DictionarySource {
    QString url,sha256;
    qint64 size;
    quint32 entries;
};
DictionarySource dictionarySource();
QString dictionaryPath(const QString& directory);
bool dictionaryAvailable(const QString& directory);
QString dictionaryEntry(const QString& text,const QString& directory={});
QJsonObject dictionaryDetails(const QString& text,const QString& directory={});
// Ordered, case-insensitive unique English tokens; retain positions and repeats.
QJsonArray dictionaryWords(const QString& text,int limit=60);
void downloadDictionary(const QString& directory,const QString& proxy,Cancel cancel,
    std::function<void(qint64,qint64)> progress={},const DictionarySource& source=dictionarySource());
bool dictionaryText(const QString& text);
}

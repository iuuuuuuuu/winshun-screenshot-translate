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
void downloadDictionary(const QString& directory,const QString& proxy,Cancel cancel,
    std::function<void(qint64,qint64)> progress={},const DictionarySource& source=dictionarySource());
bool dictionaryText(const QString& text);
}

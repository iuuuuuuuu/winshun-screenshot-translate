#pragma once
#include <QString>

namespace st {
// Returns empty for an unknown word; the caller may try another service.
QString dictionaryEntry(const QString& text);
bool dictionaryText(const QString& text);
}

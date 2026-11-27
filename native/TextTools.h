#pragma once
#include <QString>
#include <QJsonArray>
namespace st {
struct PreparedText {QString text,target;bool prefixed=false;};
PreparedText prepareText(QString text,QString target,bool pdfJoin,bool prefix=true);
QString joinPdfLines(QString text);
QString splitIdentifier(QString text);
QJsonArray namingVariants(const QString& text);
}

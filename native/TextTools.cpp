#include "TextTools.h"
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
namespace st {
QString joinPdfLines(QString text)
{
    text.replace("\r\n","\n");text.replace('\r','\n');
    const auto lines=text.split('\n',Qt::KeepEmptyParts);QStringList output;bool fenced=false;
    const QRegularExpression special(R"(^\s*(?:[-*+•▪‣]\s|\d+[.)、]\s*|#{1,6}\s|>|\|)|\t| {3,}|[{};]\s*$)");
    const QRegularExpression codeLine(R"(^\s*(?:(?:def|class|import|from|return|const|let|var)\s|(?:if|for|while)\b.*:\s*$|[A-Za-z_]\w*\s*=))");
    auto han=[](QChar c){auto v=c.unicode();return (v>=0x3400&&v<=0x9fff)||(v>=0x3040&&v<=0x30ff)||(v>=0xac00&&v<=0xd7af);};
    bool previousProtected=true;
    for(auto line:lines){
        auto trimmed=line.trimmed();const bool fence=trimmed.startsWith("```")||trimmed.startsWith("~~~");
        const bool protectedLine=fenced||fence||line.startsWith("    ")||special.match(line).hasMatch()||codeLine.match(line).hasMatch();
        if(fence)fenced=!fenced;
        if(trimmed.isEmpty()){output<<QString();previousProtected=true;continue;}
        if(output.isEmpty()||output.last().isEmpty()||protectedLine||previousProtected){output<<line;previousProtected=protectedLine;continue;}
        auto& previous=output.last();previous=previous.trimmed();
        // Soft hyphens are discretionary. A visible '-' may belong to an
        // identifier (1.25-bit / 30B-A3B), so it must never be deleted.
        if(previous.endsWith(QChar(0x00ad))){previous.chop(1);previous+=trimmed;}
        else if(previous.endsWith('-')||(han(previous.back())&&han(trimmed.front())))previous+=trimmed;
        else previous+=' '+trimmed;
        previousProtected=false;
    }
    return output.join('\n');
}
PreparedText prepareText(QString text,QString target,bool pdfJoin,bool prefix)
{
    PreparedText result{text.trimmed(),target,false};
    if(prefix){
        static const QMap<QString,QString> codes{{"zh","zh"},{"en","en"},{"ja","ja"},{"ko","ko"},{"fr","fr"},{"de","de"},{"es","es"},{"ru","ru"},
            {"中文","zh"},{"英文","en"},{"日文","ja"},{"韩文","ko"},{"法文","fr"},{"德文","de"},{"西班牙文","es"},{"俄文","ru"}};
        auto match=QRegularExpression("^([^\\s:：]{1,5})\\s*[:：]\\s*([\\s\\S]+)$").match(result.text);
        if(match.hasMatch()&&codes.contains(match.captured(1).toLower())){
            result.target=codes.value(match.captured(1).toLower());result.text=match.captured(2).trimmed();result.prefixed=true;
        }
    }
    if(pdfJoin)result.text=joinPdfLines(result.text);return result;
}
QString splitIdentifier(QString text)
{
    text.replace(QRegularExpression("([A-Z]+)([A-Z][a-z])"),"\\1 \\2");
    text.replace(QRegularExpression("([a-z0-9])([A-Z])"),"\\1 \\2");
    text.replace(QRegularExpression("[_\\-]+")," ");return text.simplified();
}
QJsonArray namingVariants(const QString& text)
{
    auto words=splitIdentifier(text).toLower().split(QRegularExpression("[^a-z0-9]+"),Qt::SkipEmptyParts);
    if(words.isEmpty())return {};
    QString pascal;for(auto word:words){word[0]=word[0].toUpper();pascal+=word;}
    auto camel=pascal;camel[0]=camel[0].toLower();
    auto safe=[](QString value){if(!value.isEmpty()&&value[0].isDigit())value.prepend('_');
        if(QStringList{"class","def","return","if","else","for","while","const","let","var","new","delete","switch","case","default","break","continue","import","from","true","false","null","void","int","float","double","char","static","public","private","protected","function","enum","struct","auto","this"}.contains(value))value+='_';return value;};
    QJsonArray rows;
    for(const auto& pair:QList<QPair<QString,QString>>{{"camelCase",safe(camel)},{"PascalCase",safe(pascal)},
        {"snake_case",safe(words.join('_'))},{"CONSTANT_CASE",safe(words.join('_').toUpper())},{"kebab-case",words.join('-')}})
        rows<<QJsonObject{{"style",pair.first},{"value",pair.second}};
    return rows;
}
}

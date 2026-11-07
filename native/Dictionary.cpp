#include "Dictionary.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <stdexcept>

namespace st {
bool dictionaryText(const QString& text)
{
    return text.size()<=100&&text.simplified().count(' ')<=7&&
        QRegularExpression("^[A-Za-z][A-Za-z '\\x{2019}.-]*$").match(text.trimmed()).hasMatch();
}
QString dictionaryEntry(const QString& text)
{
    QFile file(QCoreApplication::applicationDirPath()+"/data/ecdict.bin");
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("ECDICT 词库缺失，请重新安装完整插件包");
    auto header=file.read(12);
    if(header.size()!=12||header.left(8)!="ECDICT01")throw std::runtime_error("ECDICT 词库格式不正确");
    auto u32=[](const char* bytes){return qFromLittleEndian<quint32>(bytes);};
    const quint32 count=u32(header.constData()+8);
    if(count>2000000||12+qint64(count)*12>file.size())throw std::runtime_error("ECDICT 索引不完整");
    const QByteArray needle=text.trimmed().toLower().replace(QChar(0x2019),'\'').simplified().toUtf8();
    qint64 left=0,right=count;
    while(left<right){
        qint64 mid=(left+right)/2;file.seek(12+mid*12);auto entry=file.read(12);
        if(entry.size()!=12)throw std::runtime_error("ECDICT 索引读取失败");
        auto offset=u32(entry.constData()),length=u32(entry.constData()+4),size=u32(entry.constData()+8);
        if(length>4096||size>1024*1024||offset<12+count*12||quint64(offset)+length+size>quint64(file.size()))
            throw std::runtime_error("ECDICT 词条损坏");
        file.seek(offset);auto key=file.read(length);int order=key.compare(needle);
        if(order<0){left=mid+1;continue;}if(order>0){right=mid;continue;}
        auto values=QJsonDocument::fromJson(file.read(size)).array();
        if(values.size()!=4)throw std::runtime_error("ECDICT 词条格式不正确");
        QStringList lines;
        if(!values[0].toString().isEmpty())lines<<"/"+values[0].toString()+"/";
        auto meaning=values[1].toString();if(meaning.isEmpty())meaning=values[2].toString();
        if(meaning.isEmpty())return {};lines<<meaning;
        return lines.join('\n');
    }
    return {};
}
}

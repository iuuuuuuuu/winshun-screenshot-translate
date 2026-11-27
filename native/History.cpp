#include "History.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <stdexcept>
namespace st {
namespace {
constexpr qint64 maxBytes=32*1024*1024;
[[noreturn]] void failed(const char* message){throw std::runtime_error(message);}
QString csv(QString text){
    const auto first=text.trimmed().left(1);
    if(QStringList{"=","+","-","@"}.contains(first)||text.startsWith('\t')||text.startsWith('\r'))text.prepend('\'');
    text.replace('"',"\"\"");return '"'+text+'"';
}
QString html(QString text){return text.toHtmlEscaped().replace("\r\n","\n").replace('\r','\n').replace('\n',"<br>").replace('\t',"&#9;");}
}
HistoryStore::HistoryStore(QString directory):m_file(std::move(directory)+"/translation-history.json")
{
    QFile file(m_file);if(!file.exists())return;
    if(!file.open(QIODevice::ReadOnly)||file.size()>maxBytes)failed("无法读取翻译历史，原文件已保留");
    auto doc=QJsonDocument::fromJson(file.readAll());
    if(!doc.isObject()||!doc.object().value("rows").isArray())failed("翻译历史格式不正确，原文件已保留");
    m_rows=doc.object()["rows"].toArray();m_enabled=doc.object().value("enabled").toBool(true);
}
void HistoryStore::save(QJsonArray rows,bool enabled)
{
    int recent=0;
    for(int i=0;i<rows.size();){
        if(!rows[i].toObject().value("favorite").toBool()&&++recent>500)rows.removeAt(i);else ++i;
    }
    auto bytes=QJsonDocument(QJsonObject{{"version",1},{"enabled",enabled},{"rows",rows}}).toJson(QJsonDocument::Compact);
    // Never silently discard a favorite or truncate long source text.
    if(bytes.size()>maxBytes)failed("历史空间已满，请删除部分记录后重试；收藏未被清除");
    QDir().mkpath(QFileInfo(m_file).absolutePath());QSaveFile file(m_file);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())failed("翻译历史保存失败");
    m_rows=rows;m_enabled=enabled;
}
void HistoryStore::setEnabled(bool enabled){save(m_rows,enabled);}
QJsonArray HistoryStore::rows(QString search,bool favorites) const
{
    QJsonArray result;search=search.trimmed();
    for(auto item:m_rows){auto row=item.toObject();
        if(favorites&&!row["favorite"].toBool())continue;
        if(!search.isEmpty()&&!row["source"].toString().contains(search,Qt::CaseInsensitive)&&!row["translated"].toString().contains(search,Qt::CaseInsensitive))continue;
        result<<row;
    }return result;
}
QJsonObject HistoryStore::get(const QString& id) const
{for(auto item:m_rows)if(item.toObject()["id"]==id)return item.toObject();return {};}
QString HistoryStore::remember(const QJsonObject& data,bool favorite)
{
    if(!m_enabled&&!favorite)return {};
    if(data.value("preview").toBool()||data["source"].toString().trimmed().isEmpty()||data["translated"].toString().trimmed().isEmpty())return {};
    const auto id=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonArray{data["source"],data["translated"],data["sourceLanguage"],data["targetLanguage"]}).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    auto rows=m_rows;bool starred=favorite;
    for(int i=rows.size()-1;i>=0;--i)if(rows[i].toObject()["id"]==id){starred=starred||rows[i].toObject()["favorite"].toBool();rows.removeAt(i);}
    QJsonObject row{{"id",id},{"favorite",starred},{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    for(auto key:{"source","translated","sourceLanguage","targetLanguage","provider","channelName","model"})row[key]=data[key].toString();
    rows.prepend(row);save(rows,m_enabled);return id;
}
void HistoryStore::favorite(const QString& id,bool enabled)
{auto rows=m_rows;for(int i=0;i<rows.size();++i)if(rows[i].toObject()["id"]==id){auto row=rows[i].toObject();row["favorite"]=enabled;rows[i]=row;save(rows,m_enabled);return;}}
void HistoryStore::remove(const QString& id)
{auto rows=m_rows;for(int i=rows.size()-1;i>=0;--i)if(rows[i].toObject()["id"]==id)rows.removeAt(i);save(rows,m_enabled);}
void HistoryStore::clearRecent()
{auto rows=m_rows;for(int i=rows.size()-1;i>=0;--i)if(!rows[i].toObject()["favorite"].toBool())rows.removeAt(i);save(rows,m_enabled);}
void HistoryStore::exportFile(const QString& path,bool anki,QString search,bool favorites) const
{
    const auto selected=rows(search,favorites);if(selected.isEmpty())failed("没有可以导出的记录");
    QString output=anki?"#separator:Tab\n#html:true\n#columns:Front\tBack\tTags\n":"\uFEFF原文,译文,原文语言,译文语言,渠道,时间\r\n";
    for(auto item:selected){auto row=item.toObject();
        if(anki)output+=html(row["source"].toString())+'\t'+html(row["translated"].toString())+"\tWinShun\n";
        else{QStringList columns;for(auto key:{"source","translated","sourceLanguage","targetLanguage","provider","time"})columns<<csv(row[key].toString());output+=columns.join(',')+"\r\n";}
    }
    QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);auto bytes=output.toUtf8();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())failed("导出失败，请检查保存位置");
}
}

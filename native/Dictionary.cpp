#include "Dictionary.h"
#include "Http.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <stdexcept>

namespace st {
namespace {
[[noreturn]] void fail(const QString& message){throw std::runtime_error(message.toUtf8().constData());}
bool validDictionary(const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
    auto header=file.read(12);
    // Accept the two previously shipped layouts for upgrades; entries are
    // bounded again during lookup, since the data directory is user writable.
    return (file.size()==57823903||file.size()==71990182)&&header.size()==12&&
        header.left(8)=="ECDICT01"&&qFromLittleEndian<quint32>(header.constData()+8)==770611;
}
QString installedDictionary(const QString& directory)
{
    auto path=dictionaryPath(directory);if(validDictionary(path))return path;
    auto legacy=QCoreApplication::applicationDirPath()+"/data/ecdict.bin";
    return validDictionary(legacy)?legacy:QString();
}
}
DictionarySource dictionarySource()
{
    return {"https://github.com/iuuuuuuuu/winshun-screenshot-translate/releases/download/v0.4.4/ecdict-39cfb967-v2.bin",
        "f125d9edf0c096a5df8956d91c89c4ae36ed48405fac6b2fd38004f4a99ca5d9",57823903,770611};
}
QString dictionaryPath(const QString& directory){return directory+"/dictionaries/ecdict.bin";}
bool dictionaryAvailable(const QString& directory){return !installedDictionary(directory).isEmpty();}
void downloadDictionary(const QString& directory,const QString& proxy,Cancel cancel,
    std::function<void(qint64,qint64)> progress,const DictionarySource& source)
{
    QString proxyUrl=proxy;
    if(proxyUrl.isEmpty())for(auto key:{"OPENAPI_TRANSLATE_PROXY","HTTPS_PROXY","HTTP_PROXY","ALL_PROXY"})
        if(!(proxyUrl=qEnvironmentVariable(key)).isEmpty())break;
    if(!proxyUrl.isEmpty()){
        if(!proxyUrl.contains("://"))proxyUrl="http://"+proxyUrl;
        if(!validUrl(proxyUrl))fail("代理地址无效；请填写 HTTP / HTTPS 地址");
    }
    const auto path=dictionaryPath(directory);
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))fail("无法创建词库目录");
    QLockFile lock(path+".lock");lock.setStaleLockTime(0);
    if(!lock.tryLock())fail("词库正在其他窗口下载，请稍后重试");
    QSaveFile file(path);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly))fail("无法写入词库目录");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 received=0;QByteArray header;QString url=source.url;
    QElapsedTimer clock;clock.start();
    for(int hop=0;hop<6;++hop){
        QUrl address(url);auto host=address.host().toLower();
        const bool fixture=QUrl(source.url).host()=="127.0.0.1"&&address.scheme()=="http"&&host=="127.0.0.1";
        if(!validUrl(url)||(!fixture&&(address.scheme()!="https"||
            (host!="github.com"&&host!="release-assets.githubusercontent.com"&&host!="objects.githubusercontent.com"))))
            fail("词库下载地址无效");
        auto response=nativeRequest(url,{},false,{},proxyUrl,cancel,int(600000-clock.elapsed()),source.size,
            [&](const char* bytes,qint64 size){
                if(file.write(bytes,size)!=size)return false;
                hash.addData(QByteArrayView(bytes,size));received+=size;
                if(header.size()<12)header.append(bytes,std::min<qint64>(size,12-header.size()));
                if(progress)progress(received,source.size);return true;
            });
        if(cancel->load())fail("已取消下载");
        if(response.error)fail(QString("词库下载失败（网络或写入错误 %1），请检查网络或共用代理后重试").arg(response.error));
        if(response.status>=300&&response.status<400){
            if(response.location.isEmpty())fail("词库下载跳转无效");
            url=address.resolved(QUrl(response.location)).toString();continue;
        }
        if(response.status!=200)fail(QString("词库下载失败（HTTP %1），请稍后重试").arg(response.status));
        if(received!=source.size||hash.result().toHex()!=source.sha256.toLatin1()||header.size()!=12||
            header.left(8)!="ECDICT01"||qFromLittleEndian<quint32>(header.constData()+8)!=source.entries)
            fail("词库校验失败，未启用下载文件；请重试");
        if(cancel->load())fail("已取消下载");
        if(!file.commit())fail("无法保存词库，请重试");
        return;
    }
    fail("词库下载跳转次数过多，请稍后重试");
}
bool dictionaryText(const QString& text)
{
    return text.size()<=100&&text.simplified().count(' ')<=7&&
        QRegularExpression("^[A-Za-z][A-Za-z '\\x{2019}.-]*$").match(text.trimmed()).hasMatch();
}
QString dictionaryEntry(const QString& text,const QString& directory)
{
    QFile file(installedDictionary(directory));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("ECDICT 词库未下载，请到设置 → ECDICT 一键下载");
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

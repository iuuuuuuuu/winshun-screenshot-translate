#include "Services.h"
#include "Http.h"
#include "Dictionary.h"
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QUrlQuery>
#include <QUuid>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <vector>

namespace st {
namespace {
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }
class Unsupported : public std::runtime_error { public: explicit Unsupported(const QString& text):std::runtime_error(text.toUtf8().constData()){} };
QJsonObject readJson(const QString& file, bool strict = false)
{
    QFile f(file);
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) { if (strict) fail("无法读取已有配置；未覆盖原文件"); return {}; }
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) { if (strict) fail("已有配置格式不正确；未覆盖原文件"); return {}; }
    return doc.object();
}
QString field(const QJsonObject& p, const char* k) { return p.value(k).toString().trimmed(); }
QJsonArray array(const QStringList& values) { return QJsonArray::fromStringList(values); }
QJsonObject normalizedRouting(const QJsonObject& value)
{
    const QStringList defaults{"ecdict","zvo","sogou","youdao","mymemory","google","edge","microsoft","deepl","ai"};
    QStringList order;
    for(auto v:value.value("order").toArray()){
        const auto p=v.toString();if(providers.contains(p)&&!order.contains(p))order<<p;
    }
    for(auto p:defaults)if(!order.contains(p))order<<p;
    return {{"mode",value.value("mode").toString()=="auto"?"auto":"manual"},{"order",array(order)}};
}
QString endpoint(const QJsonObject& p)
{
    auto custom = field(p,"endpoint");
    return custom.isEmpty() ? endpoints.value(providers.indexOf(field(p,"provider"))) : custom;
}
QString code(QString language, const QString& chinese) { return language == "zh" ? chinese : language; }
QString fingerprint(const QJsonObject& p)
{
    // Configuration changes immediately retry a channel, including model and region.
    QStringList quoted;
    for (auto k : {"provider","endpoint","proxy","apiKey","model","region","appId"}) {
        auto raw = QJsonDocument(QJsonArray{p.value(k).toString()}).toJson(QJsonDocument::Compact);
        QString value = QString::fromUtf8(raw.mid(1,raw.size()-2)), ascii;
        for (QChar c : value) ascii += c.unicode() > 127 ? QString("\\u%1").arg(uint(c.unicode()),4,16,QChar('0')) : QString(c);
        quoted << ascii;
    }
    return QCryptographicHash::hash(("[" + quoted.join(", ") + "]").toUtf8(),QCryptographicHash::Sha256).toHex();
}
class Http {
public:
    Http(const QString& proxy, Cancel cancel, int budget) : m_cancel(cancel), m_budget(budget)
    {
        QString value = proxy;
        if (value.isEmpty())
            for (auto key : {"OPENAPI_TRANSLATE_PROXY","HTTPS_PROXY","HTTP_PROXY","ALL_PROXY"})
                if (!(value = qEnvironmentVariable(key)).isEmpty()) break;
        if (!value.isEmpty()) {
            if (!value.contains("://")) value = "http://" + value;
            if (!validUrl(value)) fail("代理地址无效；请填写 HTTP / HTTPS 地址");
            QUrl u(value);
            m_proxy=u.toString();
        }
        m_clock.start();
    }
    QByteArray request(QString url, QJsonValue body = {}, QMap<QByteArray,QByteArray> headers = {}, bool form = false, int cap = 0)
    {
        if (!validUrl(url)) fail("服务地址无效");
        if (m_cancel->load()) fail("cancelled");
        int remaining = m_budget - int(m_clock.elapsed());
        if (cap > 0) remaining = std::min(remaining, cap);
        if (remaining <= 0) fail("翻译超时");
        if(!headers.contains("User-Agent"))headers["User-Agent"]="WinShun-screenshot-translate/0.4.3";
        if(!headers.contains("Accept"))headers["Accept"]="application/json,text/plain,*/*";
        QByteArray bytes;
        if (!body.isUndefined() && !body.isNull()) {
            if (form) {
                auto obj = body.toObject();
                for (auto it=obj.begin();it!=obj.end();++it)
                {
                    if(!bytes.isEmpty())bytes+='&';
                    bytes+=QUrl::toPercentEncoding(it.key())+'='+QUrl::toPercentEncoding(it.value().isString()?it.value().toString():QString::number(it.value().toDouble(),'g',16));
                }
                headers["Content-Type"]="application/x-www-form-urlencoded";
            } else {
                bytes=body.isArray()?QJsonDocument(body.toArray()).toJson(QJsonDocument::Compact):QJsonDocument(body.toObject()).toJson(QJsonDocument::Compact);
                headers["Content-Type"]="application/json; charset=utf-8";
            }
        }
        auto response=nativeRequest(url,bytes,!body.isNull()&&!body.isUndefined(),headers,m_proxy,m_cancel,remaining);
        auto redirect=QUrl(response.location);
        if (m_cancel->load()) fail("cancelled");
        if (response.error==ERROR_WINHTTP_TIMEOUT) fail("翻译超时");
        if (response.error==ERROR_FILE_TOO_LARGE) fail("服务响应过大");
        if (!redirect.isEmpty()) {
            QUrl from(url), to=from.resolved(redirect);
            if (to.host().endsWith("google.com") && to.path().startsWith("/sorry")) fail("HTTP 429");
            if ((from.host()=="www.bing.com"||from.host()=="cn.bing.com") &&
                (to.host()=="www.bing.com"||to.host()=="cn.bing.com") && to.scheme()=="https" &&
                to.path()=="/translator" && (body.isNull()||body.isUndefined()) && ++m_redirects<=3)
                return request(to.toString(),{},headers);
            fail("服务重定向了请求；请配置最终地址");
        }
        if (response.status>=400) fail(QString("HTTP %1").arg(response.status));
        if (response.error==ERROR_WINHTTP_SECURE_FAILURE) fail("HTTPS 证书验证失败；请检查系统时间或代理");
        if (response.error==ERROR_WINHTTP_NAME_NOT_RESOLVED) fail("无法解析服务地址；请检查网络或代理");
        if (response.error) fail(QString("无法连接服务；请检查网络或共用代理（%1）").arg(response.error));
        return response.body;
    }
    QJsonValue json(QString url, QJsonValue body = {}, QMap<QByteArray,QByteArray> headers = {}, bool form = false)
    {
        auto bytes=request(url,body,headers,form);
        auto doc=QJsonDocument::fromJson(bytes);
        if (doc.isArray()) return doc.array();
        if (doc.isObject()) return doc.object();
        fail("服务响应格式不正确");
    }
private:
    QString m_proxy;
    Cancel m_cancel;
    QElapsedTimer m_clock;
    int m_budget, m_redirects=0;
};
QString query(QString url, const QMap<QString,QString>& values)
{
    for(auto it=values.begin();it!=values.end();++it)
        url+=(url.contains('?')?'&':'?')+QString::fromLatin1(QUrl::toPercentEncoding(it.key())+'='+QUrl::toPercentEncoding(it.value()));
    return url;
}
QMap<QByteArray,QByteArray> browser()
{
    return {{"User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"},
            {"Accept-Language","zh-CN,zh;q=0.9,en;q=0.8"}};
}
QStringList strings(const QJsonValue& value)
{
    if (!value.isArray()) fail("服务返回的文本块格式不正确");
    QStringList result;
    for(auto v:value.toArray()){if(!v.isString())fail("服务返回的文本块不完整");result<<v.toString();}
    return result;
}
QJsonValue at(const QJsonArray& a,int i) {return i>=0&&i<a.size()?a.at(i):QJsonValue();}
QString unescape(QString text)
{
    // Decode entities only; screen text such as "<button>" is not HTML.
    auto matches=QRegularExpression("&(?:#[0-9]+|#x[0-9A-Fa-f]+|[A-Za-z][A-Za-z0-9]+);").globalMatch(text);
    QList<QRegularExpressionMatch> values;while(matches.hasNext())values<<matches.next();
    for(auto it=values.rbegin();it!=values.rend();++it)
        text.replace(it->capturedStart(),it->capturedLength(),QTextDocumentFragment::fromHtml(it->captured()).toPlainText());
    return text;
}
QJsonObject sogouPage(const QByteArray& bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8);QString text=decoder(bytes);
    if(decoder.hasError()){
        int size=MultiByteToWideChar(54936,MB_ERR_INVALID_CHARS,bytes.constData(),int(bytes.size()),nullptr,0);
        if(size<=0)fail("搜狗页面编码不正确");
        std::wstring wide(size,L'\0');MultiByteToWideChar(54936,MB_ERR_INVALID_CHARS,bytes.constData(),int(bytes.size()),wide.data(),size);
        text=QString::fromStdWString(wide);
    }
    auto at=text.indexOf("window.__INITIAL_STATE__=");
    if(at<0)fail("搜狗暂未返回结果，可能需要在网页验证后重试");
    auto start=text.indexOf('{',at);int depth=0;bool quoted=false,escaped=false;
    for(auto i=start;i>=0&&i<text.size();++i){
        auto ch=text[i];
        if(quoted){if(escaped)escaped=false;else if(ch=='\\')escaped=true;else if(ch=='\"')quoted=false;continue;}
        if(ch=='\"')quoted=true;else if(ch=='{')++depth;else if(ch=='}'&&!--depth){
            auto state=QJsonDocument::fromJson(text.mid(start,i-start+1).toUtf8()).object();
            auto result=state.value("textTranslate").toObject().value("translateData").toObject();
            if(result.isEmpty())fail("搜狗没有返回翻译结果");return result;
        }
    }fail("搜狗页面格式已变化");
}
}
QString canonicalProvider(const QString& value){return legacyAi.contains(value)?"ai":providers.contains(value)?value:"mymemory";}
QString configurationError(const QJsonObject& p)
{
    const auto id=field(p,"provider");
    if((id=="microsoft"||id=="deepl"||id=="ai")&&field(p,"apiKey").isEmpty())return "未配置 API 密钥";
    if(id=="ai"&&field(p,"model").isEmpty())return "请填写模型名";
    if(id=="youdao"&&(field(p,"appId").isEmpty()!=field(p,"apiKey").isEmpty()))return "有道官方翻译需要同时填写应用 ID 和密钥";
    return {};
}
bool validUrl(const QString& value)
{
    QUrl u(value,QUrl::StrictMode);
    return u.isValid() && (u.scheme()=="http"||u.scheme()=="https") && !u.host().isEmpty() &&
        u.fragment().isEmpty() && u.userName().isEmpty() && u.password().isEmpty();
}
QString detect(const QString& text)
{
    int latin=0,han=0,kana=0,hangul=0,cyrillic=0;
    for(QChar c:text){auto u=c.unicode();latin+=(u>='A'&&u<='Z')||(u>='a'&&u<='z');
        han+=u>=0x3400&&u<=0x9fff;kana+=u>=0x3040&&u<=0x30ff;hangul+=u>=0xac00&&u<=0xd7af;cyrillic+=u>=0x0400&&u<=0x052f;}
    if(kana&&(han+kana)*2>=latin&&kana>=hangul&&(kana>=2||!(latin+han)))return "ja";
    if(hangul&&hangul*2>=latin+han*2+kana*2)return "ko";
    if(han&&han*2>=latin+cyrillic)return "zh";
    return cyrillic>latin?"ru":"en";
}
QString targetFor(const QString& text,const QString& setting)
{
    return QStringList{"zh","en","ja","ko","fr","de","es","ru"}.contains(setting)?setting:(detect(text)=="zh"?"en":"zh");
}
QByteArray crypt(const QByteArray& data,bool decrypt)
{
    QByteArray entropy("iuuuuuuuu.screenshot-translate/profiles/v1");
    DATA_BLOB input{DWORD(data.size()),reinterpret_cast<BYTE*>(const_cast<char*>(data.constData()))};
    DATA_BLOB salt{DWORD(entropy.size()),reinterpret_cast<BYTE*>(entropy.data())},output{};
    BOOL ok=decrypt?CryptUnprotectData(&input,nullptr,&salt,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output):
        CryptProtectData(&input,nullptr,&salt,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output);
    if(!ok) fail(decrypt?"Windows 无法解密已有密钥":"Windows 无法加密密钥");
    QByteArray result(reinterpret_cast<char*>(output.pbData),output.cbData); LocalFree(output.pbData);return result;
}
void atomicJson(const QString& file,const QJsonObject& value)
{
    QDir().mkpath(QFileInfo(file).absolutePath()); QSaveFile f(file);
    if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(value).toJson(QJsonDocument::Compact))<0||!f.commit())fail("无法保存配置");
}
Services::Services(QString directory):m_directory(std::move(directory))
{
    auto doc=readJson(m_directory+"/services.json",true);
    if(!doc.isEmpty()&&!doc.value("profiles").isObject())fail("已有配置格式不正确；未覆盖原文件");
    m_stored=doc.value("profiles").toObject();
    m_routing=normalizedRouting(doc.value("routing").toObject());
    m_health=readJson(m_directory+"/service-health.json").value("services").toObject();
    auto u=readJson(m_directory+"/mymemory-usage.json");
    m_day=u.value("day").toString();m_submitted=std::max(0,u.value("submitted").toInt());
    m_usageValid=!QFile::exists(m_directory+"/mymemory-usage.json")||(!m_day.isEmpty()&&u.contains("submitted"));
}
QJsonObject Services::profile(const QString& p)
{
    QMutexLocker lock(&m_lock);auto stored=m_stored.value(p).toObject();
    auto secret=stored.value("secret").toString();
    QJsonObject result{{"provider",p},{"apiKey",secret.isEmpty()?QString():QString::fromUtf8(crypt(QByteArray::fromBase64(secret.toLatin1()),true))}};
    for(auto k:{"endpoint","model","region","appId"})result[k]=stored.value(k).toString();
    return result;
}
void Services::migrateAi(const QJsonObject& settings)
{
    QMutexLocker lock(&m_lock);if(m_stored.contains("ai"))return;
    auto preferred=settings.value("provider").toString();if(preferred=="ai")preferred=settings.value("aiProvider").toString();
    QStringList order=legacyAi;if(legacyAi.contains(preferred)){order.removeAll(preferred);order.prepend(preferred);}
    for(auto old:order){
        auto p=m_stored.value(old).toObject();if(p.isEmpty())continue;
        if(p.value("secret").toString().isEmpty()&&p.value("endpoint").toString().isEmpty()&&p.value("model").toString().isEmpty())continue;
        QStringList urls{"https://api.openai.com/v1","https://api.deepseek.com","https://ark.cn-beijing.volces.com/api/v3","https://api.hunyuan.cloud.tencent.com/v1"};
        QStringList models{"gpt-4o-mini","deepseek-flash","","hunyuan-turbos-latest"};
        if(p.value("endpoint").toString().isEmpty())p["endpoint"]=urls[legacyAi.indexOf(old)];
        if(p.value("model").toString().isEmpty())p["model"]=models[legacyAi.indexOf(old)];
        p["migratedFrom"]=old;auto next=m_stored;next["ai"]=p;
        atomicJson(m_directory+"/services.json",{{"version",3},{"profiles",next},{"routing",m_routing}});m_stored=next;return;
    }
}
void Services::save(const QJsonObject& drafts,const QJsonObject& routing)
{
    QMutexLocker lock(&m_lock);auto values=m_stored;
    for(auto it=drafts.begin();it!=drafts.end();++it){
        if(!providers.contains(it.key())&&!legacyAi.contains(it.key()))fail("未知渠道");
        auto p=it.value().toObject();QString ep=field(p,"endpoint"),key=field(p,"apiKey");
        if(!ep.isEmpty()&&!validUrl(ep))fail("服务地址无效");
        values[it.key()]=QJsonObject{{"endpoint",ep},{"model",field(p,"model")},{"region",field(p,"region")},{"appId",field(p,"appId")},
            {"secret",key.isEmpty()?QString():QString::fromLatin1(crypt(key.toUtf8()).toBase64())}};
    }
    auto policy=routing.isEmpty()?m_routing:normalizedRouting(routing);
    atomicJson(m_directory+"/services.json",{{"version",3},{"profiles",values},{"routing",policy}});m_stored=values;m_routing=policy;
}
QJsonObject Services::routing()
{
    QMutexLocker lock(&m_lock);return m_routing;
}
QJsonArray Services::queue(const QJsonObject& settings,const QJsonObject& drafts,const QJsonObject& routing)
{
    QJsonObject policy,health;
    {QMutexLocker lock(&m_lock);policy=routing.isEmpty()?m_routing:normalizedRouting(routing);health=m_health;}
    const auto preferred=canonicalProvider(settings.value("provider").toString("mymemory"));
    const double recovery=std::clamp(settings.value("fallbackRecoveryMinutes").toDouble(10),1.0,1440.0)*60;
    const double now=QDateTime::currentSecsSinceEpoch();
    QList<QJsonObject> rows;
    for(auto v:policy.value("order").toArray()){
        const auto p=v.toString();auto config=drafts.contains(p)?drafts.value(p).toObject():profile(p);config["provider"]=p;
        config["proxy"]=settings.value("proxy").toString().trimmed();
        const auto h=health.value(fingerprint(config)).toObject();
        const auto problem=configurationError(config);const int samples=h.value("samples").toInt();
        const double latency=h.value("latencyMs").toDouble(-1);
        const double rate=std::clamp(h.value("successRate").toDouble(.75),0.0,1.0);
        const int weight=samples?int(std::round(80*rate+20*(latency<0?.5:1000/(1000+latency)))):50;
        const double retryAt=h.value("failedAt").toDouble()>0?h.value("failedAt").toDouble()+recovery:0;
        const bool cooling=retryAt>now;
        rows<<QJsonObject{{"provider",p},{"name",names.value(providers.indexOf(p))},{"preferred",p==preferred},
            {"configured",problem.isEmpty()},{"problem",problem},{"cooling",cooling},{"retrySeconds",std::max(0,int(retryAt-now))},
            {"weight",weight},{"latencyMs",int(latency)},{"samples",samples},{"successRate",int(std::round(rate*100))},{"checkedAt",h.value("checkedAt")},
            {"status",!problem.isEmpty()?"skipped":cooling?"cooling":samples?(h.value("lastSuccess").toBool()?"success":"failed"):"idle"}};
    }
    if(policy.value("mode")=="auto")std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){
        if(a.value("configured")!=b.value("configured"))return a.value("configured").toBool();
        if(a.value("cooling")!=b.value("cooling"))return !a.value("cooling").toBool();
        return a.value("weight").toInt()>b.value("weight").toInt();
    });
    for(int i=0;i<rows.size();++i)if(rows[i].value("preferred").toBool()){rows.move(i,0);break;}
    QJsonArray result;for(auto row:rows)result.append(row);return result;
}
void Services::recordOutcome(const QJsonObject& p,bool success,qint64 elapsed,double recovery)
{
    QMutexLocker lock(&m_lock);const auto id=fingerprint(p);auto h=m_health.value(id).toObject();
    h["provider"]=field(p,"provider");h["samples"]=std::min(1000000,h.value("samples").toInt()+1);
    h["successRate"]=.7*std::clamp(h.value("successRate").toDouble(.75),0.0,1.0)+.3*(success?1:0);
    h["lastSuccess"]=success;h["checkedAt"]=double(QDateTime::currentMSecsSinceEpoch());
    if(success){
        h["latencyMs"]=h.contains("latencyMs")?.7*h.value("latencyMs").toDouble()+.3*elapsed:double(elapsed);
        h.remove("failedAt");h.remove("retryAt");
    }else{const auto now=double(QDateTime::currentSecsSinceEpoch());h["failedAt"]=now;h["retryAt"]=now+recovery;}
    m_health[id]=h;
    try{atomicJson(m_directory+"/service-health.json",{{"version",2},{"services",m_health}});}catch(...){}
}
int Services::today()
{
    QMutexLocker lock(&m_lock);auto day=QDate::currentDate().toString(Qt::ISODate);
    if(day!=m_day){m_day=day;m_submitted=0;}return m_submitted;
}
void Services::usage(const QString& text)
{
    QMutexLocker lock(&m_lock);if(!m_usageValid)return;
    auto day=QDate::currentDate().toString(Qt::ISODate);if(day!=m_day){m_day=day;m_submitted=0;}
    m_submitted+=text.toUcs4().size();
    try{atomicJson(m_directory+"/mymemory-usage.json",{{"version",1},{"day",day},{"submitted",m_submitted}});}catch(...){m_usageValid=false;}
}
QJsonObject Translation::json(const QStringList& parts) const
{
    return {{"source",parts.join('\n')},{"translated",segments.join('\n')},{"segments",array(segments)},
        {"sourceLanguage",source},{"targetLanguage",target},{"provider",provider},{"preferredProvider",preferred},{"fallback",fallback}};
}
Translation Services::translate(const QJsonObject& settings,const QStringList& parts,const QString& source,
    const QString& target,Cancel cancel,QJsonObject draft)
{
    QString preferred=canonicalProvider(settings.value("provider").toString("mymemory"));
    if(preferred=="ai")migrateAi(settings);
    bool fallback=draft.isEmpty()&&settings.value("fallbackEnabled").toBool(true);
    QList<QJsonObject> candidates;
    auto first=draft.isEmpty()?profile(preferred):draft;
    if(!draft.isEmpty())preferred=field(draft,"provider");
    candidates<<first;
    if(fallback)for(auto row:queue(settings)){
        auto p=row.toObject().value("provider").toString();
        if(p==preferred)continue;auto config=profile(p);
        if(configurationError(config).isEmpty())candidates<<config;
    }
    double recovery=std::clamp(settings.value("fallbackRecoveryMinutes").toDouble(10),1.0,1440.0)*60;
    QElapsedTimer total;total.start();QString last;int attempted=0;
    for(auto p:candidates){
        if(cancel->load())fail("cancelled");
        p["proxy"]=settings.value("proxy").toString().trimmed();
        auto problem=configurationError(p);
        if(!problem.isEmpty()){last=problem;if(!fallback)fail(problem);continue;}
        auto id=fingerprint(p);
        {
            QMutexLocker lock(&m_lock);
            if(fallback&&m_health.value(id).toObject().value("failedAt").toDouble()+recovery>QDateTime::currentSecsSinceEpoch())continue;
        }
        int budget=std::min(field(p,"provider")=="edge"?14000:8000,int((fallback||preferred=="edge"?20000:8000)-total.elapsed()));
        if(budget<=0)break;
        ++attempted;QElapsedTimer attempt;attempt.start();
        try{
            auto result=route(p,parts,source,target,cancel,budget);
            result.provider=field(p,"provider");result.preferred=preferred;result.fallback=result.provider!=preferred;
            if(cancel->load())fail("cancelled");
            recordOutcome(p,true,attempt.elapsed(),recovery);
            return result;
        }catch(const Unsupported& e){
            // A missing dictionary entry is not an outage. Never quarantine the
            // dictionary or prevent the next supported word from being looked up.
            if(cancel->load())fail("cancelled");last=QString::fromUtf8(e.what());if(!fallback)throw;
        }catch(const std::exception& e){
            if(cancel->load())fail("cancelled");
            last=QString::fromUtf8(e.what());recordOutcome(p,false,attempt.elapsed(),recovery);if(!fallback)throw;
        }
    }
    fail(!last.isEmpty()?last:attempted?"翻译超时":"所有可用渠道暂时失败，恢复间隔后会重试；修改渠道配置可立即重试");
}
Translation Services::route(const QJsonObject& p,const QStringList& parts,const QString& source,
    const QString& target,Cancel cancel,int budget)
{
    QString provider=field(p,"provider"),key=field(p,"apiKey"),url=endpoint(p);
    if(!providers.contains(provider))fail("未知翻译渠道");
    auto problem=configurationError(p);if(!problem.isEmpty())fail(problem);
    Translation result;result.source=source=="auto"?detect(parts.join('\n')):source;result.target=target;
    QString destination=code(target,"zh-CN"),origin=code(source,"zh-CN");
    if(provider=="ecdict"){
        if(target!="zh"||(source!="en"&&source!="auto"))throw Unsupported("ECDICT 支持英文单词和词库内短语的中文释义；可开启自动降级翻译句子");
        for(auto part:parts){
            if(cancel->load())fail("cancelled");
            if(!dictionaryText(part))throw Unsupported("ECDICT 不支持这段长句；可开启自动降级");
            auto text=dictionaryEntry(part);if(text.isEmpty())throw Unsupported("ECDICT 未收录该词；可开启自动降级");
            result.segments<<text;
        }
        return result;
    }
    Http http(field(p,"proxy"),cancel,budget);
    if(provider=="sogou"){
        for(auto part:parts){
            auto headers=browser();headers["Referer"]="https://fanyi.sogou.com/";
            auto data=sogouPage(http.request(query(url,{{"keyword",part},{"transfrom",code(source,"zh-CHS")},{"transto",code(target,"zh-CHS")}}),{},headers));
            auto trans=data.value("translate").toObject();
            if(trans.value("errorCode").toString("0")!="0")fail("搜狗暂不可用或需要网页验证");
            QString text=trans.value("dit").toString();
            if(target=="zh"&&dictionaryText(part)){
                QStringList meanings;
                for(auto row:data.value("wordCard").toObject().value("usualDict").toArray()){
                    auto entry=row.toObject();QStringList values;for(auto v:entry.value("values").toArray())if(v.isString())values<<v.toString();
                    if(!values.isEmpty())meanings<<(entry.value("pos").toString()+" "+values.join("；")).trimmed();
                }if(!meanings.isEmpty())text=meanings.join('\n');
            }result.segments<<unescape(text);
        }
    }else if(provider=="youdao"){
        const bool official=!field(p,"appId").isEmpty();
        if(official&&field(p,"endpoint").isEmpty())url="https://openapi.youdao.com/api";
        for(auto part:parts){
            if(official){
                auto chars=part.toUcs4();QString input=part;
                if(chars.size()>20)input=QString::fromUcs4(chars.constData(),10)+QString::number(chars.size())+QString::fromUcs4(chars.constData()+chars.size()-10,10);
                auto salt=QUuid::createUuid().toString(QUuid::WithoutBraces),time=QString::number(QDateTime::currentSecsSinceEpoch()),appId=field(p,"appId");
                auto sign=QString::fromLatin1(QCryptographicHash::hash((appId+input+salt+time+key).toUtf8(),QCryptographicHash::Sha256).toHex());
                auto data=http.json(url,QJsonObject{{"q",part},{"from",code(source,"zh-CHS")},{"to",code(target,"zh-CHS")},
                    {"appKey",appId},{"salt",salt},{"curtime",time},{"signType","v3"},{"sign",sign},{"strict","true"}},{},true).toObject();
                if(data.value("errorCode").toString()!="0")fail("有道官方接口错误："+data.value("errorCode").toString());
                result.segments<<strings(data.value("translation")).join('\n');
            }else{
                if(target!="zh"||(source!="en"&&source!="auto")||!dictionaryText(part))throw Unsupported("有道免密钥查词支持英译中；整句 / 其他语言请配置官方应用 ID 和密钥，或开启自动降级");
                auto headers=browser();headers["Referer"]="https://dict.youdao.com/";
                auto data=http.json(query(url,{{"q",part},{"le","en"},{"client","web"},{"keyfrom","webdict"},
                    {"dicts","{\"count\":99,\"dicts\":[[\"ec\"]]}"}}),{},headers).toObject();
                QStringList meanings;
                for(auto word:data.value("ec").toObject().value("word").toArray())for(auto row:word.toObject().value("trs").toArray())
                    for(auto tr:row.toObject().value("tr").toArray())for(auto item:tr.toObject().value("l").toObject().value("i").toArray())
                        if(item.isString()&&!item.toString().trimmed().isEmpty())meanings<<unescape(item.toString());
                if(meanings.isEmpty())throw Unsupported("有道词典未收录该词；可配置官方翻译或开启自动降级");
                result.segments<<meanings.join('\n');
            }
        }
    }else if(provider=="mymemory"||provider=="google"){
        QElapsedTimer batchClock;batchClock.start();
        auto one=[&](const QString& part)->QString{
            Http http(field(p,"proxy"),cancel,std::max(1,budget-int(batchClock.elapsed())));
            if(provider=="mymemory"){
                QString src=code(source=="auto"?detect(part):source,"zh-CN");
                if(src==destination)return part;
                QStringList chunks;QString chunk;int bytes=0;
                for(auto cp:part.toUcs4()){
                    auto c=QString::fromUcs4(&cp,1);int size=c.toUtf8().size();
                    if(bytes+size>500){chunks<<chunk;chunk.clear();bytes=0;}chunk+=c;bytes+=size;
                }
                if(!chunk.isEmpty())chunks<<chunk;
                QStringList translated;
                for(auto c:chunks){
                    usage(c);
                    auto d=http.json(query(url,{{"q",c},{"langpair",src+"|"+destination}})).toObject();
                    if(d.value("responseStatus").toInt()!=200)fail("MyMemory 暂不可用或今日额度已用完");
                    translated<<unescape(d.value("responseData").toObject().value("translatedText").toString());
                }return translated.join('\n');
            }else{
                QStringList urls{url};if(field(p,"endpoint").isEmpty())urls<<"https://translate.google.com/translate_a/single";
                QString failure;
                for(auto client:{"dict-chrome-ex","chrome-extension","gtx"}){
                    for(auto ep:urls){
                        try{
                            auto d=http.json(query(ep,{{"client",client},{"sl",origin},{"tl",destination},{"dt","t"},{"q",part}}),{},browser()).toArray();
                            if(d.isEmpty()||!d[0].isArray())fail("Google 返回格式不正确");
                            QString text;for(auto row:d[0].toArray())text+=at(row.toArray(),0).toString();
                            if(text.isEmpty())fail("Google 返回空文本");return text;
                        }catch(const std::exception& e){failure=QString::fromUtf8(e.what());if(cancel->load())throw;}
                    }
                }fail(failure);
            }
        };
        std::vector<QString> outputs(parts.size());std::atomic_int next{0};std::mutex errorsLock;QString error;
        std::vector<std::thread> workers;
        for(int i=0;i<std::min(4,int(parts.size()));++i)workers.emplace_back([&]{
            for(;;){
                int index=next.fetch_add(1);if(index>=parts.size())return;
                try{outputs[index]=one(parts[index]);}
                catch(const std::exception& e){std::lock_guard lock(errorsLock);if(error.isEmpty())error=QString::fromUtf8(e.what());next=parts.size();return;}
            }
        });
        for(auto& worker:workers)worker.join();
        if(!error.isEmpty())fail(error);
        for(auto text:outputs)result.segments<<text;
    }else if(provider=="microsoft"||provider=="edge"){
        QMap<QByteArray,QByteArray> headers;
        bool bing=false;
        if(provider=="edge"){
            headers=browser();
            try{
                QString cached;
                {QMutexLocker lock(&m_lock);if(m_edgeExpiry>QDateTime::currentSecsSinceEpoch()&&m_edgeProxy==field(p,"proxy"))cached=m_edgeToken;}
                if(cached.isEmpty()){
                    cached=QString::fromUtf8(http.request("https://edge.microsoft.com/translate/auth",{},headers,false,2500)).trimmed();
                    if(cached.isEmpty())fail("Edge 返回空凭据");
                    QMutexLocker lock(&m_lock);m_edgeToken=cached;m_edgeExpiry=QDateTime::currentSecsSinceEpoch()+420;m_edgeProxy=field(p,"proxy");
                }
                headers["Authorization"]=("Bearer "+cached).toUtf8();
            }catch(...){if(cancel->load())throw;bing=true;}
        }else{
            if(!url.endsWith("/translate"))url=url.remove(QRegularExpression("/+$"))+"/translate";
            headers["Ocp-Apim-Subscription-Key"]=key.toUtf8();
            if(!field(p,"region").isEmpty())headers["Ocp-Apim-Subscription-Region"]=field(p,"region").toUtf8();
        }
        if(bing){
            headers["Cache-Control"]="no-cache";headers["Referer"]="https://www.bing.com/translator";headers["Origin"]="https://www.bing.com";
            auto page=QString::fromUtf8(http.request(query("https://www.bing.com/translator",{{"timestamp",QString::number(QDateTime::currentMSecsSinceEpoch())}}),{},headers));
            auto helper=QRegularExpression("params_AbusePreventionHelper\\s*=\\s*(\\[[^\\]]+\\])").match(page);
            auto ig=QRegularExpression("IG:\"([^\"]+)\"").match(page);
            if(!helper.hasMatch()||!ig.hasMatch())fail("微软免费翻译入口暂不可用");
            auto credentials=QJsonDocument::fromJson(helper.captured(1).toUtf8()).array();
            auto iid=QRegularExpression("data-iid=\"(translator[^\"]+)\"").match(page);
            int index=0;
            for(auto part:parts){
                auto d=http.json(query("https://www.bing.com/ttranslatev3",{{"isVertical","1"},{"IG",ig.captured(1)},
                    {"IID",(iid.hasMatch()?iid.captured(1):"translator.5028")+"."+QString::number(++index)}}),
                    QJsonObject{{"fromLang",source=="auto"?"auto-detect":code(source,"zh-Hans")},{"to",code(target,"zh-Hans")},
                        {"text",part},{"key",at(credentials,0)},{"token",at(credentials,1)}},headers,true).toArray();
                result.segments<<at(at(d,0).toObject().value("translations").toArray(),0).toObject().value("text").toString();
            }
        }else{
            QMap<QString,QString> values{{"api-version","3.0"},{"to",code(target,"zh-Hans")}};
            if(source!="auto")values["from"]=code(source,"zh-Hans");
            QJsonArray body;for(auto part:parts)body.append(QJsonObject{{"Text",part}});
            QJsonArray d;
            try{d=http.json(query(url,values),body,headers).toArray();}
            catch(...){if(provider=="edge"){QMutexLocker lock(&m_lock);m_edgeExpiry=0;}throw;}
            for(auto v:d)result.segments<<at(v.toObject().value("translations").toArray(),0).toObject().value("text").toString();
            if(source=="auto"&&!d.isEmpty())result.source=d[0].toObject().value("detectedLanguage").toObject().value("language").toString(result.source).section('-',0,0).toLower();
        }
    }else if(provider=="zvo"){
        QMap<QString,QString> mapping{{"zh","chinese_simplified"},{"en","english"},{"ja","japanese"},{"ko","korean"},
            {"fr","french"},{"de","deutsch"},{"es","spanish"},{"ru","russian"}};
        QJsonObject body{{"from",mapping.value(source,source)},{"to",mapping.value(target,target)},{"lowercase","0"},
            {"text",QString::fromUtf8(QJsonDocument(array(parts)).toJson(QJsonDocument::Compact))}};
        if(!key.isEmpty())body["key"]=key;
        auto d=http.json(url,body,{},true).toObject();
        if(d.value("result").toInt()!=1)fail("translate.js 暂不可用或被限流");
        result.segments=strings(d.value("text"));
    }else if(provider=="deepl"){
        if(field(p,"endpoint").isEmpty()&&!key.endsWith(":fx"))url="https://api.deepl.com";
        if(!url.endsWith("/v2/translate"))url=url.remove(QRegularExpression("/+$"))+"/v2/translate";
        for(int off=0;off<parts.size();off+=50){
            QJsonObject body{{"text",array(parts.mid(off,50))},{"target_lang",target.toUpper()}};
            if(source!="auto")body["source_lang"]=source.toUpper();
            auto rows=http.json(url,body,{{"Authorization",("DeepL-Auth-Key "+key).toUtf8()}}).toObject().value("translations").toArray();
            if(rows.size()!=std::min(50,int(parts.size()-off)))fail("DeepL 返回的文本块不完整");
            for(auto row:rows)result.segments<<row.toObject().value("text").toString();
            if(source=="auto"&&!rows.isEmpty())result.source=rows[0].toObject().value("detected_source_language").toString(result.source).toLower();
        }
    }else{
        url.remove(QRegularExpression("/+$"));
        if(!url.endsWith("/chat/completions"))url+="/chat/completions";
        QString model=field(p,"model");
        QString prompt=QString("Translate from %1 into %2. Input is a JSON array of independent screen text blocks. "
            "Return a JSON array of translated strings with exactly the same length and order. Preserve internal line breaks and identifiers. No markdown or explanations.").arg(source,target);
        QJsonObject body{{"model",model},{"temperature",0},{"messages",QJsonArray{
            QJsonObject{{"role","system"},{"content",prompt}},
            QJsonObject{{"role","user"},{"content",QString::fromUtf8(QJsonDocument(array(parts)).toJson(QJsonDocument::Compact))}}}}};
        auto d=http.json(url,body,{{"Authorization",("Bearer "+key).toUtf8()}}).toObject();
        auto content=at(d.value("choices").toArray(),0).toObject().value("message").toObject().value("content").toString().trimmed();
        content.remove(QRegularExpression("^```(?:json)?\\s*|\\s*```$",QRegularExpression::CaseInsensitiveOption));
        auto doc=QJsonDocument::fromJson(content.toUtf8());if(!doc.isArray())fail("AI 返回格式不正确");result.segments=strings(doc.array());
    }
    if(result.segments.size()!=parts.size())fail("服务返回的文本块不完整");
    QRegularExpression number("^\\d+(?:[.,]\\d+)*(?:[%+])?$");
    for(int i=0;i<parts.size();++i){
        if(result.segments[i].trimmed().isEmpty())fail("服务返回空文本");
        if(number.match(parts[i].trimmed()).hasMatch())result.segments[i]=parts[i];
    }return result;
}
}

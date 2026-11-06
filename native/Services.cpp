#include "Services.h"
#include "Http.h"
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
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <vector>

namespace st {
namespace {
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }
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
QString endpoint(const QJsonObject& p)
{
    auto custom = field(p,"endpoint");
    return custom.isEmpty() ? endpoints.value(providers.indexOf(field(p,"provider"))) : custom;
}
QString code(QString language, const QString& chinese) { return language == "zh" ? chinese : language; }
QString fingerprint(const QJsonObject& p)
{
    // Match Python json.dumps([provider, endpoint, proxy, apiKey]) exactly.
    QStringList quoted;
    for (auto k : {"provider","endpoint","proxy","apiKey"}) {
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
        if(!headers.contains("User-Agent"))headers["User-Agent"]="WinShun-screenshot-translate/0.4.0";
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
    for(auto k:{"endpoint","model","region"})result[k]=stored.value(k).toString();
    return result;
}
void Services::save(const QJsonObject& drafts)
{
    QMutexLocker lock(&m_lock);auto values=m_stored;
    for(auto it=drafts.begin();it!=drafts.end();++it){
        if(!providers.contains(it.key()))fail("未知渠道");
        auto p=it.value().toObject();QString ep=field(p,"endpoint"),key=field(p,"apiKey");
        if(!ep.isEmpty()&&!validUrl(ep))fail("服务地址无效");
        values[it.key()]=QJsonObject{{"endpoint",ep},{"model",field(p,"model")},{"region",field(p,"region")},
            {"secret",key.isEmpty()?QString():QString::fromLatin1(crypt(key.toUtf8()).toBase64())}};
    }
    atomicJson(m_directory+"/services.json",{{"version",2},{"profiles",values}});m_stored=values;
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
    QString preferred=settings.value("provider").toString("mymemory");
    if(preferred=="ai")preferred=settings.value("aiProvider").toString("openai");
    if(!providers.contains(preferred))preferred="mymemory";
    bool fallback=draft.isEmpty()&&settings.value("fallbackEnabled").toBool(true);
    QList<QJsonObject> candidates;
    auto first=draft.isEmpty()?profile(preferred):draft;
    if(!draft.isEmpty())preferred=field(draft,"provider");
    candidates<<first;
    if(fallback)for(const auto& p:QStringList{"zvo","mymemory","google","edge","microsoft","deepl","openai","deepseek","doubao","hunyuan"}){
        if(p==preferred)continue;auto config=profile(p);
        if(providers.indexOf(p)<4||!field(config,"apiKey").isEmpty())candidates<<config;
    }
    double recovery=std::clamp(settings.value("fallbackRecoveryMinutes").toDouble(10),1.0,1440.0)*60;
    QElapsedTimer total;total.start();QString last;int attempted=0;
    for(auto p:candidates){
        if(cancel->load())fail("cancelled");
        p["proxy"]=settings.value("proxy").toString().trimmed();
        auto id=fingerprint(p);
        {
            QMutexLocker lock(&m_lock);
            if(fallback&&m_health.value(id).toObject().value("failedAt").toDouble()+recovery>QDateTime::currentSecsSinceEpoch())continue;
        }
        int budget=std::min(field(p,"provider")=="edge"?14000:8000,int((fallback||preferred=="edge"?20000:8000)-total.elapsed()));
        if(budget<=0)break;
        ++attempted;
        try{
            auto result=route(p,parts,source,target,cancel,budget);
            result.provider=field(p,"provider");result.preferred=preferred;result.fallback=result.provider!=preferred;
            QMutexLocker lock(&m_lock);m_health.remove(id);
            try{atomicJson(m_directory+"/service-health.json",{{"version",1},{"services",m_health}});}catch(...){}
            return result;
        }catch(const std::exception& e){
            if(cancel->load())fail("cancelled");
            last=QString::fromUtf8(e.what());if(!fallback)throw;
            QMutexLocker lock(&m_lock);
            auto now=double(QDateTime::currentSecsSinceEpoch());
            m_health[id]=QJsonObject{{"provider",field(p,"provider")},{"failedAt",now},{"retryAt",now+recovery}};
            try{atomicJson(m_directory+"/service-health.json",{{"version",1},{"services",m_health}});}catch(...){}
        }
    }
    fail(!last.isEmpty()?last:attempted?"翻译超时":"所有可用渠道暂时失败，恢复间隔后会重试；修改渠道配置可立即重试");
}
Translation Services::route(const QJsonObject& p,const QStringList& parts,const QString& source,
    const QString& target,Cancel cancel,int budget)
{
    QString provider=field(p,"provider"),key=field(p,"apiKey"),url=endpoint(p);
    if(!providers.contains(provider))fail("未知翻译渠道");
    if(providers.indexOf(provider)>=4&&key.isEmpty())fail("未配置 API 密钥");
    if(provider=="doubao"&&field(p,"model").isEmpty())fail("豆包需要模型 / 接入点 ID");
    Http http(field(p,"proxy"),cancel,budget);
    Translation result;result.source=source=="auto"?detect(parts.join('\n')):source;result.target=target;
    QString destination=code(target,"zh-CN"),origin=code(source,"zh-CN");
    if(provider=="mymemory"||provider=="google"){
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
        if(!url.endsWith("/chat/completions"))url=url.remove(QRegularExpression("/+$"))+"/chat/completions";
        QString model=field(p,"model");
        if(model.isEmpty())model=provider=="deepseek"?"deepseek-flash":provider=="hunyuan"?"hunyuan-turbos-latest":"gpt-4o-mini";
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

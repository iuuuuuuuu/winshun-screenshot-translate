#include "Services.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <stdexcept>
namespace st {
void testTranslationContext(const QString& base)
{
    auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    auto fresh=[] {return std::make_shared<std::atomic_bool>(false);};
    QTemporaryDir dir;Services service(dir.path());
    check(!service.routing().value("aiContextEnabled").toBool()&&service.routing().value("aiContextMaxChars")==600,"nearby context defaults off, 600 chars");
    service.save({},{{"aiContextEnabled",true},{"aiContextMaxChars",200}});
    Services reopened(dir.path());check(reopened.routing().value("aiContextEnabled").toBool()&&reopened.routing().value("aiContextMaxChars")==200,"context settings persist");
    reopened.save({},{{"aiContextMaxChars",999999}});check(reopened.routing().value("aiContextMaxChars")==600,"invalid context budget normalized");
    check(contextSelectionMatches("A\r\nsmall  selection","A small selection")&&!contextSelectionMatches("selection","a selection")&&!contextSelectionMatches("", ""),"full selection must match for context");
    auto bounded=boundedTranslationContext({{"before",QString(1100,'b')},{"after",QString(1100,'a')}},600);
    check(bounded.value("before").toString().size()==300&&bounded.value("after").toString().size()==300,"combined context bound");
    bounded=boundedTranslationContext({{"after",QString(1100,'a')}},600);check(bounded.value("after").toString().size()==600,"document-start budget available after selection");
    const QString emoji=QString::fromUcs4(U"😀");
    bounded=boundedTranslationContext({{"before",emoji.repeated(600)},{"after",emoji.repeated(600)}},601);
    for(auto value:bounded){auto text=value.toString();check(!text.front().isLowSurrogate()&&!text.back().isHighSurrogate(),"context never splits UTF-16 pair");}
    bounded=boundedTranslationContext({{"before",QString("distant")+QChar(0xfffc)+"near"},{"after",QString("next")+QChar(0xfffc)+"distant"}},600);
    check(bounded.value("before")=="near"&&bounded.value("after")=="next","embedded objects bound context");
    QJsonObject ai{{"endpoint",base+"/ai-context"},{"apiKey","fixture-key"},{"model","context-model"},{"streaming",false}};
    service.save({{"ai",ai},{"google",QJsonObject{{"endpoint",base+"/google"}}}});
    QJsonObject settings{{"provider","ai"},{"fallbackEnabled",false},{"aiContextEnabled",true},{"aiContextMaxChars",200}};
    int reads=0;
    auto river=std::make_shared<TranslationContext>([&](Cancel){++reads;return QJsonObject{{"before","They sat by the river"},{"after",QString(700,'x')}};},200);
    auto translate=[&](const QJsonObject& config,const std::shared_ptr<TranslationContext>& context,const QStringList& parts=QStringList{"bank"}){return service.translate(config,parts,"en","zh",fresh(),{},{},context);};
    auto off=settings;off["aiContextEnabled"]=false;
    check(translate(off,river).segments==QStringList{"unqualified:bank"}&&reads==0,"disabled context performs no read or send");
    auto google=settings;google["provider"]="google";
    translate(google,river);check(reads==0,"successful non-AI provider never reads context");
    auto contextual=translate(settings,river);
    check(contextual.segments==QStringList{"河岸"}&&reads==1,"context reaches AI only with explicit opt-in");
    check(translate(settings,river).segments==contextual.segments&&reads==1,"same selection reads once");
    auto finance=std::make_shared<TranslationContext>([](Cancel){return QJsonObject{{"before","She deposited money at the"},{"after","before closing time"}};},200);
    check(translate(settings,finance).segments==QStringList{"银行"},"different context cannot reuse translation cache");
    auto missing=std::make_shared<TranslationContext>([](Cancel)->QJsonObject{throw std::runtime_error("unavailable");});
    check(translate(settings,missing).segments==QStringList{"unqualified:bank"},"context failure remains ordinary translation");
    QTemporaryDir fallbackDir;Services fallback(fallbackDir.path());QJsonObject drafts;
    for(const auto& provider:providers)drafts[provider]=QJsonObject{{"enabled",false}};
    drafts["ai"]=ai;drafts["google"]=QJsonObject{{"endpoint",base+"/batch-rate-context"},{"enabled",true}};fallback.save(drafts);
    auto fallbackSettings=settings;fallbackSettings["provider"]="google";fallbackSettings["fallbackEnabled"]=true;
    auto aiFallback=fallback.translate(fallbackSettings,{"bank"},"en","zh",fresh(),{},{},river);
    check(aiFallback.provider=="ai"&&aiFallback.fallback&&aiFallback.segments==QStringList{"河岸"},"fallback to AI preserves context");
    auto failingAi=ai;failingAi["endpoint"]=base+"/batch-rate-context";
    fallback.save({{"ai",failingAi},{"google",QJsonObject{{"endpoint",base+"/google"},{"enabled",true}}}});
    fallbackSettings["provider"]="ai";
    check(fallback.translate(fallbackSettings,{"bank"},"en","zh",fresh(),{},{},river).provider=="google","context never prevents fallback away from AI");
    int longReads=0;auto longContext=std::make_shared<TranslationContext>([&](Cancel){++longReads;return QJsonObject{{"before","river"}};});
    translate(settings,longContext,{QString(2100,'x')});check(longReads==0,"long input is translated fully without external context");
    auto cancelled=fresh();cancelled->store(true);check(river->get(cancelled).isEmpty(),"cancelled context never returned");
    auto interrupted=std::make_shared<TranslationContext>([](Cancel c){c->store(true);return QJsonObject{{"before","river"}};});
    auto cancel=fresh();check(interrupted->get(cancel).isEmpty()&&interrupted->snapshot().isEmpty(),"cancel during read never caches context");
    // Saved provider health/config and historical Translation data contain no reference text.
    check(!contextual.json({"bank"}).contains("selectionContext"),"result contains no nearby text");
    for(const auto& name:QDir(dir.path()).entryList({"*.json"},QDir::Files)){
        QFile file(dir.path()+"/"+name);file.open(QIODevice::ReadOnly);const auto bytes=file.readAll();
        check(!bytes.contains("river")&&!bytes.contains("deposited"),"context is not persisted");
    }
}
}

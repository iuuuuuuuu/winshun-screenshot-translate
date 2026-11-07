#include "Desktop.h"
#include "Services.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <QThread>
#include <thread>
#include <cstdio>
#include <stdexcept>
namespace {
void checkPhrases()
{
    auto check=[](bool yes,const char* message){if(!yes)throw std::runtime_error(message);};
    QList<st::Block> words{{"Read",{10,10,42,20},20},{"this",{58,10,36,20},20},
        {"sentence.",{100,10,86,20},20},{"Next",{194,10,42,20},20},{"one!",{242,10,36,20},20},
        {"Other column",{400,10,120,20},20},{"Other row",{10,45,100,20},20}};
    for(auto at:{QPointF(20,20),QPointF(72,20),QPointF(160,20)}){
        auto phrase=st::phraseAt(words,at);
        check(phrase.text=="Read this sentence.","pointer phrase keeps complete sentence at either end");
        check(phrase.box==QRectF(10,10,176,20),"phrase bounds contain only pointed sentence");
    }
    check(st::phraseAt(words,{210,20}).text=="Next one!","terminal punctuation separates adjacent sentences");
    check(st::phraseAt(words,{450,20}).text=="Other column","large gap separates columns");
    check(st::phraseAt(words,{40,55}).text=="Other row","pointer phrase does not cross rows");
    check(st::phraseAt(words,{340,20}).text.isEmpty(),"blank space does not select remote text");
    check(st::phraseAt({{"Settings",{10,10,80,20},20}},{40,20}).text=="Settings","standalone word unchanged");
    QList<st::Block> chinese{{"请",{0,0,20,20},20},{"稍",{21,0,20,20},20},{"等",{42,0,20,20},20},
        {"，",{63,0,20,20},20},{"再试",{84,0,40,20},20},{"。",{125,0,20,20},20},
        {"下一句",{146,0,60,20},20},{"！",{207,0,20,20},20}};
    check(st::phraseAt(chinese,{34,10}).text=="请稍等，再试。","Chinese phrase punctuation and spacing");
    check(st::phraseAt(chinese,{170,10}).text=="下一句！","Chinese sentence boundary");
    QList<st::Block> baseline{{"Read",{10,10,42,20},20},{"this",{58,10,36,20},20},
        {".",{96,30,3,3},3},{"Next",{106,10,42,20},20}};
    check(st::phraseAt(baseline,{70,20}).text=="Read this.","small baseline period keeps sentence boundary");
    QList<st::Block> comma{{"这是中文说明",{0,0,120,20},20},{"，",{122,15,4,5},5},
        {"hello",{141,2,45,17},17},{"world",{192,2,45,17},17},{"保留中文整句",{243,0,120,20},20}};
    check(st::phraseAt(comma,{30,10}).text=="这是中文说明， hello world 保留中文整句","tiny comma does not cut majority sentence before English");
    check(st::phraseAt(comma,{160,10}).text=="hello world","tiny comma still bounds minority content");
    QList<st::Block> mixed{{"这是一句中文说明，",{0,0,180,20},20},{"hello",{186,0,48,20},20},
        {"world",{240,0,48,20},20},{"还包括另一段英文",{294,0,160,20},20},
        {"ba",{460,0,20,20},20},{"by",{486,0,20,20},20},{"请保留完整原句。",{512,0,160,20},20}};
    for(auto point:{QPointF(205,10),QPointF(265,10)}){
        auto phrase=st::phraseAt(mixed,point);
        check(phrase.text=="hello world","minority language selects only pointed contiguous phrase");
        check(phrase.box==QRectF(186,0,102,20),"minority bounds exclude surrounding majority text");
    }
    check(st::phraseAt(mixed,{470,10}).text=="ba by","separate minority islands never join through Chinese");
    for(auto point:{QPointF(30,10),QPointF(350,10),QPointF(600,10)})
        check(st::phraseAt(mixed,point).text=="这是一句中文说明， hello world 还包括另一段英文 ba by 请保留完整原句。","majority language keeps complete mixed sentence");
    QList<st::Block> english{{"Please translate the",{0,0,170,20},20},{"中文",{176,0,40,20},20},
        {"words in this sentence.",{222,0,220,20},20}};
    check(st::phraseAt(english,{190,10}).text=="中文","Chinese minority in English is isolated");
    check(st::phraseAt(english,{40,10}).text=="Please translate the 中文 words in this sentence.","English majority retains embedded Chinese");
    QList<st::Block> merged{{"中文内容baby中文说明",{0,0,200,20},20}};
    check(st::phraseAt(merged,{100,10}).text=="baby","mixed-script OCR word is separated at pointer");
    check(st::phraseAt(merged,{20,10}).text=="中文内容baby中文说明","majority pointer in mixed OCR word keeps original");
    QList<st::Block> neutral{{"这是一段中文说明",{0,0,160,20},20},{"version",{166,0,60,20},20},
        {"2",{232,0,10,20},20},{"beta",{248,0,40,20},20},{"继续中文说明",{294,0,120,20},20}};
    check(st::phraseAt(neutral,{190,10}).text=="version 2 beta","numbers stay inside minority phrase");
    check(st::phraseAt(neutral,{237,10}).text=="version 2 beta","neutral seed between same-script words uses that script");
    check(st::phraseAt({{"漢字かなカナ",{0,0,120,20},20}},{20,10}).text=="漢字かなカナ","Japanese kana and Han remain together");
    check(st::phraseAt({{"English text has a",{0,0,160,20},20},{"привет",{166,0,60,20},20},
        {"word inside.",{232,0,110,20},20}},{190,10}).text=="привет","minority Cyrillic run also isolates");
    check(st::phraseAt({{"中文",{0,0,40,20},20},{"test",{46,0,40,20},20}},{60,10}).text=="中文 test","no strict majority preserves sentence");
    // Boxes from Windows OCR on small screen text: the two fragments of
    // "baby" have a 1.5px letter gap; actual word spaces are 4-5px wide.
    QList<st::Block> splitWord{{"hi,i",{9,10.5,16.5,11.5},11.5},{"ma",{30.5,13.5,18.5,8},8},
        {"you",{53,13.5,22.5,11},11},{"ba",{80.5,11,14,10.5},10.5},{"by",{96,11,14.5,13.5},13.5}};
    for(double scale:{.8,1.,1.25,1.5,2.}){
        auto scaled=splitWord;
        for(auto& word:scaled){auto b=word.box;word.box={b.x()*scale,b.y()*scale,b.width()*scale,b.height()*scale};word.glyphHeight*=scale;}
        for(auto word:scaled)
            check(st::phraseAt(scaled,word.box.center()).text=="hi,i ma you baby","split Latin word rejoins at every pointer and scale");
        check(st::groupWords(scaled).front().text=="hi,i ma you baby","pointer and screenshot spacing agree");
        scaled.back().box.translate(3.5*scale,0);
        for(auto word:scaled)
            check(st::phraseAt(scaled,word.box.center()).text=="hi,i ma you ba by","actual spaces between short words survive");
    }
    QList<st::Block> longLine;
    for(int i=0;i<100;++i)longLine.append({QString("word%1").arg(i),QRectF(i*65,0,60,20),20});
    auto local=st::phraseAt(longLine,{50*65+20,10});
    check(local.text.contains("word50")&&local.text.size()<=240&&!local.text.contains("word0 "),"long line stays local and bounded");
}
}
int main(int argc,char** argv)
{
    QGuiApplication app(argc,argv);
    try{
        auto args=app.arguments();
        if(args.contains("--ocr-image")){
            QImage image(args.value(args.indexOf("--ocr-image")+1));
            if(image.isNull())throw std::runtime_error("OCR diagnostic image unavailable");
            QList<st::Block> words;std::exception_ptr error;
            std::thread worker([&]{
                try{words=st::recognize(image,std::make_shared<std::atomic_bool>(false),false);}
                catch(...){error=std::current_exception();}
            });worker.join();if(error)std::rethrow_exception(error);
            QJsonArray found;
            for(auto word:words)found.append(QJsonObject{{"text",word.text},
                {"box",QJsonArray{word.box.x(),word.box.y(),word.box.width(),word.box.height()}},
                {"phrase",st::phraseAt(words,word.box.center()).text}});
            std::puts(QJsonDocument(QJsonObject{{"words",found}}).toJson(QJsonDocument::Compact).constData());return 0;
        }
        checkPhrases();
        if(st::detect("你好世界")!="zh"||st::targetFor("Hello","auto")!="zh")throw std::runtime_error("language");
        QTemporaryDir dir;st::Services services(dir.path());
        auto sharedDir=qEnvironmentVariable("WS_DATA_DIR");
        if(app.arguments().contains("--endpoint")&&!sharedDir.isEmpty()){
            st::Services shared(sharedDir);
            if(shared.profile("openai").value("apiKey").toString()!="python-fixture-key")throw std::runtime_error("Python DPAPI compatibility");
            shared.save({{"openai",QJsonObject{{"apiKey","native-fixture-key"}}}});
        }
        services.save({{"openai",QJsonObject{{"apiKey","native-test-密钥"},{"endpoint","https://example.com"},{"model","test"}}}});
        st::Services reopened(dir.path());
        if(reopened.profile("openai").value("apiKey").toString()!="native-test-密钥")throw std::runtime_error("DPAPI");
        if(args.contains("--endpoint")){
            auto base=args.value(args.indexOf("--endpoint")+1);
            QStringList routes;
            for(auto p:st::providers){
                if(p=="edge")continue;auto draft=services.profile(p);
                draft["endpoint"]=base+"/"+p;draft["apiKey"]="fixture-key";draft["model"]="fixture-model";
                auto cancel=std::make_shared<std::atomic_bool>(false);
                auto result=services.translate(QJsonObject{{"fallbackEnabled",false}},{"Hello + 100%","499"},"en","zh",cancel,draft);
                if(result.segments!=QStringList{"你好 + 100%","499"})throw std::runtime_error(("route "+p).toStdString());
                routes<<p;
            }
            auto chunked=services.profile("mymemory");chunked["endpoint"]=base+"/mymemory";
            auto freshCancel=[] {return std::make_shared<std::atomic_bool>(false);};
            auto proxied=chunked;proxied["endpoint"]="http://native-fixture.invalid/mymemory";
            auto proxyResult=services.translate(QJsonObject{{"proxy",base}},{"Hello + 100%"},"en","zh",freshCancel(),proxied);
            if(proxyResult.segments!=QStringList{"你好 + 100%"})throw std::runtime_error("explicit HTTP proxy");
            auto unicode=services.translate(QJsonObject{}, {QString(600,QChar(0x6c49))},"zh","en",freshCancel(),chunked);
            if(unicode.segments.size()!=1||unicode.segments[0].count(QChar(0x6c49))!=600)throw std::runtime_error("UTF-8 chunks");
            auto markup=services.translate(QJsonObject{},{"<button>&"},"en","zh",freshCancel(),chunked);
            if(markup.segments!=QStringList{"<button>&"})throw std::runtime_error("HTML entities");
            auto cancelled=std::make_shared<std::atomic_bool>(false);
            auto blocked=services.profile("mymemory");blocked["endpoint"]=base+"/blocked";
            std::thread cancelThread([cancelled]{QThread::msleep(70);cancelled->store(true);});
            bool aborted=false;try{services.translate(QJsonObject{},{"Hello"},"en","zh",cancelled,blocked);}catch(...){aborted=true;}
            cancelThread.join();if(!aborted)throw std::runtime_error("cancel");
            QJsonObject drafts{{"mymemory",QJsonObject{{"endpoint",base+"/fail"}}},
                {"zvo",QJsonObject{{"endpoint",base+"/zvo"}}}};
            services.save(drafts);
            QJsonObject settings{{"provider","mymemory"},{"fallbackEnabled",true},{"fallbackRecoveryMinutes",10}};
            auto fresh=[] {return std::make_shared<std::atomic_bool>(false);};
            auto one=services.translate(settings,{"Hello + 100%"},"en","zh",fresh());
            if(!one.fallback||one.provider!="zvo"||one.preferred!="mymemory")throw std::runtime_error("fallback");
            st::Services persisted(dir.path());
            auto two=persisted.translate(settings,{"Hello + 100%"},"en","zh",fresh());
            if(!two.fallback||two.provider!="zvo")throw std::runtime_error("remember fallback");
            QFile healthFile(dir.path()+"/service-health.json");healthFile.open(QIODevice::ReadOnly);
            auto health=QJsonDocument::fromJson(healthFile.readAll()).object();healthFile.close();auto entries=health.value("services").toObject();
            for(auto it=entries.begin();it!=entries.end();++it){auto v=it.value().toObject();v["failedAt"]=0;v["retryAt"]=0;it.value()=v;}
            st::atomicJson(dir.path()+"/service-health.json",{{"version",1},{"services",entries}});
            st::Services recovered(dir.path());recovered.translate(settings,{"Hello + 100%"},"en","zh",fresh());
            recovered.save({{"mymemory",QJsonObject{{"endpoint",base+"/mymemory"}}}});
            auto primary=recovered.translate(settings,{"Hello + 100%"},"en","zh",fresh());
            if(primary.fallback||primary.provider!="mymemory")throw std::runtime_error("config reset health");
            std::puts(QJsonDocument(QJsonObject{{"passed",true},{"fixtureRoutes",QJsonArray::fromStringList(routes)},
                {"dpapi",true},{"persistentFallback",true},{"recovery",true},{"cancel",true},{"httpProxy",true},{"pointerPhrases",true}}).toJson(QJsonDocument::Compact).constData());
        }else if(args.contains("--live")){
            QJsonArray routes;
            for(auto p:QStringList{"google","edge","zvo","mymemory"}){
                QJsonObject settings{{"provider",p},{"fallbackEnabled",false},{"proxy",args.value(args.indexOf("--proxy")+1)}};
                QJsonObject result{{"provider",p}};
                try{
                    auto data=services.translate(settings,{"Hello world"},"en","zh",std::make_shared<std::atomic_bool>(false));
                    result["status"]="success";result["translation"]=data.segments.join('\n');
                }catch(const std::exception& e){result["status"]="unavailable";result["error"]=QString::fromUtf8(e.what());}
                routes.append(result);
            }
            std::puts(QJsonDocument(QJsonObject{{"live",routes}}).toJson(QJsonDocument::Compact).constData());
        }else std::puts("native core tests passed");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"test failed: %s\n",e.what());return 1;}
}

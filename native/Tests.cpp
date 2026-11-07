#include "Desktop.h"
#include "Services.h"
#include "Dictionary.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QPainter>
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
    QImage missingInk(180,50,QImage::Format_RGB32);missingInk.fill(Qt::white);
    QList<st::Block> missingWords{{"prefix",{5,10,40,20},20},{"tail",{100,10,40,20},20}};
    check(st::phraseAt(missingWords,{20,20},&missingInk).text=="prefix","blank column gap is not bridged");
    QPainter ink(&missingInk);ink.setPen(Qt::black);QFont inkFont("Segoe UI");inkFont.setPixelSize(20);ink.setFont(inkFont);ink.drawText(53,28,"lost");ink.end();
    check(st::phraseAt(missingWords,{20,20},&missingInk).text=="prefix tail","unrecognized ink does not end the sentence at a false column");
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
void checkWrappedPhrases()
{
    auto check=[](bool yes,const char* message){if(!yes)throw std::runtime_error(message);};
    QList<st::Block> mixed{{"这里是一段中文说明，",{10,10,220,20},20},{"hi,i",{236,12,35,18},18},{"ma",{277,16,24,12},12},
        {"you",{10,46,30,16},16},{"baby",{46,42,40,20},20},{"，后面仍然是中文说明。",{92,42,220,20},20}};
    for(double scale:{.8,1.,1.25,1.5,2.}){
        auto scaled=mixed;for(auto& w:scaled){auto b=w.box;w.box={b.x()*scale,b.y()*scale,b.width()*scale,b.height()*scale};w.glyphHeight*=scale;}
        for(int i:{1,2,3,4}){
            auto result=st::wrappedPhraseAt(scaled,scaled[i].box.center());
            check(result.text=="hi,i ma you baby","mixed minority phrase continues from either wrapped line");
            check(result.box.contains(scaled[i].box.center())&&result.box.bottom()>=scaled[4].box.bottom(),"wrapped bounds map back to physical rows");
        }
        auto majority=st::wrappedPhraseAt(scaled,scaled[0].box.center()).text;
        check(majority.contains("hi,i ma you baby")&&majority.endsWith("中文说明。"),"majority sentence crosses the same wrap");
    }
    QList<st::Block> english{{"Please",{10,10,60,20},20},{"translate",{76,10,80,20},20},{"this",{162,10,40,20},20},
        {"whole",{10,42,50,20},20},{"sentence",{66,42,80,20},20},{"across",{152,42,60,20},20},
        {"three",{10,74,50,20},20},{"lines.",{66,74,50,20},20}};
    for(auto w:english)check(st::wrappedPhraseAt(english,w.box.center()).text=="Please translate this whole sentence across three lines.","three-line English sentence from every word");
    auto stopped=english;stopped[2].text="this.";
    check(st::wrappedPhraseAt(stopped,stopped[0].box.center()).text=="Please translate this.","terminal punctuation prevents paragraph merge");
    auto list=english;list[3].text="1. whole";
    check(st::wrappedPhraseAt(list,list[0].box.center()).text=="Please translate this","numbered list starts a new item");
    list[3].text="• whole";
    check(st::wrappedPhraseAt(list,list[0].box.center()).text=="Please translate this","bullet list starts a new item");
    auto spaced=english;for(int i=3;i<spaced.size();++i)spaced[i].box.translate(0,35);
    check(st::wrappedPhraseAt(spaced,spaced[0].box.center()).text=="Please translate this","blank line prevents paragraph merge");
    auto indented=english;for(int i=3;i<indented.size();++i)indented[i].box.translate(80,0);
    check(st::wrappedPhraseAt(indented,indented[0].box.center()).text=="Please translate this","different column alignment does not wrap");
    auto columns=english;for(auto w:english){w.box.translate(400,0);w.text="Other";columns<<w;}
    check(st::wrappedPhraseAt(columns,english[4].box.center()).text=="Please translate this whole sentence across three lines.","adjacent column never enters the sentence");
    QList<st::Block> labels{{"Settings",{10,10,75,20},20},{"History",{10,42,65,20},20}};
    check(st::wrappedPhraseAt(labels,{30,20}).text=="Settings","stacked short UI labels stay separate");
    check(st::wrappedPhraseAt(spaced,{150,48}).text.isEmpty(),"blank between paragraphs does not select a phrase");
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
                try{words=st::recognize(image,std::make_shared<std::atomic_bool>(false),false,
                    args.contains("--ocr-scale")?args.value(args.indexOf("--ocr-scale")+1).toDouble():2.);}
                catch(...){error=std::current_exception();}
            });worker.join();if(error)std::rethrow_exception(error);
            QJsonArray found;
            for(auto word:words)found.append(QJsonObject{{"text",word.text},
                {"box",QJsonArray{word.box.x(),word.box.y(),word.box.width(),word.box.height()}},
                {"phrase",st::phraseAt(words,word.box.center()).text}});
            QJsonArray pointed;
            if(args.contains("--ocr-points")){
                std::thread reader([&]{try{
                    for(auto pair:args.value(args.indexOf("--ocr-points")+1).split(';')){
                        auto xy=pair.split(',');QPointF at(xy.value(0).toDouble(),xy.value(1).toDouble());QElapsedTimer elapsed;elapsed.start();
                        auto phrase=st::recognizeAt(image,std::make_shared<std::atomic_bool>(false),at);
                        pointed.append(QJsonObject{{"point",QJsonArray{at.x(),at.y()}},{"text",phrase.text},{"ms",double(elapsed.elapsed())},
                            {"box",QJsonArray{phrase.box.x(),phrase.box.y(),phrase.box.width(),phrase.box.height()}}});
                    }
                }catch(...){error=std::current_exception();}});reader.join();if(error)std::rethrow_exception(error);
            }
            std::puts(QJsonDocument(QJsonObject{{"words",found},{"pointed",pointed}}).toJson(QJsonDocument::Compact).constData());return 0;
        }
        checkPhrases();
        checkWrappedPhrases();
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
            auto freshCancel=[] {return std::make_shared<std::atomic_bool>(false);};
            for(bool dark:{false,true}){
                QImage small(101,55,QImage::Format_RGB32);small.fill(dark?QColor(32,32,32):Qt::white);
                {QPainter painter(&small);painter.setPen(dark?Qt::white:Qt::black);QFont font("Segoe UI");font.setPixelSize(13);painter.setFont(font);painter.drawText(23,33,"Hi");}
                QList<st::Block> found;std::exception_ptr ocrError;
                std::thread reader([&]{try{found=st::recognize(small,freshCancel());}catch(...){ocrError=std::current_exception();}});reader.join();if(ocrError)std::rethrow_exception(ocrError);
                if(found.size()!=1||found[0].text!="Hi"||!QRectF(small.rect()).contains(found[0].box)||found[0].box.width()>25){
                    small.save(QCoreApplication::applicationDirPath()+"/small-ocr-fixture.png");
                    QJsonArray actual;for(auto word:found)actual.append(QJsonObject{{"text",word.text},{"box",QJsonArray{word.box.x(),word.box.y(),word.box.width(),word.box.height()}}});
                    throw std::runtime_error((QString("small screenshot OCR dark=%1 ").arg(dark)+QString::fromUtf8(QJsonDocument(actual).toJson(QJsonDocument::Compact))).toStdString());
                }
            }
            QImage blank(60,25,QImage::Format_RGB32);blank.fill(Qt::white);QList<st::Block> empty;
            std::thread blankReader([&]{empty=st::recognize(blank,freshCancel());});blankReader.join();
            if(!empty.isEmpty())throw std::runtime_error("blank screenshot must stay empty");
            // Existing saved providers migrate once, keeping both DPAPI secrets
            // and the selected provider's endpoint/model. Legacy backups remain.
            QTemporaryDir oldDir;st::Services old(oldDir.path());
            old.save({{"openai",QJsonObject{{"apiKey","unused"}}},
                {"deepseek",QJsonObject{{"apiKey","chosen-secret"},{"endpoint",base+"/ai"},{"model","chosen-model"}}}});
            old.migrateAi({{"provider","ai"},{"aiProvider","deepseek"}});
            auto migrated=old.profile("ai");
            if(migrated.value("apiKey")!="chosen-secret"||migrated.value("model")!="chosen-model"||migrated.value("endpoint")!=base+"/ai")throw std::runtime_error("selected AI profile migration");
            old.save({{"ai",QJsonObject{{"apiKey","edited"},{"model","new-model"}}}});old.migrateAi({{"provider","openai"}});
            if(old.profile("ai").value("apiKey")!="edited"||old.profile("deepseek").value("apiKey")!="chosen-secret")throw std::runtime_error("AI migration preserves edits and legacy backups");
            QTemporaryDir defaultsDir;st::Services legacyDefaults(defaultsDir.path());
            legacyDefaults.save({{"deepseek",QJsonObject{{"apiKey","secret"}}}});legacyDefaults.migrateAi({{"provider","deepseek"}});
            if(legacyDefaults.profile("ai").value("endpoint")!="https://api.deepseek.com"||legacyDefaults.profile("ai").value("model")!="deepseek-flash")throw std::runtime_error("legacy AI defaults retained");
            if(st::dictionaryEntry("HELLO").isEmpty()||st::dictionaryEntry("baby").isEmpty()||!st::dictionaryEntry("zznotaword7789").isEmpty())throw std::runtime_error("bundled ECDICT lookup");
            QTemporaryDir dictionaryDir;st::Services dictionary(dictionaryDir.path());
            dictionary.save({{"zvo",QJsonObject{{"endpoint",base+"/zvo"}}}});
            auto missing=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",true}},{"zznotawordtest"},"en","zh",freshCancel());
            auto known=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",true}},{"hello"},"en","zh",freshCancel());
            if(missing.provider!="zvo"||known.provider!="ecdict"||known.fallback)throw std::runtime_error("dictionary misses must not quarantine lookup");
            auto offline=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",false},{"proxy","invalid proxy"}},{"hello"},"en","zh",freshCancel());
            if(offline.provider!="ecdict")throw std::runtime_error("offline dictionary must not depend on proxy configuration");
            auto sentence=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",true}},{"This is a complete sentence that is not in the dictionary."},"en","zh",freshCancel());
            if(sentence.provider!="zvo")throw std::runtime_error("dictionary sentence fallback");
            QStringList routes;
            for(auto p:st::providers){
                if(p=="edge"||p=="ecdict")continue;auto draft=services.profile(p);
                draft["endpoint"]=base+"/"+p;draft["apiKey"]="fixture-key";draft["model"]="fixture-model";
                if(p=="youdao")draft["appId"]="fixture-app";
                auto cancel=std::make_shared<std::atomic_bool>(false);
                auto result=services.translate(QJsonObject{{"fallbackEnabled",false}},{"Hello + 100%","499"},"en","zh",cancel,draft);
                if(result.segments!=QStringList{"你好 + 100%","499"})throw std::runtime_error(("route "+p).toStdString());
                routes<<p;
            }
            auto chunked=services.profile("mymemory");chunked["endpoint"]=base+"/mymemory";
            auto freeYoudao=services.profile("youdao");freeYoudao["endpoint"]=base+"/youdao-dictionary";
            auto lookup=services.translate({}, {"hello"},"en","zh",freshCancel(),freeYoudao);
            if(lookup.segments!=QStringList{"int. 你好"})throw std::runtime_error("Youdao free dictionary");
            auto official=freeYoudao;official["endpoint"]=base+"/youdao";official["apiKey"]="fixture-key";official["appId"]="fixture-app";
            auto unicodeSign=services.translate({}, {QString::fromUtf8("你好😀这是用于测试有道签名的超长 Unicode 字符串 1234567890")},"zh","en",freshCancel(),official);
            if(unicodeSign.segments.isEmpty())throw std::runtime_error("Youdao Unicode signing");
            auto gbk=services.profile("sogou");gbk["endpoint"]=base+"/sogou-gbk";
            auto gbkResult=services.translate({}, {"Hello + 100%"},"en","zh",freshCancel(),gbk);
            if(gbkResult.segments!=QStringList{"你好 + 100%"})throw std::runtime_error("Sogou GB18030 page");
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
                {"dpapi",true},{"smallScreenshotOcr",true},{"aiMigration",true},{"offlineDictionary",true},{"dictionaryFallback",true},{"youdaoSignature",true},{"sogouEncoding",true},
                {"persistentFallback",true},{"recovery",true},{"cancel",true},{"httpProxy",true},{"pointerPhrases",true}}).toJson(QJsonDocument::Compact).constData());
        }else if(args.contains("--live")){
            QJsonArray routes;
            auto live=args.contains("--live-providers")?args.value(args.indexOf("--live-providers")+1).split(','):QStringList{"google","edge","zvo","mymemory","sogou","youdao","ecdict"};
            for(auto p:live){
                QJsonObject settings{{"provider",p},{"fallbackEnabled",false},{"proxy",args.contains("--proxy")?args.value(args.indexOf("--proxy")+1):QString()}};
                QJsonObject result{{"provider",p}};
                try{
                    auto data=services.translate(settings,{p=="youdao"||p=="ecdict"?"hello":"Hello world. How are you?"},"en","zh",std::make_shared<std::atomic_bool>(false));
                    result["status"]="success";result["translation"]=data.segments.join('\n');
                }catch(const std::exception& e){result["status"]="unavailable";result["error"]=QString::fromUtf8(e.what());}
                routes.append(result);
            }
            std::puts(QJsonDocument(QJsonObject{{"live",routes}}).toJson(QJsonDocument::Compact).constData());
        }else std::puts("native core tests passed");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"test failed: %s\n",e.what());return 1;}
}

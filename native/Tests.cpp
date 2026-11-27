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
#include <QDateTime>
#include <QPainter>
#include <QFontMetrics>
#include <QDir>
#include <thread>
#include <future>
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
    QList<st::Block> skewed{{"Rotate",{10,30,55,18},18,1},{"text",{72,23,40,18},18,1},
        {"correctly.",{120,12,85,18},18,1}};
    auto row=st::groupWords(skewed);
    check(row.size()==1&&row.front().text=="Rotate text correctly.","native OCR line retains reading order across tilted boxes");
    skewed.append({"Other column",{320,24,120,18},18,2});
    row=st::groupWords(skewed);
    check(row.size()==2&&row.front().text=="Rotate text correctly."&&row.back().text=="Other column","native OCR lines never swallow another column");
    check(st::groupWords({{"Read",{10,10,40,20},20,1},{".",{52,28,3,3},3,1}}).front().text=="Read.","native baseline punctuation stays attached");
    const auto toolbar=st::groupWords({{"Remote",{900,21,80,19},19},{"Prod",{20,30,36,15},15},
        {"ucts",{57.5,32,31.5,13},13},{"Last",{1100,29,40,16},16}});
    check(toolbar.size()==3&&toolbar[0].text=="Products"&&toolbar[1].text=="Remote"&&toolbar[2].text=="Last",
        "remote search boxes do not split words or reverse column order");
    const auto dots=st::groupWords({{"3",{10,10,8,14},14},{"．",{19,22,2,2},2},{"47",{22,10,18,14},14},
        {"，",{41,22,2,2},2},{"9",{44,10,8,14},14}});
    check(dots.size()==1&&dots[0].text=="3.47.9","numeric punctuation rejoins without inserted spaces");
    const auto thousands=st::groupWords({{"1",{10,10,8,14},14},{"，",{19,21,3,4},4},{"218",{23,10,24,14},14}});
    check(thousands.size()==1&&thousands[0].text=="1,218","real thousands commas remain commas");
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
void checkScreenshotLayout()
{
    auto check=[](bool yes,const char* message){if(!yes)throw std::runtime_error(message);};
    check(st::screenshotTranslationText("A sentence wraps\nonto the next line.")=="A sentence wraps onto the next line.","OCR soft wraps join before translation");
    check(st::screenshotTranslationText("1.25-\nbit and 30B-\nA3B, real-\nworld")=="1.25-bit and 30B-A3B, real-world","printed hyphens preserve model names and compounds");
    check(st::screenshotTranslationText(QString("trans%1\nlation").arg(QChar(0x00ad)))=="translation","explicit discretionary hyphen is removed");
    check(st::screenshotTranslationText("第一行文字，\n下一行。\n\n第二段。")=="第一行文字，下一行。\n\n第二段。","CJK wraps have no extra space and blank paragraphs survive");
    check(st::screenshotTranslationText("List:\n1. First\n2. Second\n• Third")=="List:\n1. First\n2. Second\n• Third","numbered and bullet boundaries survive");
    check(st::screenshotTranslationText("First paragraph.\r\n\r\nSecond paragraph.")=="First paragraph.\n\nSecond paragraph.","Windows line endings retain paragraphs");
    auto paragraph=st::groupWords({{"Long sentence continues",{10,10,270,20},20,1},
        {"over the next row.",{10,42,220,20},20,2},{"Separate paragraph.",{10,115,230,20},20,3}});
    check(paragraph.size()==2&&st::screenshotTranslationText(paragraph[0].text)=="Long sentence continues over the next row.","paragraph geometry excludes blank paragraph gap");
    auto bullets=st::groupWords({{"• First item",{10,10,180,20},20,1},{"• Second item",{10,42,180,20},20,2}});
    check(bullets.size()==2,"OCR bullet items keep separate translation blocks");
    const auto hyphens=st::groupWords({{"1.25",{10,10,45,20},20,1},{"一",{57,21,9,3},3,1},
        {"bit quantization",{10,42,180,20},20,2}});
    check(hyphens.size()==1&&st::screenshotTranslationText(hyphens[0].text)=="1.25-bit quantization","tiny Latin line-end hyphen misread as Han is repaired");
    const auto han=st::groupWords({{"这是一",{10,10,60,20},20,1},{"段文字",{74,10,60,20},20,1}});
    check(han.size()==1&&han[0].text=="这是一段文字","real Han text is never changed to hyphens");
    for(bool dark:{false,true}){
        QImage image(400,300,QImage::Format_RGB32);image.fill(dark?QColor(30,30,30):Qt::white);
        QPainter paint(&image);QFont font("Segoe UI");font.setPixelSize(18);paint.setFont(font);
        QList<st::Block> words;
        for(int row=0;row<5;++row){
            int top=20+row*45;paint.setPen(QPen(dark?QColor(160,170,180):QColor(90,100,110),2));
            paint.drawRect(QRect(20,top,18,18));
            // Deliberately different OCR guesses for equally graphical marks.
            if(row==4){
                words.append({"〈",QRectF(19,top-1,9,20),20});
                words.append({"〉",QRectF(30,top-1,9,20),20});
            }else words.append({QStringList{"旧","中","0","O"}[row],QRectF(19,top-1,20,20),20});
            paint.setPen(dark?Qt::white:Qt::black);paint.drawText(52,top+17,"Settings");
            words.append({"Settings",QRectF(52,top,70,20),20});
            paint.drawText(145,top+17,"7");words.append({"7",QRectF(145,top,10,20),20});
        }
        paint.end();auto cleaned=st::filterUiIcons(image,words);
        check(cleaned.size()==10,"graphical prefixes excluded while numeric badges remain");
        for(int row=0;row<5;++row)check(cleaned[row*2].text=="Settings"&&cleaned[row*2+1].text=="7","icon filtering preserves label order and counts");
        auto translated=st::translatedImage(image,st::groupWords(cleaned),{"设置 7","设置 7","设置 7","设置 7","设置 7"},dark);
        for(int row=0;row<5;++row)check(translated.copy(18,18+row*45,24,24)==image.copy(18,18+row*45,24,24),"translation does not paint over excluded icons");
        auto numbered=words.mid(0,12);
        for(int row=0;row<4;++row){
            numbered[row*3].text=QStringList{"0","1","12","A"}[row];
            numbered[row*3].box.setWidth(row==2?20:10);
        }
        check(st::filterUiIcons(image,numbered).size()==numbered.size(),"coloured list numbers and letter prefixes remain");
        // A real short prefix can have exactly the same geometry as an icon.
        // With text-coloured ink it must not disappear, even in aligned rows.
        {QPainter p(&image);p.setPen(QPen(dark?Qt::white:Qt::black,2));
            for(int row=0;row<5;++row)p.drawRect(QRect(20,20+row*45,18,18));}
        auto preserved=st::filterUiIcons(image,words);
        if(preserved.size()!=words.size()){
            image.save(QCoreApplication::applicationDirPath()+"/prefix-fixture.png");
            QStringList actual;for(auto w:preserved)actual<<w.text;
            throw std::runtime_error((QString("same-colour prefixes dark=%1: ").arg(dark)+actual.join(" / ")).toStdString());
        }
        check(st::filterUiIcons(image,words.mid(0,2)).size()==2,"isolated mixed-language prefix is not classified from text alone");
    }
    for(int size:{11,18,36}){
        QImage source(400,120,QImage::Format_RGB32);source.fill(Qt::white);
        QFont font("Segoe UI");font.setPixelSize(size);QFontMetricsF metrics(font);
        QRectF ink=metrics.tightBoundingRect("Settings").translated(20,65);
        {QPainter p(&source);p.setPen(Qt::black);p.setFont(font);p.drawText(QPointF(20,65),"Settings");}
        auto blocks=st::groupWords({{"Settings",ink,ink.height()}});
        check(std::abs(blocks.first().fontPixels-size)<=2,"source font estimate follows original glyph size");
        check(st::translatedImage(source,blocks,{"Settings"},false)==source,"unchanged text keeps original typography and nearby symbols");
        QList<ws::capture::RedactLine> selectable;
        const auto output=st::translatedImage(source,blocks,{"设置"},false,&selectable);
        QRect inkBounds;
        for(int y=0;y<output.height();++y)for(int x=0;x<output.width();++x)if(qGray(output.pixel(x,y))<100)inkBounds=inkBounds.united(QRect(x,y,1,1));
        check(inkBounds.height()>=size*.7&&inkBounds.height()<=size*1.2,"translation tracks small and large original fonts without fixed clamps");
        check(std::abs(inkBounds.top()-ink.top())<=2,"translated ink aligns with source ink");
        check(!selectable.isEmpty()&&selectable.first().text=="设置","new font layout retains selectable translated text");
    }
    {
        QImage source(300,90,QImage::Format_RGB32);source.fill(Qt::white);
        QFont font("Segoe UI");font.setPixelSize(18);QFontMetricsF metrics(font);
        const auto ink=metrics.tightBoundingRect("lib").translated(20,40);
        {QPainter p(&source);p.setPen(Qt::black);p.setFont(font);p.drawText(QPointF(20,40),"lib");
            p.setPen(QPen(Qt::darkGray,2));p.drawRect(95,15,50,45);}
        QList<ws::capture::RedactLine> selected;
        auto output=st::translatedImage(source,st::groupWords({{"lib",ink,ink.height()}}),{"图书馆"},false,&selected);
        check(selected.size()==1&&selected[0].text=="图书馆","short translated label uses free horizontal space before wrapping");
        check(source.copy(93,13,55,50)==output.copy(93,13,55,50),"translation preserves adjacent graphics and button borders");
        check(source.copy(155,0,145,90)==output.copy(155,0,145,90),"background clearing stays within used text area");
        {QPainter p(&source);p.setPen(Qt::gray);p.drawLine(74,15,74,60);}
        output=st::translatedImage(source,st::groupWords({{"lib",ink,ink.height()}}),{"主要内容"},false,&selected);
        check(selected.size()==1&&selected[0].text=="主要内容","short button label fits one line at a smaller font when needed");
        check(source.copy(73,15,3,46)==output.copy(73,15,3,46),"one-line fitting does not paint through adjacent border");
    }
    {
        QImage source(260,100,QImage::Format_RGB32);source.fill(Qt::white);
        QFont font("Segoe UI");font.setPixelSize(20);QFontMetricsF metrics(font);
        auto mark=metrics.tightBoundingRect("!").translated(100,45);
        {QPainter p(&source);p.setPen(QColor(115,80,220));p.setFont(font);p.drawText(QPointF(100,45),"!");
            p.fillRect(20,25,6,6,QColor(115,80,220));p.fillRect(34,25,6,6,QColor(115,80,220));
            p.fillRect(26,31,8,6,QColor(115,80,220));p.fillRect(34,38,6,9,QColor(115,80,220));}
        const auto result=st::filterUiIcons(source,{{"；",{34,25,6,22},22},{"!",mark,mark.height()}});
        check(result.size()==1&&result.front().text=="!","avatar fragments are removed while genuine coloured punctuation survives");
    }
    {
        QImage source(200,60,QImage::Format_RGB32);source.fill(Qt::white);
        {QPainter p(&source);p.setPen(QPen(Qt::gray,2));p.drawEllipse(10,10,18,18);
            QFont font("Segoe UI");font.setPixelSize(13);p.setFont(font);p.setPen(Qt::black);p.drawText(35,25,"1218");}
        auto result=st::filterUiIcons(source,{{"01218",{9,9,58,21},21}});
        check(result.size()==1&&result[0].text=="1218"&&result[0].box.left()>30,"fused history icon is separated from actual count");
        QImage blank(100,30,QImage::Format_RGB32);blank.fill(Qt::white);
        result=st::filterUiIcons(blank,{{"01218",{10,5,45,15},15}});
        check(result.size()==1&&result[0].text=="01218","leading zeros are not stripped from numeric text alone");
    }
    {
        QImage button(160,45,QImage::Format_RGB32);button.fill(QColor(31,136,61));
        {QPainter p(&button);p.setPen(QPen(Qt::white,2));p.drawLine(17,12,10,19);p.drawLine(10,19,17,26);
            p.drawLine(24,12,31,19);p.drawLine(31,19,24,26);QFont font("Segoe UI");font.setPixelSize(18);p.setFont(font);p.drawText(43,26,"Code");}
        const auto result=st::filterUiIcons(button,{{"<",{9,11,9,16},16},{"〉",{23,11,9,16},16},{"Code",{43,12,42,16},16}});
        check(result.size()==1&&result.front().text=="Code","paired button chevrons remain graphical decorations");
    }
}
}
int main(int argc,char** argv)
{
    QGuiApplication app(argc,argv);
    try{
        auto args=app.arguments();
        if(args.contains("--ocr-suite")){
            auto out=args.value(args.indexOf("--ocr-suite")+1);QDir().mkpath(out);
            QJsonArray reports;
            auto run=[&](QString name,QImage image,QString expected,QPointF pointer=QPointF(-1,-1)){
                image.save(out+"/"+name+".png");QList<st::Block> words;st::Block pointed;std::exception_ptr error;
                QElapsedTimer clock;clock.start();
                std::thread worker([&]{try{
                    words=st::recognize(image,std::make_shared<std::atomic_bool>(false),true);
                    if(pointer.x()>=0)pointed=st::recognizeAt(image,std::make_shared<std::atomic_bool>(false),pointer);
                }catch(...){error=std::current_exception();}});worker.join();if(error)std::rethrow_exception(error);
                QStringList text;bool geometry=true;for(auto word:words){text<<word.text;geometry=geometry&&QRectF(image.rect()).contains(word.box);}
                double inkCoverage=-1;
                if(name.startsWith("rotated")){
                    int ink=0,covered=0;
                    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qGray(image.pixel(x,y))<128){
                        ++ink;for(const auto& word:words)if(word.box.adjusted(-1,-1,1,1).contains(QPointF(x,y))){++covered;break;}
                    }
                    inkCoverage=ink?double(covered)/ink:0;
                    if(inkCoverage<.85||text.join(" ")!=expected)throw std::runtime_error("rotated OCR geometry or reading order");
                }
                if(name=="wide-tile-boundary"&&text.join(" ").count("BoundaryText1234567890")!=1)
                    throw std::runtime_error("wide tile boundary loses or repeats identifier");
                if(!geometry)throw std::runtime_error("OCR box outside source image");
                reports.append(QJsonObject{{"name",name},{"expected",expected},{"actual",text.join(QChar(10))},{"pointer",pointed.text},{"ms",clock.elapsed()},{"geometry",geometry},{"inkCoverage",inkCoverage}});
            };
            for(auto text:{QString("Please translate this sentence accurately."),QString("这是中文说明，hello world 是需要翻译的英文。")}){
                bool chinese=text.startsWith("这");
                for(int size:{11,14,18})for(int contrast:{0,1,2}){
                    QFont font(chinese?"Microsoft YaHei UI":"Segoe UI");font.setPixelSize(size);QFontMetrics metrics(font);
                    QImage image(metrics.horizontalAdvance(text)+36,metrics.height()+32,QImage::Format_RGB32);
                    image.fill(contrast==1?QColor(32,32,32):Qt::white);
                    QPainter painter(&image);painter.setFont(font);painter.setPen(contrast==0?QColor(30,30,30):contrast==1?QColor(170,170,170):QColor(205,205,205));
                    painter.drawText(16,16+metrics.ascent(),text);painter.end();
                    run(QString("%1-%2-%3").arg(chinese?"mixed":"latin").arg(size).arg(contrast),image,text,QPointF(22,16+metrics.ascent()/2.));
                }
            }
            for(int margin:{0,2,12}){
                QString text="Screenshot translation 2026";QFont font("Segoe UI");font.setPixelSize(13);QFontMetrics metrics(font);
                QImage image(metrics.horizontalAdvance(text)+margin*2,metrics.height()+margin*2,QImage::Format_RGB32);image.fill(Qt::white);
                QPainter p(&image);p.setFont(font);p.setPen(Qt::black);p.drawText(margin,margin+metrics.ascent(),text);p.end();
                run(QString("edge-%1").arg(margin),image,text);
            }
            QImage sparse(1200,800,QImage::Format_RGB32);sparse.fill(QColor(30,30,30));
            {QPainter p(&sparse);QFont f("Segoe UI");f.setPixelSize(11);p.setFont(f);p.setPen(QColor(100,100,100));p.drawText(30,40,"Sparse small text");}
            run("sparse-dark",sparse,"Sparse small text");
            QImage boundary(3300,100,QImage::Format_RGB32);boundary.fill(Qt::white);
            {QPainter p(&boundary);QFont f("Segoe UI");f.setPixelSize(15);p.setFont(f);p.setPen(Qt::black);p.drawText(1440,40,"LongIdentifierCrossingBoundary1234567890");}
            run("tile-boundary",boundary,"LongIdentifierCrossingBoundary1234567890");
            QImage tiled(3300,100,QImage::Format_RGB32);tiled.fill(Qt::white);
            {QPainter p(&tiled);QFont f("Segoe UI");f.setPixelSize(18);p.setFont(f);p.setPen(Qt::black);
                p.drawText(16,40,"Start");p.drawText(1120,40,"BoundaryText1234567890");p.drawText(3120,40,"Finish");}
            run("wide-tile-boundary",tiled,"Start BoundaryText1234567890 Finish");
            QImage blank(500,120,QImage::Format_RGB32);blank.fill(QColor(65,65,65));run("blank-dark",blank,"");
            for(int size:{12,17,24}){
                QImage document(720,220,QImage::Format_RGB32);document.fill(Qt::white);
                QPainter p(&document);QFont f("Segoe UI");f.setPixelSize(size);p.setFont(f);p.setPen(Qt::black);
                p.drawText(20,45,"Read the first row.");
                p.drawText(20,85,"Keep the next row.");
                p.drawText(400,45,"Separate column.");
                p.drawText(400,85,"Another label.");p.end();
                run(QString("columns-%1").arg(size),document,"Read the first row.\nSeparate column.\nKeep the next row.\nAnother label.");
            }
            for(int degrees:{-5,5}){
                QImage rotated(600,130,QImage::Format_RGB32);rotated.fill(Qt::white);
                QPainter p(&rotated);QFont f("Segoe UI");f.setPixelSize(24);p.setFont(f);p.setPen(Qt::black);
                p.translate(300,65);p.rotate(degrees);p.drawText(-250,8,"Rotate text and retain its position.");p.end();
                run(QString("rotated-%1").arg(degrees),rotated,"Rotate text and retain its position.");
            }
            for(int size:{12,16}){
                QImage label(1000,500,QImage::Format_RGB32);label.fill(QColor(28,28,28));
                QPainter p(&label);QFont f("Segoe UI");f.setPixelSize(size);p.setFont(f);p.setPen(QColor(110,110,110));
                p.drawText(120,130,"Settings: 12345 / Version 2.0");p.end();
                run(QString("sparse-label-%1").arg(size),label,"Settings: 12345 / Version 2.0");
            }
            QImage white(600,400,QImage::Format_RGB32);white.fill(Qt::white);run("blank-light",white,"");
            std::puts(QJsonDocument(QJsonObject{{"cases",reports}}).toJson(QJsonDocument::Compact).constData());return 0;
        }
        if(args.contains("--speed")){
            QTemporaryDir data;st::Services service(data.path());
            auto base=args.value(args.indexOf("--speed")+1);
            auto fresh=[] {return std::make_shared<std::atomic_bool>(false);};
            service.save({{"zvo",QJsonObject{{"endpoint",base+"/zvo-speed"}}}});
            QJsonObject settings{{"provider","zvo"},{"fallbackEnabled",false}};
            QStringList sentences;for(int i=0;i<160;++i)sentences<<QString("Sentence %1 remains intact.").arg(i);
            QElapsedTimer timer;timer.start();qint64 first=-1;
            auto result=service.translate(settings,sentences,"en","zh",fresh(),{},[&](const st::Translation&,int,int){if(first<0)first=timer.elapsed();});
            auto elapsed=timer.elapsed();timer.restart();
            auto repeated=service.translate(settings,sentences,"en","zh",fresh());auto repeat=timer.elapsed();
            if(result.segments!=repeated.segments||!result.segments.last().contains("159"))throw std::runtime_error("benchmark result ordering");
            auto google=service.profile("google");google["endpoint"]=base+"/google-speed";
            timer.restart();auto paragraph=service.translate({}, {sentences.join(" ")},"en","zh",fresh(),google);
            if(!paragraph.segments.first().contains("159"))throw std::runtime_error("paragraph tail lost");
            std::puts(QJsonDocument(QJsonObject{{"passed",true},{"sentences",160},{"firstMs",first},{"totalMs",elapsed},
                {"repeatMs",repeat},{"paragraphMs",timer.elapsed()}}).toJson(QJsonDocument::Compact).constData());return 0;
        }
        if(args.contains("--ocr-image")){
            QElapsedTimer ocrTimer;ocrTimer.start();
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
            const auto rawMs=ocrTimer.elapsed();ocrTimer.restart();
            QList<st::Block> blocks;
            std::thread groupedReader([&]{try{blocks=st::recognize(image,std::make_shared<std::atomic_bool>(false),true,
                args.contains("--ocr-scale")?args.value(args.indexOf("--ocr-scale")+1).toDouble():2.);}catch(...){error=std::current_exception();}});
            groupedReader.join();if(error)std::rethrow_exception(error);const auto groupedMs=ocrTimer.elapsed();QJsonArray grouped;
            for(const auto& block:blocks)grouped.append(QJsonObject{{"text",block.text},{"fontPixels",block.fontPixels},
                {"box",QJsonArray{block.box.x(),block.box.y(),block.box.width(),block.box.height()}}});
            if(args.contains("--ocr-preview")){
                const auto path=args.value(args.indexOf("--ocr-preview")+1);
                QFile translations(path+".json");QStringList segments;
                if(translations.open(QIODevice::ReadOnly)){
                    const auto mapping=QJsonDocument::fromJson(translations.readAll()).object();
                    for(const auto& block:blocks)segments<<mapping.value(block.text).toString(block.text);
                }else for(const auto& block:blocks)segments<<block.text;
                st::translatedImage(image,blocks,segments,false).save(path+".png");
            }
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
            std::puts(QJsonDocument(QJsonObject{{"words",found},{"blocks",grouped},{"pointed",pointed},{"rawMs",rawMs},{"groupedMs",groupedMs}}).toJson(QJsonDocument::Compact).constData());return 0;
        }
        if(args.contains("--dictionary-download")){
            QTemporaryDir data;
            st::downloadDictionary(data.path(),{},std::make_shared<std::atomic_bool>(false));
            if(st::dictionaryEntry("hello",data.path()).isEmpty())throw std::runtime_error("downloaded dictionary lookup");
            std::puts("{\"passed\":true,\"githubDictionaryDownload\":true,\"sha256Verified\":true,\"offlineLookup\":true}");return 0;
        }
        checkPhrases();
        if(st::canonicalProvider("")!="ecdict"||st::canonicalProvider("mymemory")!="mymemory")
            throw std::runtime_error("ECDICT is the missing-setting default; explicit provider selection is preserved");
        checkWrappedPhrases();
        checkScreenshotLayout();
        if(st::detect("你好世界")!="zh"||st::targetFor("Hello","auto")!="zh")throw std::runtime_error("language");
        if(st::selectionDefaultLanguage("auto","en","zh")!="en"||st::selectionDefaultLanguage("auto","auto","zh")!="zh"||
            st::selectionDefaultLanguage("auto","auto","en-US")!="en"||st::selectionDefaultLanguage("ja","en","zh")!="ja")
            throw std::runtime_error("selection default language follows explicit override, target, then interface");
        for(const auto& test:QList<QPair<QString,QString>>{{"这是一段中文说明。","zh"},{"這是一段繁體中文說明。","zh"},
            {"Please translate this sentence accurately.","en"},{"Ceci est un texte écrit en français pour vérifier la langue.","fr"},
            {"Dies ist ein deutscher Text zum Testen der Spracherkennung.","de"},{"Este es un texto en español para comprobar el idioma.","es"},
            {"これは日本語の文章です。","ja"},{"이것은 한국어 문장입니다.","ko"},{"Это предложение написано на русском языке.","ru"}}){
            if(st::selectionIconAllowed(test.first,test.second)||!st::selectionIconAllowed(test.first,test.second=="zh"?"en":"zh"))
                throw std::runtime_error(("selection language filtering: "+test.second).toStdString());
        }
        if(st::selectionIconAllowed("12345 … !? 😀","zh")||st::selectionIconAllowed("  ","zh",false)||
            !st::selectionIconAllowed("中文","zh",false)||!st::selectionIconAllowed("مرحبا بالعالم","en")||
            st::selectionIconAllowed(QString(200000,QChar(0x4e2d)),"zh"))throw std::runtime_error("neutral, all-language and long selection filtering");
        if(st::selectionIconAllowed("这是一段中文说明，hello world 是其中的英文词语，请按主要语种判断。","zh")||
            st::selectionIconAllowed("中文：This is an English paragraph explaining how the selection language preference works.","en"))
            throw std::runtime_error("mixed selections follow their main language");
        {
            QTemporaryDir settingsDir;st::Services settings(settingsDir.path());
            if(!settings.routing().value("selectionIconForeignOnly").toBool()||settings.routing().value("selectionIconDefaultLanguage")!="auto")
                throw std::runtime_error("fresh settings default to foreign selection only");
            settings.save({},{{"selectionIconForeignOnly",false},{"selectionIconDefaultLanguage","en"}});
            st::Services reopenedSettings(settingsDir.path());
            if(reopenedSettings.routing().value("selectionIconForeignOnly").toBool(true)||reopenedSettings.routing().value("selectionIconDefaultLanguage")!="en")
                throw std::runtime_error("selection language preferences survive restart");
            reopenedSettings.save({},{{"selectionIconDefaultLanguage","invalid"}});
            if(reopenedSettings.routing().value("selectionIconDefaultLanguage")!="auto")throw std::runtime_error("invalid default language is normalized");
        }
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
            {
                QTemporaryDir hostDir;st::Services host(hostDir.path());
                host.save({{"google",QJsonObject{{"endpoint",base+"/google"}}}});
                QJsonObject options{{"provider","google"},{"fallbackEnabled",false}};
                const QString input="\nHello + 100%\r\n\r\nSettings\n";
                auto result=host.translateLines(options,input,freshCancel());
                if(result["translated"].toString()!="\n你好 + 100%\n\n设置\n")throw std::runtime_error("host preserves empty lines and line positions");
                auto longLine=QString("Long source sentence. ").repeated(160)+"TAIL";
                result=host.translateLines(options,longLine+"\nHistory",freshCancel());
                auto output=result["translated"].toString().split('\n');
                if(output.size()!=2||!output[0].contains("TAIL")||output[1]!="历史")throw std::runtime_error("host long text has no UI detour or extra line");
                auto stopped=freshCancel();stopped->store(true);bool rejected=false;
                try{host.translateLines(options,"cancel",stopped);}catch(...){rejected=true;}
                if(!rejected)throw std::runtime_error("host cancellation");
            }
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
            QImage tall(480,3700,QImage::Format_RGB32);tall.fill(QColor(35,35,35));
            {QPainter painter(&tall);QFont font("Arial",18);painter.setFont(font);painter.setPen(QColor(190,190,190));
                painter.drawText(30,50,"First sentence");painter.drawText(30,1620,"Middle sentence");painter.drawText(30,3650,"Final sentence");}
            QList<st::Block> tallWords;std::exception_ptr tallError;
            std::thread tallReader([&]{try{tallWords=st::recognize(tall,freshCancel(),false);}catch(...){tallError=std::current_exception();}});tallReader.join();
            if(tallError)std::rethrow_exception(tallError);
            QString tallText;int finals=0;for(const auto& word:tallWords){tallText+=word.text+" ";if(word.text.contains("Final",Qt::CaseInsensitive))++finals;
                if(!QRectF(tall.rect()).contains(word.box))throw std::runtime_error("tile OCR geometry outside image");}
            if(!tallText.contains("First",Qt::CaseInsensitive)||!tallText.contains("Middle",Qt::CaseInsensitive)||finals!=1)
                throw std::runtime_error(("dark tall OCR retains all tiles without duplicates: "+tallText).toStdString());
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
            QTemporaryDir dictionaryDir;st::Services dictionary(dictionaryDir.path());
            dictionary.save({{"zvo",QJsonObject{{"endpoint",base+"/zvo"}}}});
            if(st::dictionaryAvailable(dictionaryDir.path()))throw std::runtime_error("fresh package must not bundle the dictionary");
            auto noDictionary=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",true}},{"hello"},"en","zh",freshCancel());
            if(noDictionary.provider!="zvo"||dictionary.configurationProblem(dictionary.profile("ecdict")).isEmpty())throw std::runtime_error("undownloaded dictionary is unconfigured, not failed");
            auto source=st::dictionarySource();source.url=base+"/dictionary-redirect";
            st::downloadDictionary(dictionaryDir.path(),{},freshCancel(),{},source);
            if(st::dictionaryEntry("HELLO",dictionaryDir.path()).isEmpty()||st::dictionaryEntry("baby",dictionaryDir.path()).isEmpty()||!st::dictionaryEntry("zznotaword7789",dictionaryDir.path()).isEmpty())throw std::runtime_error("downloaded ECDICT lookup");
            auto preserved=st::dictionaryEntry("hello",dictionaryDir.path());
            auto bad=source;bad.sha256=QString(64,'0');bool rejected=false;
            try{st::downloadDictionary(dictionaryDir.path(),{},freshCancel(),{},bad);}catch(...){rejected=true;}
            if(!rejected||st::dictionaryEntry("hello",dictionaryDir.path())!=preserved)throw std::runtime_error("bad hash preserves installed dictionary");
            auto stopped=freshCancel();auto slow=source;slow.url=base+"/dictionary-slow";
            rejected=false;try{st::downloadDictionary(dictionaryDir.path(),{},stopped,[&](qint64,qint64){stopped->store(true);},slow);}catch(...){rejected=true;}
            if(!rejected||st::dictionaryEntry("hello",dictionaryDir.path())!=preserved)throw std::runtime_error("cancel preserves installed dictionary");
            // Reopen the same persistent directory, not a process-only cache.
            st::Services installedAgain(dictionaryDir.path());
            if(!installedAgain.configurationProblem(installedAgain.profile("ecdict")).isEmpty())throw std::runtime_error("download survives service restart");
            auto missing=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",true}},{"zznotawordtest"},"en","zh",freshCancel());
            auto known=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",true}},{"hello"},"en","zh",freshCancel());
            if(missing.provider!="zvo"||known.provider!="ecdict"||known.fallback)throw std::runtime_error("dictionary misses must not quarantine lookup");
            auto offline=dictionary.translate({{"provider","ecdict"},{"fallbackEnabled",false},{"proxy","invalid proxy"}},{"hello"},"en","zh",freshCancel());
            if(offline.provider!="ecdict")throw std::runtime_error("offline dictionary must not depend on proxy configuration");
            auto tokens=st::dictionaryWords(" Hello, HELLO! can't well-known café abc123 你好 hello.");
            if(tokens.size()!=3||tokens[0].toObject()["count"].toInt()!=3||tokens[0].toObject()["start"].toInt()!=1||tokens[1].toObject()["key"]!="can't"||tokens[2].toObject()["key"]!="well-known")throw std::runtime_error("English token boundaries, contraction, order and duplicates");
            if(!st::dictionaryWords("你好 café abc123").isEmpty())throw std::runtime_error("non English text does not create partial words");
            if(st::dictionaryDetails("walk",dictionaryDir.path()).value("forms").toString().isEmpty())throw std::runtime_error("dictionary word forms");
            dictionary.save({{"youdao",QJsonObject{{"endpoint",base+"/word-dictionary"}}}});
            auto chineseCandidates=dictionary.lookupWords({},"玩具",freshCancel());auto chineseRows=chineseCandidates["rows"].toArray();
            if(chineseCandidates["direction"]!="zh-en"||chineseRows.size()<3||chineseRows[0].toObject()["word"]!="toy"||chineseRows[1].toObject()["word"]!="plaything"||chineseRows[0].toObject()["meaning"].toString().isEmpty())throw std::runtime_error("Chinese dictionary provides ordered multiple English alternatives");
            auto phraseCandidates=dictionary.lookupWords({},"银行",freshCancel())["rows"].toArray();
            if(phraseCandidates.isEmpty()||phraseCandidates[0].toObject()["word"]!="banking house")throw std::runtime_error("Youdao Chinese multi-word candidates stay joined");
            auto englishRelated=dictionary.lookupWords({},"beautiful",freshCancel())["related"].toArray();
            if(englishRelated.size()!=2||englishRelated[0].toObject()["kind"]!="synonym"||englishRelated[0].toObject()["words"].toArray().size()!=2||englishRelated[1].toObject()["kind"]!="derived")throw std::runtime_error("synonyms grouped by sense and separated from related forms");
            bool localFirst=false;
            auto perWord=dictionary.lookupWords({},"Hello HELLO, novelword unknownword.",freshCancel(),[&](QJsonObject value){
                if(value["found"].toInt()==1&&value["rows"].toArray().size()==3)localFirst=true;
            });
            auto wordRows=perWord["rows"].toArray();auto onlineWord=wordRows[1].toObject();
            if(!localFirst||perWord["found"].toInt()!=2||wordRows[0].toObject()["count"].toInt()!=2||onlineWord["provider"]!="有道"||!onlineWord["pronunciation"].toString().contains("美")||onlineWord["meaning"]!="n. 新词；<例>"||onlineWord["forms"]!="复数 novelwords"||wordRows[2].toObject()["status"]!="missing")throw std::runtime_error("local first progressive word lookup with Youdao fallback");
            dictionary.save({{"youdao",QJsonObject{{"enabled",false}}}});
            auto reverseOffline=dictionary.lookupWords({{"proxy","invalid proxy"}},"玩具",freshCancel())["rows"].toArray();QStringList reverseKeys;
            for(auto value:reverseOffline)reverseKeys<<value.toObject()["word"].toString();
            if(!reverseKeys.contains("toy")||!reverseKeys.contains("plaything")||reverseKeys.contains("toys")||reverseOffline[0].toObject()["relation"]!="汉英候选")throw std::runtime_error("offline reverse lookup ranks matching base words first");
            auto offlineWords=dictionary.lookupWords({{"proxy","invalid proxy"}},"hello unknownword",freshCancel());
            if(offlineWords["found"].toInt()!=1||offlineWords["rows"].toArray()[1].toObject()["status"]!="missing")throw std::runtime_error("disabled Youdao not requested, offline independent of proxy");
            dictionary.save({{"ecdict",QJsonObject{{"enabled",false}}}});
            if(dictionary.lookupWords({},"hello",freshCancel())["found"].toInt()!=0)throw std::runtime_error("disabled offline dictionary not consulted");
            dictionary.save({{"ecdict",QJsonObject{{"enabled",true}}},{"youdao",QJsonObject{{"enabled",true},{"endpoint",base+"/word-dictionary"}}}});
            auto cancelWords=freshCancel();bool wordsCancelled=false;
            try{dictionary.lookupWords({},"hello novelword",cancelWords,[&](QJsonObject){cancelWords->store(true);});}catch(...){wordsCancelled=true;}
            if(!wordsCancelled)throw std::runtime_error("word lookup cancellation stops pending requests");
            if(dictionary.lookupWords({},"こんにちは",freshCancel())["message"].toString().isEmpty())throw std::runtime_error("word lookup empty language guidance");
            auto reverseCancel=freshCancel();bool reverseStopped=false;
            try{dictionary.lookupWords({},"玩具",reverseCancel,[&](QJsonObject){reverseCancel->store(true);});}catch(...){reverseStopped=true;}
            if(!reverseStopped)throw std::runtime_error("Chinese candidate cancellation stops network request");
            QStringList longWords;for(int i=0;i<65;++i)longWords<<"hello"+QString(QChar('a'+i/26))+QChar('a'+i%26);
            dictionary.save({{"youdao",QJsonObject{{"enabled",false}}}});
            auto capped=dictionary.lookupWords({},longWords.join(' '),freshCancel());
            if(!capped["truncated"].toBool()||capped["rows"].toArray().size()!=60||capped["total"].toInt()!=65)throw std::runtime_error("word lookup explicit bounded results");
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
            QString longText;for(int i=0;i<600;++i)longText+=QString("Sentence %1 stays complete. ").arg(i);
            auto split=st::sentenceParts(longText);if(split.size()!=600||split.last()!="Sentence 599 stays complete.")throw std::runtime_error("sentence segmentation and tail preservation");
            int updates=0;auto longDraft=services.profile("zvo");longDraft["endpoint"]=base+"/zvo";
            auto longResult=services.translate({}, {longText},"en","zh",freshCancel(),longDraft,[&](const st::Translation& partial,int done,int total){
                if(done<=0||done>total||partial.segments.size()!=1)throw std::runtime_error("long text progress");++updates;
            });
            if(updates<2||!longResult.segments[0].contains("Sentence 599 stays complete."))throw std::runtime_error("unlimited document complete");
            auto emoji=QString::fromUtf8("😀").repeated(1300);auto unicodeParts=st::sentenceParts(emoji);
            if(unicodeParts.join("")!=emoji)throw std::runtime_error("sentence split must preserve surrogate pairs");
            auto stopLong=freshCancel();bool longCancelled=false;int beforeCancel=0;
            try{services.translate({}, {longText},"en","zh",stopLong,longDraft,[&](const st::Translation&,int,int){++beforeCancel;stopLong->store(true);});}catch(...){longCancelled=true;}
            if(!longCancelled||beforeCancel!=1)throw std::runtime_error("cancel long document between batches");
            // Cache identity follows provider, configuration, languages and purpose.
            QTemporaryDir cacheDir;st::Services cachedService(cacheDir.path());
            cachedService.save({{"zvo",QJsonObject{{"endpoint",base+"/zvo-cache"}}}});
            QJsonObject cacheSettings{{"provider","zvo"},{"fallbackEnabled",false}};
            auto cacheFirst=cachedService.translate(cacheSettings,{"cache text"},"en","zh",freshCancel());
            auto cacheSecond=cachedService.translate(cacheSettings,{"cache text"},"en","zh",freshCancel());
            if(cacheFirst.segments!=cacheSecond.segments)throw std::runtime_error("cache content mismatch");
            cachedService.translate(cacheSettings,{"cache text"},"en","fr",freshCancel());
            cachedService.translate(cacheSettings,{"cache text"},"de","zh",freshCancel());
            auto cacheProbe=cachedService.profile("zvo");
            cachedService.translate(cacheSettings,{"cache text"},"en","zh",freshCancel(),cacheProbe);
            auto stopCache=freshCancel();stopCache->store(true);bool cacheAborted=false;
            try{cachedService.translate(cacheSettings,{"cache text"},"en","zh",stopCache);}catch(...){cacheAborted=true;}
            if(!cacheAborted)throw std::runtime_error("cache hit ignores cancellation");
            cachedService.save({{"zvo",QJsonObject{{"endpoint",base+"/zvo-cache-changed"}}}});
            cachedService.translate(cacheSettings,{"cache text"},"en","zh",freshCancel());
            // The healthy first batch publishes before blocked workers; cancellation joins them promptly.
            QTemporaryDir concurrentDir;st::Services concurrent(concurrentDir.path());
            concurrent.save({{"zvo",QJsonObject{{"endpoint",base+"/zvo-cancel-speed"}}}});
            QStringList parallelText;for(int i=0;i<80;++i)parallelText<<QString("Part %1 stays in order.").arg(i);
            auto cancelParallel=freshCancel();QElapsedTimer parallelClock;parallelClock.start();
            std::thread cancelWorker([cancelParallel]{QThread::msleep(300);cancelParallel->store(true);});
            bool parallelAborted=false;int published=0;
            try{concurrent.translate(cacheSettings,parallelText,"en","zh",cancelParallel,{},[&](const st::Translation&,int,int){++published;});}catch(...){parallelAborted=true;}
            cancelWorker.join();
            if(!parallelAborted||published!=1||parallelClock.elapsed()>1500)throw std::runtime_error("parallel cancellation joins workers and preserves first batch");
            // Any failed later batch must preserve only the already published ordered prefix.
            concurrent.save({{"zvo",QJsonObject{{"endpoint",base+"/zvo-error-speed"}}}});
            bool partialFailed=false;int donePrefix=0;
            try{concurrent.translate(cacheSettings,parallelText,"en","zh",freshCancel(),{},[&](const st::Translation& result,int done,int){
                donePrefix=done;if(!result.segments[done-1].contains(QString::number(done-1)))throw std::runtime_error("partial ordering");
            });}catch(...){partialFailed=true;}
            if(!partialFailed||donePrefix!=16)throw std::runtime_error("failed later batch preserves ordered prefix");
            // A rate-limited Google endpoint must receive one attempt, not every client variation.
            auto rateGoogle=services.profile("google");rateGoogle["endpoint"]=base+"/google-rate-speed";
            bool rateFailed=false;try{services.translate({}, {"rate limit text"},"en","zh",freshCancel(),rateGoogle);}catch(...){rateFailed=true;}
            if(!rateFailed)throw std::runtime_error("rate limit unexpectedly succeeds");
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
            // Unconfigured keys in the primary and backup must be skipped, not
            // reported as the final failure when another service was attempted.
            QTemporaryDir keyFallbackDir;st::Services keyFallback(keyFallbackDir.path());
            QJsonObject keyProfiles;
            for(const auto& provider:st::providers)keyProfiles[provider]=QJsonObject{{"enabled",false}};
            keyProfiles["deepl"]=QJsonObject{{"enabled",true}};
            keyProfiles["microsoft"]=QJsonObject{{"enabled",true}};
            keyProfiles["zvo"]=QJsonObject{{"endpoint",base+"/zvo"}};
            QJsonObject keyRouting{{"mode","manual"},{"order",QJsonArray{"zvo","microsoft"}}};
            keyFallback.save(keyProfiles,keyRouting);
            QJsonObject keySettings{{"provider","deepl"},{"fallbackEnabled",true}};
            auto keyBackup=keyFallback.translate(keySettings,{"Hello + 100%"},"en","zh",fresh());
            if(!keyBackup.fallback||keyBackup.provider!="zvo")throw std::runtime_error("unconfigured primary does not block configured fallback");
            keyProfiles["zvo"]=QJsonObject{{"endpoint",base+"/queue-fail"}};
            keyFallback.save({{"zvo",keyProfiles["zvo"].toObject()}},keyRouting);
            auto keyError=[&]{
                try{keyFallback.translate(keySettings,{"hello"},"en","zh",fresh());}
                catch(const std::exception& e){return QString::fromUtf8(e.what());}
                return QString();
            };
            if(!keyError().contains("HTTP 503"))throw std::runtime_error("unconfigured backup does not mask request error");
            if(!keyError().contains("冷却"))throw std::runtime_error("unconfigured backup does not mask cooldown");
            keySettings["fallbackEnabled"]=false;
            if(!keyError().contains("未配置 API 密钥"))throw std::runtime_error("unconfigured primary is explicit when fallback is off");
            // Exercise the actual routing path, not just the displayed order.
            QTemporaryDir queueDir;st::Services queued(queueDir.path());
            QJsonObject manual{{"mode","manual"},{"order",QJsonArray{"google","zvo","ecdict","google","unknown"}}};
            queued.save({{"mymemory",QJsonObject{{"endpoint",base+"/queue-fail"}}},
                {"google",QJsonObject{{"endpoint",base+"/google-slow"}}},{"zvo",QJsonObject{{"endpoint",base+"/zvo"}}}},manual);
            if(queued.routing().value("order").toArray().size()!=st::providers.size())throw std::runtime_error("queue validates and completes provider IDs");
            auto ordered=queued.translate(settings,{"Hello + 100%"},"en","zh",fresh());
            if(ordered.provider!="google")throw std::runtime_error("manual queue takes first backup");
            queued.translate({}, {"hello"},"en","zh",fresh(),queued.profile("zvo"));
            auto automatic=queued.routing();automatic["mode"]="auto";queued.save({},automatic);
            auto ranked=queued.queue(settings);
            if(ranked[0].toObject().value("provider")!="mymemory"||ranked[1].toObject().value("provider")!="zvo")throw std::runtime_error("auto queue respects preferred and latency");
            if(queued.translate(settings,{"Hello + 100%"},"en","zh",fresh()).provider!="zvo")throw std::runtime_error("automatic weights drive fallback");
            automatic["adjustments"]=QJsonObject{{"google",1000}};queued.save({},automatic);
            auto boosted=queued.queue(settings);
            if(boosted[0].toObject().value("provider")!="mymemory"||boosted[1].toObject().value("provider")!="google"||
                boosted[1].toObject().value("effectiveWeight").toInt()<=boosted[2].toObject().value("effectiveWeight").toInt())
                throw std::runtime_error("automatic user adjustment changes backup ranking but keeps primary first");
            if(queued.translate(settings,{"Hello + 100%"},"en","zh",fresh()).provider!="google")throw std::runtime_error("automatic drag weight drives actual fallback");
            st::Services queueReopened(queueDir.path());
            if(queueReopened.routing().value("mode")!="auto"||queueReopened.routing().value("adjustments").toObject().value("google").toInt()!=1000||
                queueReopened.translate(settings,{"hello"},"en","zh",fresh()).provider!="google")throw std::runtime_error("weights, manual adjustments and queue survive restart");
            queueReopened.save({},manual);
            if(queueReopened.translate(settings,{"hello"},"en","zh",fresh()).provider!="google")throw std::runtime_error("manual order ignores automatic score");
            auto queueEntry=[](const QJsonArray& values,const QString& provider){for(auto v:values)if(v.toObject().value("provider")==provider)return v.toObject();return QJsonObject();};
            // A temporary outage must recover without removing health files or
            // requiring a settings-page test, even with dictionary-only backups.
            QTemporaryDir rescueDir;st::Services rescue(rescueDir.path());QJsonObject rescueProfiles;
            for(const auto& provider:st::providers)rescueProfiles[provider]=QJsonObject{{"enabled",false}};
            rescueProfiles["zvo"]=QJsonObject{{"enabled",true},{"endpoint",base+"/zvo-emergency"}};
            rescueProfiles["youdao"]=QJsonObject{{"enabled",true},{"endpoint",base+"/youdao-dictionary"}};
            rescue.save(rescueProfiles);
            QJsonObject rescueSettings{{"provider","ecdict"},{"fallbackEnabled",true}};
            const QString rescueText="Please translate this complete sentence, even when the main service is down.";
            auto rescueError=[&](st::Services& service,const QJsonObject& config,const QString& text){
                try{service.translate(config,{text},"en","zh",fresh());}catch(const std::exception& e){return QString::fromUtf8(e.what());}
                return QString();
            };
            if(!rescueError(rescue,rescueSettings,rescueText).contains("HTTP 503")||
               !rescueError(rescue,rescueSettings,rescueText).contains("冷却"))throw std::runtime_error("temporary outage starts with bounded cooldown");
            auto ageFailures=[&](const QString& folder){
                QFile file(folder+"/service-health.json");if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("health fixture missing");
                auto doc=QJsonDocument::fromJson(file.readAll()).object();file.close();auto states=doc["services"].toObject();
                for(auto it=states.begin();it!=states.end();++it){auto h=it.value().toObject();
                    if(h["failedAt"].toDouble()>0){h["failedAt"]=double(QDateTime::currentSecsSinceEpoch()-600);h.remove("probeAfter");it.value()=h;}}
                st::atomicJson(folder+"/service-health.json",{{"version",2},{"services",states}});
            };
            ageFailures(rescueDir.path());st::Services noFallback(rescueDir.path());
            auto offSettings=rescueSettings;offSettings["provider"]="zvo";offSettings["fallbackEnabled"]=false;
            if(!rescueError(noFallback,offSettings,rescueText).contains("冷却"))throw std::runtime_error("recovery honors fallback disabled");
            st::Services restored(rescueDir.path());
            auto rescued=restored.translate(rescueSettings,{rescueText},"en","zh",fresh());
            if(rescued.provider!="zvo"||!rescued.fallback||queueEntry(restored.queue(rescueSettings),"zvo")["cooling"].toBool())
                throw std::runtime_error("all unavailable routes recover via one eligible probe");
            // A failure for new text must not discard usable cached translations.
            rescueError(restored,rescueSettings,"fail after cache!");
            if(restored.translate(rescueSettings,{rescueText},"en","zh",fresh()).segments!=rescued.segments)
                throw std::runtime_error("cache remains usable during an outage");
            for(const auto& path:{"/mymemory-temporary","/mymemory-quota","/auth-fail"}){
                QTemporaryDir failureDir;st::Services failure(failureDir.path());auto profiles=rescueProfiles;
                profiles["zvo"]=QJsonObject{{"enabled",false}};profiles["youdao"]=QJsonObject{{"enabled",false}};
                profiles["mymemory"]=QJsonObject{{"enabled",true},{"endpoint",base+path}};failure.save(profiles);
                rescueError(failure,rescueSettings,rescueText);
                auto health=queueEntry(failure.queue(rescueSettings),"mymemory");
                bool temporary=QString(path)=="/mymemory-temporary";
                if(health["recoveryProbe"].toBool()!=temporary||health["detail"].toString().isEmpty())
                    throw std::runtime_error("classify temporary failures separately from authentication and quotas");
                ageFailures(failureDir.path());st::Services reopenedFailure(failureDir.path());
                auto error=rescueError(reopenedFailure,rescueSettings,rescueText);
                if(temporary?!error.contains("HTTP 503"):!error.contains("冷却"))throw std::runtime_error("hard cooldown cannot be bypassed by automatic recovery");
                if(!rescueError(reopenedFailure,rescueSettings,rescueText).contains("冷却"))throw std::runtime_error("failed recovery probe backs off again");
            }
            QTemporaryDir wavesDir;st::Services waves(wavesDir.path());auto waveProfiles=rescueProfiles;
            waveProfiles["zvo"]=QJsonObject{{"enabled",false}};waveProfiles["youdao"]=QJsonObject{{"enabled",false}};
            waveProfiles["mymemory"]=QJsonObject{{"enabled",true},{"endpoint",base+"/mymemory-wave"}};waves.save(waveProfiles);
            QStringList waveText;for(int i=0;i<16;++i)waveText<<QString("Sentence %1 is part of a screenshot.").arg(i);
            auto waveResult=waves.translate(rescueSettings,waveText,"en","zh",fresh());
            if(waveResult.provider!="mymemory"||waveResult.segments.size()!=16||!waveResult.segments[15].contains("Sentence 15"))
                throw std::runtime_error("dictionary primary fallback grants per-text service enough waves");
            waves.save({{"zvo",QJsonObject{{"endpoint",base+"/queue-fail"},{"enabled",true}}}});
            try{waves.translate({}, {rescueText},"en","zh",fresh(),waves.profile("zvo"));}catch(...){}
            QFile rankedHealth(wavesDir.path()+"/service-health.json");rankedHealth.open(QIODevice::ReadOnly);
            auto rankedStates=QJsonDocument::fromJson(rankedHealth.readAll()).object()["services"].toObject();rankedHealth.close();
            for(auto it=rankedStates.begin();it!=rankedStates.end();++it){auto h=it.value().toObject();
                // A recovering established service retains its older mixed history.
                if(h["provider"].toString()=="mymemory"){h["samples"]=52;h["successRate"]=.57;h["latencyMs"]=2500;}
                if(h["failedAt"].toDouble()>0){h["failedAt"]=double(QDateTime::currentSecsSinceEpoch()-7200);h["retryAt"]=0;}it.value()=h;}
            st::atomicJson(wavesDir.path()+"/service-health.json",{{"version",2},{"services",rankedStates}});
            st::Services rankedRecovery(wavesDir.path());rankedRecovery.save({},{{"mode","auto"}});
            auto healthy=queueEntry(rankedRecovery.queue(rescueSettings),"mymemory"),retryable=queueEntry(rankedRecovery.queue(rescueSettings),"zvo");
            if(retryable["cooling"].toBool()||healthy["effectiveWeight"].toInt()<=retryable["effectiveWeight"].toInt())
                throw std::runtime_error("expired failure does not outrank a proven slower backup");
            auto client=waves.profile("google");client["enabled"]=true;client["endpoint"]=base+"/google-client";
            if(waves.translate({}, {rescueText},"en","zh",fresh(),client).segments.isEmpty())throw std::runtime_error("Google prefers working public client");
            auto beforeMiss=queueEntry(queueReopened.queue(settings),"ecdict");
            try{queueReopened.translate({}, {"zznotawordtest"},"en","zh",fresh(),queueReopened.profile("ecdict"));}catch(...){}
            auto afterMiss=queueEntry(queueReopened.queue(settings),"ecdict");
            if(beforeMiss.value("samples")!=afterMiss.value("samples")||afterMiss.value("cooling").toBool())throw std::runtime_error("dictionary miss doesn't reduce weight");
            auto cancelledProfile=queueReopened.profile("google");cancelledProfile["endpoint"]=base+"/blocked";
            auto stop=fresh();std::thread cancelQueue([stop]{QThread::msleep(70);stop->store(true);});
            try{queueReopened.translate({}, {"hello"},"en","zh",stop,cancelledProfile);}catch(...){}cancelQueue.join();
            auto cancelledState=queueEntry(queueReopened.queue(settings,{{"google",cancelledProfile}}),"google");
            if(cancelledState.value("samples").toInt()||cancelledState.value("cooling").toBool())throw std::runtime_error("cancel doesn't change health or weight");
            try{queueReopened.translate({}, {"hello"},"en","zh",fresh(),queueReopened.profile("ai"));}catch(...){}
            if(queueEntry(queueReopened.queue(settings),"ai").value("samples").toInt())throw std::runtime_error("unconfigured service doesn't reduce weight");
            auto changed=queueReopened.profile("mymemory");changed["endpoint"]=base+"/mymemory";
            auto reset=queueEntry(queueReopened.queue(settings,{{"mymemory",changed}}),"mymemory");
            if(reset.value("cooling").toBool()||reset.value("samples").toInt())throw std::runtime_error("changed configuration gets fresh health");
            // A manual diagnostic can bypass cooldown, and success restores it.
            auto failedProbe=queueReopened.profile("zvo");failedProbe["endpoint"]=base+"/zvo-recover";
            try{queueReopened.translate({}, {"hello"},"en","zh",fresh(),failedProbe);}catch(...){}
            if(!queueEntry(queueReopened.queue(settings,{{"zvo",failedProbe}}),"zvo").value("cooling").toBool())throw std::runtime_error("test failure enters cooldown");
            queueReopened.translate({}, {"hello"},"en","zh",fresh(),failedProbe);
            if(queueEntry(queueReopened.queue(settings,{{"zvo",failedProbe}}),"zvo").value("cooling").toBool())throw std::runtime_error("successful probe clears the same configuration cooldown");
            auto alwaysFail=queueReopened.profile("zvo");alwaysFail["endpoint"]=base+"/backoff-fail";
            try{queueReopened.translate({}, {"hello"},"en","zh",fresh(),alwaysFail);}catch(...){}
            auto firstWait=queueEntry(queueReopened.queue(settings,{{"zvo",alwaysFail}}),"zvo").value("retrySeconds").toInt();
            try{queueReopened.translate({}, {"hello"},"en","zh",fresh(),alwaysFail);}catch(...){}
            auto secondWait=queueEntry(queueReopened.queue(settings,{{"zvo",alwaysFail}}),"zvo").value("retrySeconds").toInt();
            if(firstWait<3595||secondWait<7195)throw std::runtime_error("persistent exponential outage backoff");
            for(auto path:{"/auth-fail","/rate-fail"}){
                auto denied=alwaysFail;denied["endpoint"]=base+path;
                try{queueReopened.translate({}, {"hello"},"en","zh",fresh(),denied);}catch(...){}
                int wait=queueEntry(queueReopened.queue(settings,{{"zvo",denied}}),"zvo").value("retrySeconds").toInt();
                if(QString(path)=="/auth-fail"?wait<86395:(wait<10||wait>15))throw std::runtime_error("authentication cooldown differs from temporary rate limiting");
            }
            // Real HTTP batches, concurrent reuse, cancellation and a shared
            // Retry-After pause. The Python server verifies exact request counts.
            QStringList batchText{"First <button>& line\nSecond line", "literal &lt; and <br data-st-lb=\"1\">"};
            for(const auto& provider:QStringList{"google","edge"}){
                QTemporaryDir dir;st::Services service(dir.path());
                service.save({{provider,QJsonObject{{"enabled",true},{"endpoint",base+"/batch-"+provider+(provider=="google"?"/translateHtml":"/translatetext")}}}});
                QJsonObject config{{"provider",provider},{"fallbackEnabled",false}};
                std::vector<std::future<st::Translation>> calls;
                for(int i=0;i<8;++i)calls.emplace_back(std::async(std::launch::async,[&]{return service.translate(config,batchText,"en","zh",fresh());}));
                for(auto& call:calls){auto result=call.get();
                    if(result.segments.size()!=2||!result.segments[0].contains("<button>& line\n")||
                        !result.segments[1].contains("&lt; and <br data-st-lb=\"1\">"))throw std::runtime_error("batch order, literal markup and newlines round-trip");}
                if(queueEntry(service.queue(config),provider)["samples"].toInt()!=1)throw std::runtime_error("concurrent identical translations share one request");
            }
            {
                QTemporaryDir dir;st::Services service(dir.path());
                service.save({{"google",QJsonObject{{"endpoint",base+"/batch-cancel/translateHtml"}}}});
                QJsonObject config{{"provider","google"},{"fallbackEnabled",false}};
                auto active=std::async(std::launch::async,[&]{return service.translate(config,{"first"},"en","zh",fresh());});
                QThread::msleep(80);auto cancel=fresh();
                auto waiting=std::async(std::launch::async,[&]{try{service.translate(config,{"second"},"en","zh",cancel);}catch(const std::exception& e){return QString::fromUtf8(e.what());}return QString();});
                QThread::msleep(40);cancel->store(true);
                if(waiting.get()!="cancelled")throw std::runtime_error("queued translation cancellation");active.get();
                if(queueEntry(service.queue(config),"google")["samples"].toInt()!=1)throw std::runtime_error("cancelled waiter never damages provider health");
            }
            for(const auto& path:QStringList{"/batch-rate/translatetext","/batch-rate-date/translatetext"}){
                QTemporaryDir dir;st::Services service(dir.path());
                service.save({{"edge",QJsonObject{{"endpoint",base+path}}}});
                QJsonObject config{{"provider","edge"},{"fallbackEnabled",false}};
                std::vector<std::future<QString>> calls;
                for(int i=0;i<8;++i)calls.emplace_back(std::async(std::launch::async,[&,i]{try{service.translate(config,{QString("text %1").arg(i)},"en","zh",fresh());}catch(const std::exception& e){return QString::fromUtf8(e.what());}return QString();}));
                for(auto& call:calls)if(call.get().isEmpty())throw std::runtime_error("rate-limited request cannot succeed");
                st::Services reopened(dir.path());auto health=queueEntry(reopened.queue(config),"edge");
                if(health["samples"].toInt()!=1||health["retrySeconds"].toInt()<115||health["retrySeconds"].toInt()>120||health["recoveryProbe"].toBool())
                    throw std::runtime_error(("Retry-After pause: "+path+" "+QString::fromUtf8(QJsonDocument(health).toJson(QJsonDocument::Compact))).toStdString());
            }
            // Multi-endpoint/model routing uses real requests, encryption and reopened state.
            QTemporaryDir aiDir;st::Services multi(aiDir.path());
            auto model=[](QString name,bool enabled=true){return QJsonObject{{"name",name},{"enabled",enabled}};};
            auto channel=[&](QString id,QJsonArray models,bool enabled=true){return QJsonObject{{"id",id},{"name",id},{"endpoint",base+"/ai-test"},{"apiKey","fixture-key"},{"enabled",enabled},{"models",models}};};
            QJsonArray channels{channel("first",QJsonArray{model("bad"),model("disabled",false),model("good")}),channel("second",QJsonArray{model("last")})};
            QJsonObject aiProfile{{"provider","ai"},{"enabled",true},{"channels",channels}};
            QJsonObject configs;for(auto id:st::providers)configs[id]=QJsonObject{{"enabled",false}};
            configs["ai"]=aiProfile;multi.save(configs,{{"mode","manual"}});
            QJsonObject aiSettings{{"provider","ai"},{"fallbackEnabled",true},{"fallbackRecoveryMinutes",1}};
            auto aiFirst=multi.translate(aiSettings,{"hello"},"auto","zh",fresh());
            if(aiFirst.model!="good"||aiFirst.channelId!="first"||!aiFirst.fallback)throw std::runtime_error("AI falls through failed model to next enabled model");
            st::Services aiReopened(aiDir.path());auto remembered=aiReopened.translate(aiSettings,{"hello"},"auto","zh",fresh());
            if(remembered.model!="good"||!remembered.fallback)throw std::runtime_error("AI per-model cooldown persists");
            QFile encrypted(aiDir.path()+"/services.json");encrypted.open(QIODevice::ReadOnly);auto encoded=encrypted.readAll();encrypted.close();
            if(encoded.contains("fixture-key")||encoded.contains("apiKey"))throw std::runtime_error("all AI channel keys must be encrypted");
            auto autoPolicy=aiReopened.routing();autoPolicy["mode"]="auto";aiReopened.save({},autoPolicy);
            if(aiReopened.candidates(aiReopened.profile("ai"),aiSettings).first().value("model")!="good")throw std::runtime_error("AI weights rank healthy models first");
            auto selectedSettings=aiSettings;selectedSettings["requestedChannelId"]="second";selectedSettings["requestedModel"]="last";selectedSettings["fallbackEnabled"]=false;
            auto selectedModel=aiReopened.translate(selectedSettings,{"manual model"},"auto","zh",fresh());
            if(selectedModel.channelId!="second"||selectedModel.model!="last"||selectedModel.fallback)throw std::runtime_error("explicit model selection overrides automatic model ranking");
            selectedSettings["requestedChannelId"]="first";selectedSettings["requestedModel"]="disabled";
            bool modelRejected=false;try{aiReopened.translate(selectedSettings,{"disabled manual model"},"auto","zh",fresh());}catch(...){modelRejected=true;}
            if(!modelRejected)throw std::runtime_error("explicit model selection cannot reenable disabled models");
            auto firstChannel=channels[0].toObject();firstChannel["models"]=QJsonArray{model("bad"),model("good",false),model("disabled",false)};channels[0]=firstChannel;
            aiProfile["channels"]=channels;aiReopened.save({{"ai",aiProfile}},{{"mode","manual"}});
            auto alternate=aiReopened.translate(aiSettings,{"hello"},"auto","zh",fresh());
            if(alternate.channelId!="second"||alternate.model!="last"||!alternate.fallback)throw std::runtime_error("AI falls through across channels");
            QFile aiHealth(aiDir.path()+"/service-health.json");aiHealth.open(QIODevice::ReadOnly);auto aiDoc=QJsonDocument::fromJson(aiHealth.readAll()).object();aiHealth.close();
            auto aiEntries=aiDoc.value("services").toObject();for(auto it=aiEntries.begin();it!=aiEntries.end();++it){auto row=it.value().toObject();row["failedAt"]=0;it.value()=row;}
            st::atomicJson(aiDir.path()+"/service-health.json",{{"version",2},{"services",aiEntries}});
            st::Services aiRecovered(aiDir.path());auto recovery=aiRecovered.translate(aiSettings,{"hello"},"auto","zh",fresh());
            if(recovery.model!="bad"||recovery.fallback)throw std::runtime_error("AI recovers first model after cooldown");
            firstChannel["enabled"]=false;channels[0]=firstChannel;aiProfile["channels"]=channels;aiRecovered.save({{"ai",aiProfile}});
            if(aiRecovered.translate(aiSettings,{"hello"},"auto","zh",fresh()).channelId!="second")throw std::runtime_error("disabled AI channel is skipped");
            auto definitionSettings=aiSettings;definitionSettings["definition"]=true;
            if(aiRecovered.translate(definitionSettings,{"hello"},"auto","zh",fresh()).segments!=QStringList{"meaning:hello"})throw std::runtime_error("AI explanation prompt used independently");
            aiProfile["enabled"]=false;aiRecovered.save({{"ai",aiProfile},{"google",QJsonObject{{"enabled",true},{"endpoint",base+"/google"}}}});
            if(aiRecovered.translate(aiSettings,{"hello"},"auto","zh",fresh()).provider!="google")throw std::runtime_error("disabled primary falls back to enabled service");
            aiRecovered.save({{"google",QJsonObject{{"enabled",false},{"endpoint",base+"/google"}}}});
            bool noEnabled=false;try{aiRecovered.translate(aiSettings,{"hello"},"auto","zh",fresh());}catch(const std::exception&){noEnabled=true;}
            if(!noEnabled)throw std::runtime_error("all-disabled queue must not send requests");
            std::puts(QJsonDocument(QJsonObject{{"passed",true},{"batchProtocols",true},{"concurrentDeduplication",true},{"retryAfterSecondsAndDate",true},{"queuedCancellation",true},{"translationCache",true},{"cacheLanguageIsolation",true},{"cacheDiagnosticBypass",true},{"parallelCancellation",true},{"orderedPartialFailure",true},{"singleRateLimitAttempt",true},{"longDocumentTail",true},{"sentenceProgress",true},{"longDocumentCancellation",true},{"unicodeSentenceBoundaries",true},{"exponentialBackoff",true},{"authQuotaDisablePeriods",true},{"darkTallOcrTiles",true},{"fixtureRoutes",QJsonArray::fromStringList(routes)},
                {"chineseDictionaryCandidates",true},{"offlineReverseDictionary",true},{"senseGroupedSynonyms",true},{"wordTokenizer",true},{"structuredDictionary",true},{"youdaoWordFallback",true},{"progressiveWordLookup",true},{"wordLookupLimits",true},{"wordLookupCancellation",true},{"multiAiFallback",true},{"aiModelCooldown",true},{"aiModelRecovery",true},{"aiChannelEncryption",true},{"disabledProviders",true},{"aiExplanation",true},{"dpapi",true},{"smallScreenshotOcr",true},{"aiMigration",true},{"offlineDictionary",true},{"dictionaryFallback",true},{"dictionaryDownload",true},{"dictionaryIntegrity",true},{"youdaoSignature",true},{"sogouEncoding",true},
                {"persistentFallback",true},{"recovery",true},{"cancel",true},{"httpProxy",true},{"pointerPhrases",true},
                {"manualQueue",true},{"automaticWeights",true},{"persistentQueue",true},{"diagnosticsHealth",true},{"neutralCancellationAndMisses",true}}).toJson(QJsonDocument::Compact).constData());
        }else if(args.contains("--live-candidates")){
            auto cancel=std::make_shared<std::atomic_bool>(false);
            auto chinese=services.lookupWords({},"玩具",cancel);auto english=services.lookupWords({},"beautiful",cancel);
            if(chinese["rows"].toArray().size()<2||english["related"].toArray().isEmpty())throw std::runtime_error("live bilingual dictionary alternatives unavailable");
            std::puts(QJsonDocument(QJsonObject{{"passed",true},{"chinese",chinese},{"english",english}}).toJson(QJsonDocument::Compact).constData());
        }else if(args.contains("--live-lookup")){
            auto words=services.lookupWords({},"hello beautiful running",std::make_shared<std::atomic_bool>(false));
            std::puts(QJsonDocument(words).toJson(QJsonDocument::Compact).constData());
            if(words.value("found").toInt()!=3)throw std::runtime_error("live Youdao word lookup unavailable");
        }else if(args.contains("--live-fallback")){
            if(sharedDir.isEmpty())throw std::runtime_error("isolated live profile directory required");
            st::Services live(sharedDir);
            QJsonArray routes;bool passed=true;
            QJsonObject settings{{"provider","ecdict"},{"fallbackEnabled",true},
                {"proxy",args.contains("--proxy")?args.value(args.indexOf("--proxy")+1):QString()}};
            QList<QStringList> texts{{"Hello world. How are you?"},{"今天的天气不错，我们一起出去走走吧。"}};
            QStringList screenshot;for(int i=0;i<12;++i)screenshot<<QString("This is sentence %1 in a screenshot.").arg(i+1);texts<<screenshot;
            int index=0;
            for(const auto& parts:texts){
                QJsonObject row{{"case",++index}};QElapsedTimer clock;clock.start();
                try{auto result=live.translate(settings,parts,"auto",index==2?"en":"zh",std::make_shared<std::atomic_bool>(false));
                    row["provider"]=result.provider;row["fallback"]=result.fallback;row["segments"]=result.segments.size();
                    row["translation"]=result.segments.join('\n');row["passed"]=result.segments.size()==parts.size();passed&=row["passed"].toBool();
                }catch(const std::exception& e){row["passed"]=false;row["error"]=QString::fromUtf8(e.what());passed=false;}
                row["elapsedMs"]=clock.elapsed();routes<<row;
            }
            std::puts(QJsonDocument(QJsonObject{{"passed",passed},{"fallback",routes}}).toJson(QJsonDocument::Compact).constData());
            return passed?0:1;
        }else if(args.contains("--live")){
            QJsonArray routes;
            auto live=args.contains("--live-providers")?args.value(args.indexOf("--live-providers")+1).split(','):QStringList{"google","edge","zvo","mymemory","sogou","youdao","ecdict"};
            for(auto p:live){
                QJsonObject settings{{"provider",p},{"fallbackEnabled",false},{"proxy",args.contains("--proxy")?args.value(args.indexOf("--proxy")+1):QString()}};
                QJsonObject result{{"provider",p}};
                try{
                    QElapsedTimer timing;timing.start();
                    auto data=services.translate(settings,{p=="youdao"||p=="ecdict"?"hello":"Hello world. How are you?"},"en","zh",std::make_shared<std::atomic_bool>(false));
                    result["elapsedMs"]=timing.elapsed();timing.restart();
                    auto repeat=services.translate(settings,{p=="youdao"||p=="ecdict"?"hello":"Hello world. How are you?"},"en","zh",std::make_shared<std::atomic_bool>(false));
                    result["repeatMs"]=timing.elapsed();
                    if(repeat.segments!=data.segments)throw std::runtime_error("live cache mismatch");
                    result["status"]="success";result["translation"]=data.segments.join('\n');
                }catch(const std::exception& e){result["status"]="unavailable";result["error"]=QString::fromUtf8(e.what());}
                routes.append(result);
            }
            std::puts(QJsonDocument(QJsonObject{{"live",routes}}).toJson(QJsonDocument::Compact).constData());
        }else std::puts("native core tests passed");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"test failed: %s\n",e.what());return 1;}
}

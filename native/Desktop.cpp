#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.UserProfile.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include "Desktop.h"
#include "FirefoxSelection.h"
#include "SelectionCopy.h"
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextBoundaryFinder>
#include <QTextDocumentFragment>
#include <QAbstractTextDocumentLayout>
#include <QtMath>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QTransform>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <chrono>
#include <stdexcept>

namespace st {
namespace {
QString from(const winrt::hstring& s) { return QString::fromWCharArray(s.c_str(),s.size()); }
bool cjk(QChar c) { auto n=c.unicode();return (n>=0x2e80&&n<=0x9fff)||(n>=0xf900&&n<=0xfaff); }
int letterScript(QChar c)
{
    if(!c.isLetter())return 0;
    auto script=c.script();
    // Han and kana belong to one Japanese text run, not separate languages.
    if(script==QChar::Script_Hiragana||script==QChar::Script_Katakana)script=QChar::Script_Han;
    return int(script)+1;
}
int wordScript(const Block& word)
{
    for(auto c:word.text)if(auto script=letterScript(c))return script;
    return 0;
}
double letterWidth(QChar c)
{
    return cjk(c)||c.script()==QChar::Script_Hangul?2.:c.isLetterOrNumber()?1.:.6;
}
QList<Block> scriptWords(const QList<Block>& words)
{
    QList<Block> result;
    for(auto word:words){
        QList<int> starts{0};int previous=0;
        for(int i=0;i<word.text.size();++i)if(auto script=letterScript(word.text[i])){
            if(previous&&previous!=script)starts<<i;
            previous=script;
        }
        if(starts.size()==1){result<<word;continue;}
        // OcrWord exposes a word rectangle, not character rectangles. Estimate
        // run positions only for the uncommon mixed-script word; keep native
        // boxes untouched for ordinary OCR words and split Latin fragments.
        double total=0,offset=0;for(auto c:word.text)total+=letterWidth(c);
        starts<<word.text.size();
        for(int i=0;i+1<starts.size();++i){
            auto text=word.text.mid(starts[i],starts[i+1]-starts[i]);double width=0;
            for(auto c:text)width+=letterWidth(c);
            result.append({text,{word.box.x()+word.box.width()*offset/total,word.box.y(),word.box.width()*width/total,word.box.height()},word.glyphHeight});
            offset+=width;
        }
    }return result;
}
bool numeric(const QString& s) { return QRegularExpression("^\\d+(?:[.,]\\d+)*(?:[%+])?$").match(s.trimmed()).hasMatch(); }
double sourceFontPixels(const Block& word)
{
    if(word.fontPixels>0)return word.fontPixels;
    if(word.text.isEmpty())return word.glyphHeight;
    bool han=false;for(auto c:word.text)han|=cjk(c);
    QFont font(han?"Microsoft YaHei UI":"Segoe UI");font.setPixelSize(100);
    const auto ink=QFontMetricsF(font).tightBoundingRect(word.text);
    double estimate=ink.height()>0?word.glyphHeight*100./ink.height():word.glyphHeight;
    // A mistaken capital (j -> J) can remove the measured descender. Width
    // provides a conservative upper bound without forcing one font size.
    if(word.text.size()>=4&&!word.text.contains('\n')&&ink.width()>0)
        estimate=std::min(estimate,word.box.width()*115./ink.width());
    return std::clamp(estimate,6.,256.);
}
struct InkTone { QColor foreground; int background=0; bool valid=false; };
InkTone inkTone(const QImage& image,QRectF area)
{
    const auto box=area.toAlignedRect().intersected(image.rect());if(box.isEmpty())return {};
    QList<int> border;
    for(int x=box.left();x<=box.right();++x){border<<qGray(image.pixel(x,box.top()))<<qGray(image.pixel(x,box.bottom()));}
    std::sort(border.begin(),border.end());const int bg=border[border.size()/2];
    QList<QRgb> ink;
    for(int y=box.top();y<=box.bottom();++y)for(int x=box.left();x<=box.right();++x){
        const auto pixel=image.pixel(x,y);if(std::abs(qGray(pixel)-bg)>40)ink<<pixel;
    }
    if(ink.size()<4)return {};
    std::sort(ink.begin(),ink.end(),[bg](QRgb a,QRgb b){return std::abs(qGray(a)-bg)>std::abs(qGray(b)-bg);});
    return {QColor(ink[ink.size()/10]),bg,true};
}
bool inkBridges(const QImage& image,QRectF a,QRectF b)
{
    double h=std::max(a.height(),b.height());
    if(b.left()-a.right()>h*6)return false;
    int top=std::max(0,int(std::floor(std::min(a.top(),b.top())-h*.15)));
    int bottom=std::min(image.height(),int(std::ceil(std::max(a.bottom(),b.bottom())+h*.15)));
    int blank=0,ink=0,limit=std::max(12,qRound(std::min(a.height(),b.height())*1.4));
    for(int x=std::max(0,int(std::ceil(a.right())));x<std::min(image.width(),int(std::floor(b.left())));++x){
        int low=255,high=0;
        for(int y=top;y<bottom;++y){auto value=qGray(image.pixel(x,y));low=std::min(low,value);high=std::max(high,value);}
        if(high-low>=48){++ink;blank=0;}else if(++blank>limit)return false;
    }return ink>=2;
}
QRect ocrInkBounds(const QImage& image,Cancel cancel)
{
    if(image.isNull())return {};
    QList<int> border;
    for(int x=0;x<image.width();x+=std::max(1,image.width()/80)){border<<qGray(image.pixel(x,0));border<<qGray(image.pixel(x,image.height()-1));}
    for(int y=0;y<image.height();y+=std::max(1,image.height()/80)){border<<qGray(image.pixel(0,y));border<<qGray(image.pixel(image.width()-1,y));}
    std::sort(border.begin(),border.end());int bg=border[border.size()/2],agree=0;
    for(int value:border)agree+=std::abs(value-bg)<12;
    if(agree<border.size()*.95)return image.rect();
    int left=image.width(),right=-1,top=image.height(),bottom=-1;
    auto gray=image.convertToFormat(QImage::Format_Grayscale8);
    for(int y=0;y<gray.height();++y){
        if(cancel->load())return {};
        const auto row=gray.constScanLine(y);
        for(int x=0;x<gray.width();++x)if(std::abs(int(row[x])-bg)>12){
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
        }
    }
    return right>=left?QRect(QPoint(left,top),QPoint(right,bottom)):QRect();
}
QImage ocrContrast(const QImage& source,int trimDivisor=1000)
{
    int histogram[256]{};int count=0;
    for(int y=0;y<source.height();y+=3)for(int x=0;x<source.width();x+=3){++histogram[qGray(source.pixel(x,y))];++count;}
    int low=0,high=255,sum=0;for(;low<255;++low){sum+=histogram[low];if(sum>std::max(1,count/trimDivisor))break;}
    sum=0;for(;high>low;--high){sum+=histogram[high];if(sum>std::max(1,count/trimDivisor))break;}
    int dark=0;for(int i=0;i<128;++i)dark+=histogram[i];
    bool invert=dark>count*3/4;
    if(high-low>20&&(invert||high-low<150)){
        auto normalized=source.convertToFormat(QImage::Format_RGB32);
        for(int y=0;y<normalized.height();++y){auto row=reinterpret_cast<QRgb*>(normalized.scanLine(y));
            for(int x=0;x<normalized.width();++x){int v=std::clamp((qGray(row[x])-low)*255/(high-low),0,255);if(invert)v=255-v;row[x]=qRgb(v,v,v);}}
        return normalized;
    }
    return {};
}
BITMAPINFO bitmapInfo(int width,int height)
{
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;
    bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;return bi;
}
}
QRect virtualScreen() { return {GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN)}; }
QRect monitorRect(POINT point,bool work)
{
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&info);
    auto r=work?info.rcWork:info.rcMonitor;return {r.left,r.top,r.right-r.left,r.bottom-r.top};
}
QImage capture(const QRect& area)
{
    if(area.isEmpty()||qint64(area.width())*area.height()>24'000'000)throw std::runtime_error("截图区域过大，请选择较小区域");
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);void* pixels=nullptr;auto bi=bitmapInfo(area.width(),area.height());
    HBITMAP dib=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dib){DeleteDC(dc);ReleaseDC(nullptr,screen);throw std::runtime_error("无法创建截图");}
    auto old=SelectObject(dc,dib);
    BOOL ok=BitBlt(dc,0,0,area.width(),area.height(),screen,area.x(),area.y(),SRCCOPY|CAPTUREBLT);
    QImage image=ok?QImage(static_cast<uchar*>(pixels),area.width(),area.height(),QImage::Format_RGB32).copy():QImage();
    SelectObject(dc,old);DeleteObject(dib);DeleteDC(dc);ReleaseDC(nullptr,screen);
    if(image.isNull())throw std::runtime_error("无法读取屏幕");return image;
}
QList<Block> filterUiIcons(const QImage& image,const QList<Block>& input)
{
    QList<Block> words;
    for(auto word:input){
        auto r=word.box.toAlignedRect().intersected(image.rect());
        // A coloured avatar sometimes contributes just one tall punctuation
        // fragment. It is decoration, provided it is not part of text ink.
        bool punctuation=!word.text.isEmpty()&&std::all_of(word.text.begin(),word.text.end(),[](QChar c){return c.isPunct();});
        int colour=0,ink=0;
        if(punctuation&&r.height()>8){
            auto region=r.adjusted(-2,-2,2,2).intersected(image.rect());
            for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x){
                auto p=image.pixelColor(x,y);int low=std::min({p.red(),p.green(),p.blue()}),high=std::max({p.red(),p.green(),p.blue()});
                if(qGray(p.rgb())<210){++ink;colour+=high-low>45;}
            }
        }
        if(ink>8&&colour>ink*.6){
            const auto region=r.adjusted(-qRound(r.height()*1.5),-4,3,4).intersected(image.rect());
            QRect coloured;
            for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x){
                auto p=image.pixelColor(x,y);
                if(qGray(p.rgb())<210&&std::max({p.red(),p.green(),p.blue()})-std::min({p.red(),p.green(),p.blue()})>45)
                    coloured=coloured.united(QRect(x,y,1,1));
            }
            bool adjacentText=false;
            for(const auto& other:input)if(other.box!=word.box&&other.box.intersects(coloured)){adjacentText=true;break;}
            // Colour alone is not evidence: a genuine coloured ! or ; stays.
            // Require a separate, roughly square picture around the fragment.
            if(!adjacentText&&coloured.left()>region.left()+2&&coloured.width()>r.width()*2&&
                coloured.width()>coloured.height()*.7&&coloured.width()<coloured.height()*1.6)continue;
        }
        // Occasionally OCR fuses a round history icon with the first digit.
        // Split only at a real blank column and when the left ink is taller.
        if(word.text.size()>=2&&word.text.startsWith('0')&&numeric(word.text)&&r.width()>r.height()*1.3){
            const auto tone=inkTone(image,r.adjusted(-1,-1,1,1));
            if(tone.valid)for(int x=r.left()+qRound(r.height()*.8);x<r.right()-3;++x){
                bool blank=true;for(int y=r.top();y<=r.bottom();++y)if(std::abs(qGray(image.pixel(x,y))-tone.background)>55){blank=false;break;}
                if(!blank)continue;
                QRect left,right;
                for(int y=r.top();y<=r.bottom();++y)for(int k=r.left();k<=r.right();++k)if(std::abs(qGray(image.pixel(k,y))-tone.background)>55){
                    if(k<x)left=left.united(QRect(k,y,1,1));else right=right.united(QRect(k,y,1,1));
                }
                if(!right.isEmpty()&&left.height()>right.height()*1.35&&left.width()>left.height()*.7){
                    word.text.remove(0,1);word.box=right;word.glyphHeight=right.height();
                }
                break;
            }
        }
        words<<word;
    }
    // A recognizer can call a person/grid/tag icon a Chinese character or 0.
    // Require both graphical layout evidence and a different ink colour from
    // its label. Never strip a prefix merely because its text looks unusual.
    struct Candidate { QList<int> indices; QRectF box; int label=-1; };
    QList<Candidate> candidates;QSet<int> claimed;
    auto symbol=[](const Block& w){
        if(w.text.isEmpty()||w.text.size()>3)return false;
        int letters=0;for(auto c:w.text)letters+=c.isLetter()&&!cjk(c);
        if(letters>1)return false;
        return true;
    };
    QList<int> symbols,labels;
    for(int i=0;i<words.size();++i){
        if(symbol(words[i]))symbols<<i;
        int letters=0;for(auto c:words[i].text)letters+=c.isLetter();if(letters>=2||numeric(words[i].text))labels<<i;
    }
    if(symbols.isEmpty()||labels.isEmpty())return words;
    for(int i=0;i<words.size();++i){
        if(claimed.contains(i)||!symbol(words[i]))continue;
        Candidate candidate{{i},words[i].box};
        for(int j:symbols){
            if(i==j||claimed.contains(j))continue;
            auto b=words[j].box,a=candidate.box;double h=std::max(a.height(),b.height());
            if(b.left()<a.left()||b.left()-a.right()>h*.4||b.right()>a.left()+h*1.8||
                std::abs(a.center().y()-b.center().y())>h*.3)continue;
            candidate.indices<<j;candidate.box=a.united(b);
        }
        const auto a=candidate.box;const double h=a.height();
        if(h<5||a.width()<h*.5||a.width()>h*1.8)continue;
        // Real list numbers and single-letter prefixes are narrower than a
        // square pictogram. In particular, never discard multi-digit counts.
        if(candidate.indices.size()==1){
            const auto text=words[i].text;
            if(numeric(text)&&(text.size()!=1||a.width()<h*.85))continue;
            if(text.size()==1&&text.front().script()==QChar::Script_Latin&&a.width()<h*.85)continue;
        }
        double nearest=1e9;
        for(int j:labels){
            if(candidate.indices.contains(j))continue;
            const auto b=words[j].box;const double gap=b.left()-a.right();
            if(gap<h*.25||gap>h*1.6||gap>=nearest||h<b.height()*.65||h>b.height()*1.9||
                std::abs(a.center().y()-b.center().y())>std::max(h,b.height())*.5)continue;
            candidate.label=j;nearest=gap;
        }
        if(candidate.label<0)continue;
        for(auto index:candidate.indices)claimed.insert(index);
        candidates<<candidate;
    }
    QSet<int> excluded;int latin=0,han=0;
    for(const auto& word:words)for(auto c:word.text){latin+=c.script()==QChar::Script_Latin;han+=cjk(c);}
    for(const auto& candidate:candidates){
        const auto a=candidate.box,b=words[candidate.label].box;
        int peers=0;
        for(const auto& other:candidates){
            if(other.indices==candidate.indices)continue;
            const auto c=other.box,d=words[other.label].box;const double h=std::max(a.height(),c.height());
            bool column=std::abs(a.center().x()-c.center().x())<h*.4&&std::abs(b.left()-d.left())<h*.45&&
                std::abs(a.center().y()-c.center().y())>h*1.4&&std::abs(a.center().y()-c.center().y())<h*18;
            bool row=std::abs(a.center().y()-c.center().y())<h*.4&&std::abs(a.center().x()-c.center().x())>h*2;
            if(column||row)++peers;
        }
        const auto icon=inkTone(image,a.adjusted(-1,-1,1,1)),text=inkTone(image,b.adjusted(-1,-1,1,1));
        if(!icon.valid||!text.valid||std::abs(icon.background-text.background)>35)continue;
        // ClearType adds coloured fringes even to black text. Compare ink
        // luminance, not the largest RGB channel, to preserve real prefixes.
        const int difference=std::abs(qGray(icon.foreground.rgb())-qGray(text.foreground.rgb()));
        bool cjkMark=false;for(auto index:candidate.indices)for(auto c:words[index].text)cjkMark|=cjk(c);
        QString marks;for(auto index:candidate.indices)marks+=words[index].text;
        const auto backdrop=image.pixelColor(std::clamp(qRound(a.left()-2),0,image.width()-1),std::clamp(qRound(a.top()-2),0,image.height()-1));
        const bool buttonChevrons=candidate.indices.size()==2&&(marks=="<〉"||marks=="〈〉"||marks=="<>")&&
            backdrop.saturation()>100&&backdrop.lightness()<170;
        const bool tallMark=peers&&a.height()>b.height()*1.15&&latin>han*12;
        const bool nonLatinIcon=peers>=2&&cjkMark&&latin>han*30;
        if(difference<(peers?24:48)&&!tallMark&&!nonLatinIcon&&!buttonChevrons)continue;
        if(!peers&&!buttonChevrons&&(a.width()<a.height()*.8||a.width()>a.height()*1.35))continue;
        for(auto index:candidate.indices)excluded.insert(index);
    }
    QList<Block> result;for(int i=0;i<words.size();++i)if(!excluded.contains(i))result<<words[i];return result;
}
QString screenshotTranslationText(const QString& text)
{
    // Only call on an OCR paragraph whose rows were grouped geometrically.
    // Preserve explicit paragraphs and list items, and keep the original text
    // separately for copying, editing and mapping back to the screenshot.
    auto lines=QString(text).replace("\r\n","\n").replace('\r','\n').split('\n');
    QString out;bool paragraphStart=true;
    const QRegularExpression list("^(?:[-*•●▪◦‣]|\\d+[.)、]|[A-Za-z][.)])\\s");
    for(auto line:lines){
        line=line.trimmed();
        if(line.isEmpty()){if(!out.isEmpty()&&!out.endsWith("\n\n"))out+="\n\n";paragraphStart=true;continue;}
        if(!out.isEmpty()&&!paragraphStart){
            if(list.match(line).hasMatch())out+='\n';
            else if(out.endsWith(QChar(0x00ad)))out.chop(1); // an explicit discretionary hyphen
            else if(out.endsWith('-')||out.endsWith(QChar(0x2010))||out.endsWith(QChar(0x2011))){
                // A printed hyphen can be part of a model name or compound;
                // never guess it away (1.25-bit, 30B-A3B, real-world).
            }else if(!(cjk(out.back())&&cjk(line.front()))&&
                !QString(",.!?:;，。！？：；、)]}）】》」』”’").contains(line.front())&&
                !(QString("，。！？：；、").contains(out.back())&&cjk(line.front())))out+=' ';
        }
        out+=line;paragraphStart=false;
    }
    return out.trimmed();
}
QList<Block> groupWords(QList<Block> words)
{
    std::sort(words.begin(),words.end(),[](auto& a,auto& b){return a.box.y()==b.box.y()?a.box.x()<b.box.x():a.box.y()<b.box.y();});
    QList<QList<Block>> rows;
    for(auto word:words){
        if(word.text.isEmpty()||word.box.isEmpty())continue;
        bool found=false;
        for(auto& row:rows){
            auto nearest=std::min_element(row.begin(),row.end(),[&](const Block& a,const Block& b){
                auto distance=[&](QRectF r){return std::max({r.left()-word.box.right(),word.box.left()-r.right(),0.});};
                return distance(a.box)<distance(b.box);
            });
            auto b=nearest->box;
            // Keep WinRT's native line membership, including rotated lines.
            // Geometry is a fallback for synthetic/merged words without a line id.
            if(word.ocrLine&&row[0].ocrLine){
                if(word.ocrLine==row[0].ocrLine){row<<word;found=true;break;}
                continue;
            }
            // After tiling, a distant column must not become the vertical
            // anchor for this word (a higher search box used to split Products).
            const double gap=std::max({b.left()-word.box.right(),word.box.left()-b.right(),0.});
            if(gap>std::max(18.,std::max(b.height(),word.box.height())*1.6))continue;
            bool punctuation=std::all_of(word.text.begin(),word.text.end(),[](QChar c){return c.isPunct();});
            bool baseline=punctuation&&word.box.top()>=b.top()-b.height()*.2&&word.box.bottom()<=b.bottom()+b.height()*.3&&
                word.box.left()>=b.left()-b.height()&&word.box.left()<=row.last().box.right()+b.height()*1.5;
            if(baseline||std::abs(b.center().y()-word.box.center().y())<=std::max(b.height(),word.box.height())*.45||std::min(b.bottom(),word.box.bottom())-std::max(b.top(),word.box.top())>=std::min(b.height(),word.box.height())*.5){
                row<<word;found=true;break;
            }
        }if(!found)rows.append(QList<Block>{word});
    }
    // Words arrive sorted by top edge, so a short middle word can arrive after
    // both neighbours. Reconnect those local fragments once all words exist.
    for(int i=0;i<rows.size();++i)for(int j=i+1;j<rows.size();){
        bool join=false;
        for(const auto& a:rows[i])for(const auto& b:rows[j]){
            if(a.ocrLine&&b.ocrLine&&a.ocrLine!=b.ocrLine)continue;
            const auto x=a.box,y=b.box;const double h=std::max(x.height(),y.height());
            const double gap=std::max({x.left()-y.right(),y.left()-x.right(),0.});
            const double overlap=std::min(x.bottom(),y.bottom())-std::max(x.top(),y.top());
            if(gap<=std::max(18.,h*1.6)&&overlap>=std::min(x.height(),y.height())*.5&&
                std::abs(x.center().y()-y.center().y())<=h*.5){join=true;break;}
        }
        if(join){rows[i].append(rows.takeAt(j));j=i+1;}else ++j;
    }
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.front().box.top()<b.front().box.top();});
    // Separate columns can have slightly different glyph tops. Within each
    // visual row, read left to right without using a remote column as an anchor.
    auto bounds=[](const QList<Block>& row){QRectF box;for(const auto& word:row)box=box.united(word.box);return box;};
    for(int begin=0;begin<rows.size();){
        const auto first=bounds(rows[begin]);int end=begin+1;
        while(end<rows.size()){
            const auto next=bounds(rows[end]);
            if(std::abs(first.center().y()-next.center().y())>std::min(first.height(),next.height())*.5)break;
            ++end;
        }
        std::sort(rows.begin()+begin,rows.begin()+end,[&](const auto& a,const auto& b){return bounds(a).left()<bounds(b).left();});
        begin=end;
    }
    QList<Block> out;int paragraph=-1;
    for(auto row:rows){
        std::sort(row.begin(),row.end(),[](auto& a,auto& b){return a.box.x()<b.box.x();});
        QList<double> heights;for(auto w:row)heights<<w.box.height();std::sort(heights.begin(),heights.end());
        double h=heights[heights.size()/2],limit=std::max(14.,h*1.2);
        QList<Block> blocks;Block run;Block prev;QList<double> fontSizes;
        auto flush=[&]{if(!run.text.isEmpty()){
            run.glyphHeight=h;
            if(!fontSizes.isEmpty()){std::sort(fontSizes.begin(),fontSizes.end());run.fontPixels=fontSizes[fontSizes.size()/2];}
            blocks<<run;run={};fontSizes.clear();
        }};
        for(int i=0;i<row.size();++i){
            auto w=row[i];
            if(!run.text.isEmpty()&&w.box.x()-prev.box.right()>limit)flush();
            if(!fontSizes.isEmpty()&&wordScript(w)&&w.text.size()>=2&&prev.text.size()>=2&&w.box.x()-prev.box.right()>std::min(w.box.height(),prev.box.height())*.3){
                const auto current=sourceFontPixels(w),previous=fontSizes.back();
                // x-height-only words can be ~30% shorter in the same font.
                if((current>previous*1.6&&w.glyphHeight>prev.glyphHeight*1.6)||
                    (previous>current*1.6&&prev.glyphHeight>w.glyphHeight*1.6))flush();
            }
            // Small dots are often returned as fullwidth commas/periods. Only
            // normalize when tightly enclosed by digits in a numeric token.
            bool numberDot=i>0&&i+1<row.size()&&QString("．，·").contains(w.text)&&w.text.size()==1&&
                w.box.height()<h*.4&&row[i-1].text.back().isDigit()&&row[i+1].text.front().isDigit()&&
                w.box.left()-row[i-1].box.right()<h*.3&&row[i+1].box.left()-w.box.right()<h*.3;
            if(numberDot)w.text=w.box.height()>h*.25?",":".";
            // Windows OCR sometimes calls a thin hyphen a Chinese "一".
            // Require Latin neighbours and a tiny, wide horizontal glyph.
            const bool thinHyphen=i>0&&w.text=="一"&&w.box.height()<h*.25&&w.box.width()>w.box.height()*1.8&&
                row[i-1].text.back().isLetterOrNumber()&&(wordScript(row[i-1])==int(QChar::Script_Latin)+1||numeric(row[i-1].text))&&
                w.box.left()-row[i-1].box.right()<h*.3&&
                (i+1==row.size()||(wordScript(row[i+1])==int(QChar::Script_Latin)+1&&row[i+1].box.left()-w.box.right()<h*.3));
            if(thinHyphen)w.text="-";
            if(run.text.isEmpty())run=w;
            else{
                bool numericJoin=w.text.front().isDigit()&&(run.text.endsWith('.')||run.text.endsWith(','))&&i>=2&&row[i-2].text.back().isDigit()&&w.box.left()-prev.box.right()<h*.3;
                if(!thinHyphen&&!numericJoin&&!QString(",.!?:;，。！？：；、)]}）】》」』”’").contains(w.text.front())&&!(cjk(run.text.back())&&cjk(w.text.front()))&&w.box.x()-prev.box.right()>std::max(1.,std::min(prev.box.height(),w.box.height())*.16))run.text+=" ";
                run.text+=w.text;run.box=run.box.united(w.box);
            }
            if(std::any_of(w.text.begin(),w.text.end(),[](QChar c){return c.isLetter();}))fontSizes<<sourceFontPixels(w);
            prev=w;
        }flush();
        if(blocks.size()==1){
            auto b=blocks[0];
            bool list=QRegularExpression("^(?:[-*•●▪◦‣]|\\d+[.)、]|[A-Za-z][.)])\\s").match(b.text).hasMatch();
            if(paragraph>=0&&!list){
                auto& p=out[paragraph];double gap=b.box.y()-p.box.bottom();
                if(std::abs(p.box.x()-b.box.x())<=h*.6&&gap>=-2&&gap<=std::max(8.,h)&&std::abs(p.glyphHeight-h)<=3){
                    p.text+="\n"+b.text;p.box=p.box.united(b.box);continue;
                }
            }paragraph=list?-1:out.size();
        }else paragraph=-1;
        out.append(blocks);
    }return out;
}
Block wordAt(const QList<Block>& words,QPointF point)
{
    Block result;double nearest=1e9;
    for(auto word:words){
        // Permit only tiny gaps between letters. Do not translate an unrelated
        // word elsewhere in the crop when the pointer rests on blank space.
        auto b=word.box;double dx=std::max({b.left()-point.x(),0.,point.x()-b.right()});
        double dy=std::max({b.top()-point.y(),0.,point.y()-b.bottom()});
        double distance=std::hypot(dx,dy);
        if(distance<=std::clamp(b.height()*.3,3.,10.)&&distance<nearest){
            result=word;nearest=distance;
        }
    }return result;
}
Block phraseAt(const QList<Block>& words,QPointF point,const QImage* image)
{
    const auto parts=scriptWords(words);
    const auto seed=wordAt(parts,point);
    if(seed.text.isEmpty())return {};
    QList<Block> line;
    for(auto word:parts){
        if(word.text.trimmed().isEmpty()||word.box.isEmpty())continue;
        auto a=seed.box,b=word.box;
        auto overlap=std::min(a.bottom(),b.bottom())-std::max(a.top(),b.top());
        // OCR sometimes emits a period as a tiny box just below the letters'
        // baseline. Keep it so sentence boundaries survive seed-word changes.
        bool punctuation=std::all_of(word.text.begin(),word.text.end(),[](QChar c){
            return QString(",.!?:;，。！？：；、\"'()[]{}（）【】《》「」『』“”‘’").contains(c);
        });
        bool nearBaseline=punctuation&&b.top()>=a.top()-a.height()*.25&&b.bottom()<=a.bottom()+a.height()*.25;
        if(nearBaseline||(overlap>=std::min(a.height(),b.height())*.5&&
            std::abs(a.center().y()-b.center().y())<=std::max(a.height(),b.height())*.6))
            line<<word;
    }
    std::sort(line.begin(),line.end(),[](const auto& a,const auto& b){return a.box.left()<b.box.left();});
    int at=0;for(int i=0;i<line.size();++i)if(line[i].box==seed.box&&line[i].text==seed.text){at=i;break;}
    auto ends=[](QString text){
        text=text.trimmed();
        while(!text.isEmpty()&&QString("\"'”’）)]》」』").contains(text.back()))text.chop(1);
        return !text.isEmpty()&&QString(".!?。！？;；").contains(text.back());
    };
    auto connected=[&](int left,int right){
        auto a=line[left].box,b=line[right].box;
        // Commas can be only a few pixels high. Their height must not turn
        // an ordinary following word space into a spurious column boundary.
        auto height=wordScript(line[left])&&wordScript(line[right])?
            std::min(a.height(),b.height()):std::max(a.height(),b.height());
        return b.left()-a.right()<=std::max(12.,height*1.4)||(image&&inkBridges(*image,a,b));
    };
    // Keep only the sentence on the pointed line. Large gaps separate columns
    // and menu labels; terminal punctuation separates neighbouring sentences.
    int first=at,last=at;
    while(first>0&&connected(first-1,first)&&!ends(line[first-1].text))--first;
    while(last+1<line.size()&&connected(last,last+1)&&!ends(line[last].text))++last;
    QMap<int,double> weights;double total=0;
    for(int i=first;i<=last;++i)for(auto c:line[i].text)if(auto script=letterScript(c)){
        // Compare text extent: one full-width CJK character is approximately
        // two Latin letters. Punctuation and numbers do not vote for a language.
        auto width=letterWidth(c);weights[script]+=width;total+=width;
    }
    int dominant=0;for(auto it=weights.cbegin();it!=weights.cend();++it)
        if(it.value()>total*.5)dominant=it.key();
    int pointed=wordScript(seed);
    if(!pointed){
        int l=at-1,r=at+1;
        while(l>=first&&!wordScript(line[l]))--l;
        while(r<=last&&!wordScript(line[r]))++r;
        if(l>=first&&r<=last&&wordScript(line[l])==wordScript(line[r]))pointed=wordScript(line[l]);
    }
    bool minority=pointed&&dominant&&pointed!=dominant;
    if(minority){
        auto belongs=[&](int i){auto script=wordScript(line[i]);return !script||script==pointed;};
        int l=at,r=at;
        while(l>first&&belongs(l-1))--l;
        while(r<last&&belongs(r+1))++r;
        while(l<at&&!wordScript(line[l]))++l;
        while(r>at&&!wordScript(line[r]))--r;
        first=l;last=r;
    }
    // A dense line without punctuation stays a small local phrase. Grow around
    // the pointer so the selected word is never lost to a prefix truncation.
    int left=at,right=at,characters=seed.text.size();
    while(left>first||right<last){
        bool takeLeft=left>first&&(right==last||
            point.x()-line[left-1].box.right()<=line[right+1].box.left()-point.x());
        int next=takeLeft?left-1:right+1;
        if(right-left+1>=48||characters+line[next].text.size()+1>240)break;
        characters+=line[next].text.size()+1;
        if(takeLeft)--left;else ++right;
    }
    Block phrase;
    for(int i=left;i<=right;++i){
        auto word=line[i];auto text=word.text.trimmed();
        if(phrase.text.isEmpty()){phrase=word;phrase.text=text;continue;}
        bool punctuation=QString(",.!?:;，。！？：；、)]}）】》」』”’").contains(text.front());
        bool opening=QString("([{（【《「『“‘").contains(phrase.text.back());
        bool eastAsian=(cjk(phrase.text.back())||QString("，。！？：；、）】》」』”’").contains(phrase.text.back()))&&cjk(text.front());
        // Windows OCR can split a Latin word into adjacent blocks. Use the
        // same letter-gap tolerance as screenshot OCR; a block boundary alone
        // is not a word space (especially with small text and CJK recognizers).
        if(!punctuation&&!opening&&!eastAsian&&
            word.box.left()-line[i-1].box.right()>std::max(1.,std::min(word.box.height(),line[i-1].box.height())*.16))phrase.text+=' ';
        phrase.text+=text;phrase.box=phrase.box.united(word.box);
    }
    if(minority){
        const QString separators=",;:，。！？：；、()[]{}（）【】《》「」『』“”‘’\"";
        while(!phrase.text.isEmpty()&&(phrase.text.front().isSpace()||separators.contains(phrase.text.front())))phrase.text.remove(0,1);
        while(!phrase.text.isEmpty()&&(phrase.text.back().isSpace()||separators.contains(phrase.text.back())))phrase.text.chop(1);
    }
    return phrase;
}
namespace {
QList<Block> screenshotBlocks(const QImage& image,const QList<Block>& words,Cancel cancel)
{
    auto blocks=groupWords(words);
    struct Strip { int index; QRect crop,slot; double scale; };
    QList<Strip> strips;QList<Block> refined=blocks;QMap<int,QString> proposals;bool confirming=false;
    QImage sheet(1800,1800,QImage::Format_RGB32);sheet.fill(Qt::white);int x=12,y=12,rowHeight=0;
    auto flush=[&]{
        if(strips.isEmpty()||cancel->load())return;
        const auto found=recognize(sheet.copy(0,0,sheet.width(),std::min(sheet.height(),y+rowHeight+12)),cancel,false,1.,false);
        for(const auto& strip:strips){
            QList<Block> local;
            for(auto word:found)if(strip.slot.contains(word.box.center().toPoint())){
                const auto b=word.box;
                word.box={strip.crop.x()+(b.x()-strip.slot.x())/strip.scale,strip.crop.y()+(b.y()-strip.slot.y())/strip.scale,b.width()/strip.scale,b.height()/strip.scale};
                word.glyphHeight/=strip.scale;word.ocrLine=0;local<<word;
            }
            const auto groups=groupWords(local);if(groups.size()!=1)continue;
            const auto original=blocks[strip.index];auto candidate=groups.front();
            auto letters=[](const QString& text){int n=0;for(auto c:text)n+=c.isLetterOrNumber();return n;};
            const auto before=letters(original.text),after=letters(candidate.text);
            int oldOther=0,newOther=0;for(auto c:original.text)oldOther+=c.isLetter()&&c.script()!=QChar::Script_Latin;
            for(auto c:candidate.text)newOther+=c.isLetter()&&(c.script()!=QChar::Script_Latin||c.unicode()>127);
            if(newOther>oldOther||candidate.box.height()<original.box.height()*.8||after<before*.85||after>before*1.3+2||candidate.box.width()<original.box.width()*.8||
                candidate.box.left()>original.box.left()+original.glyphHeight*.5||candidate.box.right()<original.box.right()-original.glyphHeight*.5)continue;
            auto compact=[](QString text){return text.remove(QRegularExpression("\\s")).toLower();};
            if(compact(original.text)==compact(candidate.text)&&candidate.text.count(' ')>=original.text.count(' '))continue;
            auto lookalikes=[](QString text){return text.replace('I','l').remove(QRegularExpression("\\s")).toLower();};
            if(lookalikes(original.text)==lookalikes(candidate.text)&&candidate.text.count(' ')>=original.text.count(' '))continue;
            auto digits=[](QString text){return text.remove(QRegularExpression("[^0-9]"));};
            if(digits(original.text)!=digits(candidate.text))continue;
            auto alnum=[](QString text){return text.remove(QRegularExpression("[^\\p{L}\\p{N}]" )).toLower();};
            if((original.text.size()>32||after<before)&&alnum(original.text)!=alnum(candidate.text))continue;
            if(candidate.text.count(' ')>original.text.count(' '))continue;
            if(confirming){
                if(compact(candidate.text)==compact(proposals.value(strip.index)))refined[strip.index].text=proposals.value(strip.index);
            }else if(alnum(original.text)==alnum(candidate.text))refined[strip.index].text=candidate.text;
            else proposals.insert(strip.index,candidate.text);
        }
        strips.clear();sheet.fill(Qt::white);x=12;y=12;rowHeight=0;
    };
    auto append=[&](int index,double scale){
        const auto crop=blocks[index].box.toAlignedRect().adjusted(-3,-3,3,3).intersected(image.rect());
        QSize size(qRound(crop.width()*scale),qRound(crop.height()*scale));
        if(x+size.width()+12>sheet.width()){x=12;y+=rowHeight+28;rowHeight=0;}
        if(y+size.height()+12>sheet.height())flush();
        QRect slot(QPoint(x,y),size);
        {QPainter painter(&sheet);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(slot,image.copy(crop));}
        strips.append({index,crop,slot,scale});x+=size.width()+48;rowHeight=std::max(rowHeight,size.height());
    };
    for(int i=0;i<blocks.size();++i){
        if(cancel->load())return {};
        const auto& block=blocks[i];int latin=0,other=0;
        for(auto c:block.text)if(c.isLetter()){if(c.script()==QChar::Script_Latin)++latin;else ++other;}
        if(latin<3||other>1||block.glyphHeight>24||block.box.height()>block.glyphHeight*1.6||block.text.contains('\n')||block.text.contains('/'))continue;
        const auto crop=block.box.toAlignedRect().adjusted(-3,-3,3,3).intersected(image.rect());
        const double scale=std::min(4.,1760./crop.width());if(scale<2.5)continue;
        append(i,scale);
    }
    flush();
    // A second size must agree before changing letters. One enlarged pass
    // can itself misread a correct label, especially with a different neighbour.
    confirming=true;
    for(auto it=proposals.cbegin();it!=proposals.cend();++it){
        if(cancel->load())return {};
        append(it.key(),std::min(3.25,1760./(blocks[it.key()].box.width()+8)));
    }
    flush();
    for(auto& block:refined){
        // In a dotted version token, I/l is a common OCR reading of digit 1.
        // Require at least two numeric components so ordinary words are intact.
        static const QRegularExpression version("\\bv[lI]\\s*[.．，]\\s*(\\d+(?:\\s*[.．，]\\s*\\d+)+)");
        auto match=version.match(block.text);
        if(match.hasMatch()){
            auto tail=match.captured(1);tail.replace(QRegularExpression("\\s*[.．，]\\s*"),".");
            block.text.replace(match.capturedStart(),match.capturedLength(),"v1."+tail);
        }
    }
    return refined;
}
}
QList<Block> recognize(const QImage& source,Cancel cancel,bool grouped,double maxScale,bool retryEmpty)
{
    using namespace winrt::Windows::Media::Ocr;
    using namespace winrt::Windows::Graphics::Imaging;
    try{
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Cleanup { ~Cleanup(){winrt::uninit_apartment();} } cleanup;
        OcrEngine engine{nullptr};auto available=OcrEngine::AvailableRecognizerLanguages();
        for(auto tag:winrt::Windows::System::UserProfile::GlobalizationPreferences::Languages()){
            auto s=from(tag);if(!s.startsWith("zh")&&!s.startsWith("ja")&&!s.startsWith("ko"))continue;
            for(auto lang:available)if(from(lang.LanguageTag()).left(2)==s.left(2)){
                engine=OcrEngine::TryCreateFromLanguage(lang);if(engine)break;
            }if(engine)break;
        }
        if(!engine)engine=OcrEngine::TryCreateFromUserProfileLanguages();
        if(!engine&&available.Size())engine=OcrEngine::TryCreateFromLanguage(available.GetAt(0));
        if(!engine)throw std::runtime_error("Windows 未安装 OCR 语言包；请在 Windows 语言设置中安装");
        if(source.isNull()||cancel->load())return {};
        if(retryEmpty&&(source.width()>640||source.height()>240)){
            auto ink=ocrInkBounds(source,cancel);
            if(cancel->load())return {};
            auto crop=ink.adjusted(-12,-12,12,12).intersected(source.rect());
            if(!ink.isEmpty()&&!crop.isEmpty()&&qint64(crop.width())*crop.height()<qint64(source.width())*source.height()/2){
                auto found=recognize(source.copy(crop),cancel,false,maxScale,retryEmpty);
                for(auto& word:found)word.box.translate(crop.topLeft());
                if(!found.isEmpty()||cancel->load())return grouped?screenshotBlocks(source,found,cancel):found;
            }
        }
        const int maxSide=int(OcrEngine::MaxImageDimension());
        // Tile full captures with overlapping context instead of shrinking tiny glyphs.
        // Pointer scans already refine a local phrase: preserve their established scan scale.
        const int tile=std::min(1200,maxSide/3),margin=320;
        if(retryEmpty&&maxScale>0&&(source.width()>tile||source.height()>tile)){
            QList<Block> collected;
            for(int y=0;y<source.height();y+=tile)for(int x=0;x<source.width();x+=tile){
                if(cancel->load())return {};
                QRect owner(x,y,std::min(tile,source.width()-x),std::min(tile,source.height()-y));
                QRect crop=owner.adjusted(-margin,-margin,margin,margin).intersected(source.rect());
                // Recursive crops use scale constrained to the OCR limit; disable retiling below.
                auto found=recognize(source.copy(crop),cancel,false,-std::abs(maxScale),retryEmpty);
                for(auto word:found){word.box.translate(crop.topLeft());
                    const auto c=word.box.center();
                    if(c.x()>=owner.x()&&c.x()<owner.x()+owner.width()&&c.y()>=owner.y()&&c.y()<owner.y()+owner.height())collected<<word;}
            }
            for(auto& word:collected)word.ocrLine=0;
            return grouped?screenshotBlocks(source,collected,cancel):collected;
        }
        maxScale=std::abs(maxScale);
        if(retryEmpty)maxScale=std::max(maxScale,2.);
        const QImage* pixels=&source;QImage normalized;
        if(retryEmpty){normalized=ocrContrast(source);if(!normalized.isNull())pixels=&normalized;}
        auto read=[&](OcrEngine reader,double requested,int padding){
            QList<Block> words;if(cancel->load())return words;
            double scale=std::min(requested,double(maxSide-2*padding)/std::max(source.width(),source.height()));
            QSize scaled(qMax(1,qRound(source.width()*scale)),qMax(1,qRound(source.height()*scale)));
            QImage image=pixels->scaled(scaled,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
            if(padding){
                QImage padded(scaled+QSize(padding*2,padding*2),QImage::Format_ARGB32);
                padded.fill(pixels->pixelColor(source.width()-1,source.height()-1));
                {QPainter painter(&padded);painter.drawImage(padding,padding,image);}image=padded;
            }
            for(int y=0;y<image.height();++y){auto px=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)px[x]|=0xff000000;}
            auto size=uint32_t(image.sizeInBytes());winrt::Windows::Storage::Streams::Buffer buffer(size);
            std::memcpy(buffer.data(),image.constBits(),size);buffer.Length(size);
            SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8,image.width(),image.height(),BitmapAlphaMode::Premultiplied);bitmap.CopyFromBuffer(buffer);
            if(cancel->load())return words;
            auto result=reader.RecognizeAsync(bitmap).get();
            const double sx=double(scaled.width())/source.width(),sy=double(scaled.height())/source.height();
            // WinRT returns boxes in its deskewed coordinate system. Rotate them
            // around the padded image center before mapping to screen pixels.
            QTransform orientation;
            if(auto angle=result.TextAngle()){
                orientation.translate(image.width()/2.,image.height()/2.);
                orientation.rotate(angle.Value());
                orientation.translate(-image.width()/2.,-image.height()/2.);
            }
            int lineId=0;
            for(auto line:result.Lines()){++lineId;for(auto word:line.Words()){
                auto r=word.BoundingRect();auto physical=orientation.mapRect(QRectF(r.X,r.Y,r.Width,r.Height));
                QRectF box((physical.x()-padding)/sx,(physical.y()-padding)/sy,physical.width()/sx,physical.height()/sy);
                box=box.intersected(QRectF(source.rect()));
                if(!box.isEmpty())words.append({from(word.Text()),box,r.Height/sy,lineId});
            }}return filterUiIcons(source,words);
        };
        const int border=retryEmpty?32:0;
        auto words=read(engine,maxScale,border);
        if(retryEmpty&&!words.isEmpty()&&!cancel->load()){
            QList<double> heights;for(auto w:words)if(w.text.size()&&w.box.height()>3)heights<<w.box.height();
            std::sort(heights.begin(),heights.end());
            if(!heights.isEmpty()&&heights[heights.size()/2]<20){
                auto alternative=read(engine,3.,32);
                auto score=[](const QList<Block>& found){int count=0;for(const auto& w:found)for(auto c:w.text)if(c.isLetterOrNumber())count+=cjk(c)?2:1;return count;};
                if(score(alternative)>score(words))words=alternative;
            }
        }
        // The CJK recognizer may drop a short Latin word entirely. Retry an
        // installed English recognizer, without changing the user's languages.
        OcrEngine english{nullptr};
        if(retryEmpty&&words.isEmpty()&&!cancel->load())for(auto lang:available)if(from(lang.LanguageTag()).startsWith("en")){
            english=OcrEngine::TryCreateFromLanguage(lang);if(english){words=read(english,maxScale,border);break;}
        }
        // Tiny pictograms can look like Han characters, so one such guess must
        // not disable English OCR for an otherwise Latin page. Retain real
        // multi-character non-Latin spans when merging the two recognizers.
        if(retryEmpty&&!words.isEmpty()&&!cancel->load()){
            int latin=0,other=0;for(const auto& word:words)for(auto c:word.text){
                if(c.isLetter()){if(c.script()==QChar::Script_Latin)++latin;else ++other;}}
            if(latin>=3&&other<=std::max(3,latin/10)&&!from(engine.RecognizerLanguage().LanguageTag()).startsWith("en")){
                for(auto lang:available)if(from(lang.LanguageTag()).startsWith("en")){
                    english=OcrEngine::TryCreateFromLanguage(lang);break;}
                if(english){auto alternative=read(english,maxScale,border);
                    int letters=0;for(const auto& word:alternative)for(auto c:word.text)if(c.isLetter())++letters;
                    if(letters>=latin*.9&&letters<=latin*1.5+8){
                        QList<Block> retained;
                        for(const auto& word:words){
                            int count=0;for(auto c:word.text)count+=c.isLetter()&&c.script()!=QChar::Script_Latin;
                            if(count>=2)retained<<word;
                        }
                        if(!retained.isEmpty()){
                            alternative.erase(std::remove_if(alternative.begin(),alternative.end(),[&](const Block& candidate){
                                for(const auto& keep:retained)if(candidate.box.intersected(keep.box).width()*candidate.box.intersected(keep.box).height()>
                                    std::min(candidate.box.width()*candidate.box.height(),keep.box.width()*keep.box.height())*.35)return true;
                                return false;
                            }),alternative.end());
                            alternative.append(retained);for(auto& word:alternative)word.ocrLine=0;
                        }
                        words=alternative;
                    }
                }
            }
        }
        if(retryEmpty&&words.isEmpty()&&!cancel->load()&&source.width()<=640&&source.height()<=240){
            // Very short labels can disappear when padding dilutes the text.
            // Retry the original framing before escalating magnification.
            words=read(engine,maxScale,0);
            if(words.isEmpty()&&english&&!cancel->load())words=read(english,maxScale,0);
            if(words.isEmpty()&&!normalized.isNull()&&!cancel->load()){
                pixels=&source;words=read(engine,maxScale,0);
                if(words.isEmpty()&&english&&!cancel->load())words=read(english,maxScale,0);
                pixels=&normalized;
            }
        }
        if(retryEmpty&&words.isEmpty()&&!cancel->load()&&source.width()<=640&&source.height()<=240){
            auto scale=std::max(maxScale,4.);
            words=read(engine,scale,32);
            if(words.isEmpty()&&english&&!cancel->load())words=read(english,scale,32);
            if(words.isEmpty()&&!cancel->load()){
                // Short antialiased labels may need stronger percentile stretching
                // than sparse text. Restrict it to an otherwise empty small crop.
                auto stronger=ocrContrast(source,100);
                if(!stronger.isNull()){
                    pixels=&stronger;words=read(engine,scale,32);
                    if(words.isEmpty()&&english&&!cancel->load())words=read(english,scale,32);
                    pixels=normalized.isNull()?&source:&normalized;
                }
            }
            if(words.isEmpty()&&!cancel->load()){
                // ClearType's colored edge pixels can make a very short word
                // disappear in the CJK recognizer, even after enlargement.
                // Otsu contrast recovery is used only after the normal passes
                // returned no words. Keep the image and returned geometry intact.
                auto clean=source.convertToFormat(QImage::Format_RGB32);quint64 histogram[256]{};
                for(int y=0;y<clean.height();++y)for(int x=0;x<clean.width();++x)++histogram[qGray(clean.pixel(x,y))];
                double sum=0;quint64 count=quint64(clean.width())*clean.height();
                for(int i=0;i<256;++i)sum+=double(i)*histogram[i];
                quint64 weight=0;double partial=0,best=0;int threshold=0;
                for(int i=0;i<255;++i){weight+=histogram[i];partial+=double(i)*histogram[i];if(!weight||weight==count)continue;
                    double difference=partial/weight-(sum-partial)/(count-weight),score=double(weight)*(count-weight)*difference*difference;
                    if(score>best){best=score;threshold=i;}}
                if(best>0){
                    bool light=qGray(clean.pixel(clean.width()-1,clean.height()-1))>threshold;
                    for(int y=0;y<clean.height();++y){auto row=reinterpret_cast<QRgb*>(clean.scanLine(y));for(int x=0;x<clean.width();++x)row[x]=((qGray(row[x])>threshold)==light)?qRgb(255,255,255):qRgb(0,0,0);}
                    pixels=&clean;words=read(engine,scale,32);
                    if(words.isEmpty()&&english&&!cancel->load())words=read(english,scale,32);
                }
            }
        }
        return cancel->load()?QList<Block>():grouped?screenshotBlocks(source,words,cancel):words;
    }catch(const winrt::hresult_error&){throw std::runtime_error("Windows OCR 识别失败；请检查语言包");}
}
namespace {
struct PointerRow {
    QList<Block> words;
    QRectF box;
    double height=0,baseline=0;
    void add(Block word){words<<word;box=box.united(word.box);}
    void measure(){
        QList<double> heights,bottoms;
        for(auto w:words)if(wordScript(w)){heights<<w.box.height();bottoms<<w.box.bottom();}
        if(heights.isEmpty()){height=box.height();baseline=box.bottom();return;}
        std::sort(heights.begin(),heights.end());std::sort(bottoms.begin(),bottoms.end());
        height=heights[heights.size()/2];baseline=bottoms[bottoms.size()/2];
    }
};
struct PointerLayout {
    QList<Block> words;
    QImage image;
    QPointF point;
    QList<QRectF> strips;
    QList<QPointF> offsets;
    Block restore(Block result) const {
        if(strips.isEmpty()||result.text.isEmpty())return result;
        QRectF physical;
        for(int i=0;i<strips.size();++i){auto part=result.box.intersected(strips[i]);
            if(!part.isEmpty())physical=physical.united(part.translated(-offsets[i]));
        }
        result.box=physical;return result;
    }
};
PointerLayout pointerLayout(const QList<Block>& words,QPointF point,const QImage* image)
{
    PointerLayout layout{words,image?*image:QImage(),point};
    auto parts=scriptWords(words);
    // Place letters before tiny baseline punctuation so a comma cannot seed
    // an extra row. Split large horizontal gaps before deciding paragraph flow.
    std::stable_sort(parts.begin(),parts.end(),[](auto a,auto b){return a.box.height()>b.box.height();});
    QList<PointerRow> bands;
    for(auto word:parts){
        if(word.text.trimmed().isEmpty()||word.box.isEmpty())continue;
        int at=-1;double nearest=1e9;
        for(int i=0;i<bands.size();++i){auto a=bands[i].box,b=word.box;
            double overlap=std::min(a.bottom(),b.bottom())-std::max(a.top(),b.top()),distance=std::abs(a.center().y()-b.center().y());
            bool baseline=!wordScript(word)&&b.top()>=a.top()-a.height()*.2&&b.bottom()<=a.bottom()+a.height()*.2;
            if((baseline||(overlap>=std::min(a.height(),b.height())*.5&&distance<=std::max(a.height(),b.height())*.6))&&distance<nearest){at=i;nearest=distance;}
        }
        if(at<0){bands.append(PointerRow{});at=int(bands.size())-1;}bands[at].add(word);
    }
    QList<PointerRow> rows;
    for(auto band:bands){
        std::sort(band.words.begin(),band.words.end(),[](auto a,auto b){return a.box.left()<b.box.left();});
        PointerRow row;
        for(auto word:band.words){
            if(!row.words.isEmpty()){
                auto previous=row.words.back();auto a=previous.box,b=word.box;
                double h=wordScript(previous)&&wordScript(word)?std::min(a.height(),b.height()):std::max(a.height(),b.height());
                if(b.left()-a.right()>std::max(12.,h*1.4)&&!(image&&inkBridges(*image,a,b))){row.measure();rows<<row;row={};}
            }row.add(word);
        }
        if(!row.words.isEmpty()){row.measure();rows<<row;}
    }
    std::sort(rows.begin(),rows.end(),[](auto a,auto b){return a.box.top()==b.box.top()?a.box.left()<b.box.left():a.box.top()<b.box.top();});
    int at=-1;double nearest=1e9;
    auto seed=wordAt(parts,point);
    for(int i=0;i<rows.size();++i){
        auto b=rows[i].box;
        if(!seed.text.isEmpty()){
            for(auto w:rows[i].words)if(w.box==seed.box&&w.text==seed.text){at=i;break;}
            if(at>=0)break;
        }else if(b.adjusted(-rows[i].height*2,-3,rows[i].height*2,3).contains(point)){
            double distance=std::abs(point.y()-b.center().y());if(distance<nearest){at=i;nearest=distance;}
        }
    }
    if(at<0)return layout;
    auto wraps=[&](int upper,int lower){
        const auto& a=rows[upper];const auto& b=rows[lower];double h=std::min(a.height,b.height);
        double gap=b.box.top()-a.box.bottom();
        if(h<=0||std::max(a.height,b.height)>h*1.5||gap<0||gap>h*1.25||
            std::abs(a.box.left()-b.box.left())>std::max(8.,h*.8)||a.box.width()<h*6||
            b.box.right()>a.box.right()+h*2)return false;
        auto end=a.words.back().text.trimmed();
        while(!end.isEmpty()&&QString("\"'”’）)]》」』").contains(end.back()))end.chop(1);
        if(!end.isEmpty()&&QString(".!?。！？;；:：").contains(end.back()))return false;
        QString start;for(auto w:b.words){start+=w.text+' ';if(start.size()>16)break;}
        if(QRegularExpression("^\\s*(?:[-*•●▪]|\\d+[.)、]|[（(]\\d+[)）])").match(start).hasMatch())return false;
        // Never jump over a visible intervening row in this text column.
        for(int i=0;i<rows.size();++i)if(i!=upper&&i!=lower){auto r=rows[i].box;
            if(r.center().y()>a.box.center().y()+h*.4&&r.center().y()<b.box.center().y()-h*.4&&
                std::min(r.right(),a.box.right())>std::max(r.left(),a.box.left()))return false;
        }return true;
    };
    QList<int> chain{at};
    for(int pass=0;pass<3;++pass){
        bool added=false;
        for(bool before:{true,false}){
            if(chain.size()>=4)break;
            int endpoint=before?chain.front():chain.back(),candidate=-1;double distance=1e9;
            for(int i=0;i<rows.size();++i)if(!chain.contains(i)&&(before?wraps(i,endpoint):wraps(endpoint,i))){
                double d=std::abs(rows[i].baseline-rows[endpoint].baseline);if(d<distance){candidate=i;distance=d;}
            }
            if(candidate>=0){if(before)chain.prepend(candidate);else chain.append(candidate);added=true;}
        }if(!added)break;
    }
    if(chain.size()==1)return layout;
    double height=0;for(int i:chain)height=std::max(height,rows[i].height);
    double x=4,baseline=std::ceil(height*1.5)+4;
    layout.words.clear();
    for(int i:chain){
        const auto& row=rows[i];QPointF offset(qRound(x-row.box.left()),qRound(baseline-row.baseline));
        auto strip=row.box.adjusted(-2,-2,2,2).toAlignedRect();
        layout.strips<<QRectF(strip).translated(offset);layout.offsets<<offset;
        for(auto word:row.words){word.box.translate(offset);layout.words<<word;}
        if(i==at)layout.point=point+offset;
        x=row.box.right()+offset.x()+std::max(5.,height*.5);
    }
    if(image){
        layout.image=QImage(qCeil(x+4),qCeil(height*3+8),QImage::Format_RGB32);
        auto sample=rows[chain.front()].box.topLeft().toPoint()-QPoint(2,2);
        sample.setX(std::clamp(sample.x(),0,image->width()-1));sample.setY(std::clamp(sample.y(),0,image->height()-1));
        layout.image.fill(image->pixelColor(sample));QPainter painter(&layout.image);
        for(int i=0;i<chain.size();++i){auto strip=layout.strips[i].translated(-layout.offsets[i]).toAlignedRect().intersected(image->rect());
            painter.drawImage(QPointF(strip.topLeft())+layout.offsets[i],*image,strip);
        }
    }
    return layout;
}
Block recognizeLineAt(const QImage& image,const QList<Block>& words,Cancel cancel,QPointF point)
{
    if(cancel->load())return {};
    auto parts=scriptWords(words);auto seed=wordAt(parts,point);
    const int latin=int(QChar::Script_Latin)+1;
    if(seed.text.isEmpty()){
        // A dropped letter has no OCR box. Use nearby ink to seed a retry,
        // but still require the refined result to cover the original pointer.
        double nearest=1e9;
        for(auto w:parts){
            auto b=w.box;double gap=std::max(b.left()-point.x(),point.x()-b.right());
            if(wordScript(w)!=latin||gap<0||gap>b.height()*6||gap>=nearest||
                point.y()<b.top()-b.height()*.2||point.y()>b.bottom()+b.height()*.2)continue;
            QRectF target(point.x()-3,b.top(),6,b.height());
            int ink=0;
            for(int x=std::max(0,int(target.left()));x<std::min(image.width(),int(target.right()));++x){
                int low=255,high=0;
                for(int y=std::max(0,int(b.top())-2);y<std::min(image.height(),int(b.bottom())+2);++y){
                    int value=qGray(image.pixel(x,y));low=std::min(low,value);high=std::max(high,value);
                }ink+=high-low>=48;
            }
            if(ink<2)continue;
            if(gap<=b.height()*1.4||(point.x()<b.left()?inkBridges(image,target,b):inkBridges(image,b,target))){seed=w;nearest=gap;}
        }
    }
    if(!seed.text.isEmpty()&&!wordScript(seed)){
        Block before,after;
        for(auto w:parts){
            if(!wordScript(w)||std::abs(w.box.center().y()-seed.box.center().y())>w.box.height()*.6)continue;
            if(w.box.right()<=seed.box.left()&&(before.text.isEmpty()||w.box.right()>before.box.right()))before=w;
            if(w.box.left()>=seed.box.right()&&(after.text.isEmpty()||w.box.left()<after.box.left()))after=w;
        }
        if(wordScript(before)==latin&&wordScript(after)==latin&&
            seed.box.left()-before.box.right()<before.box.height()*1.4&&
            after.box.left()-seed.box.right()<after.box.height()*1.4)seed=before;
        else if(wordScript(after)==latin&&after.box.left()-seed.box.right()<after.box.height()*1.4)seed=after;
        else if(wordScript(before)==latin&&seed.box.left()-before.box.right()<before.box.height()*1.4)seed=before;
    }
    auto original=[&]{return phraseAt(words,point,&image);};
    if(wordScript(seed)!=latin||seed.box.height()>36)return original();
    // Small Latin letters are easily dropped or read as punctuation by the
    // system's CJK recognizer. Locate their ink before applying text/sentence
    // boundaries, then read just this local run at a useful glyph resolution.
    double h=seed.box.height(),left=std::max(0.,seed.box.left()-600.),right=std::min(double(image.width()),seed.box.right()+600.);
    for(auto word:parts){
        auto a=seed.box,b=word.box;
        if(std::min(a.bottom(),b.bottom())-std::max(a.top(),b.top())<std::min(a.height(),b.height())*.5||
            std::abs(a.center().y()-b.center().y())>std::max(a.height(),b.height())*.6)continue;
        int script=wordScript(word);
        if(script&&script!=latin){
            if(b.right()<=a.left())left=std::max(left,b.right()+1);
            if(b.left()>=a.right())right=std::min(right,b.left()-1);
        }
        if(script==latin&&b.height()<=seed.box.height()*1.8)h=std::max(h,b.height());
    }
    int top=std::max(0,int(std::floor(seed.box.center().y()-h*.8)));
    int bottom=std::min(image.height(),int(std::ceil(seed.box.center().y()+h*.8)));
    auto pixels=image.convertToFormat(QImage::Format_RGB32);
    auto ink=[&](int x){
        int low=255,high=0;
        for(int y=top;y<bottom;++y){auto value=qGray(reinterpret_cast<const QRgb*>(pixels.constScanLine(y))[x]);low=std::min(low,value);high=std::max(high,value);}
        return high-low>=48;
    };
    auto extent=[&](int start,int direction){
        int last=start,blank=0,limit=std::max(12,qRound(h*1.4));
        for(int x=start;x>=int(std::ceil(left))&&x<int(std::floor(right));x+=direction){
            if(ink(x)){last=x;blank=0;}else if(++blank>limit)break;
        }return last;
    };
    int x1=extent(std::clamp(int(seed.box.left()),0,image.width()-1),-1);
    int x2=extent(std::clamp(int(seed.box.right()),0,image.width()-1),1);
    int pad=std::max(3,qRound(h*.4));
    x1=std::max(int(std::ceil(left)),x1-pad);x2=std::min(int(std::floor(right)),x2+pad+1);
    QRect area(x1,top,x2-x1,bottom-top);
    if(area.isEmpty())return original();
    QList<Block> refined;
    try{refined=recognize(image.copy(area),cancel,false,std::clamp(std::ceil(96./h),2.,8.),false);}
    catch(const std::exception&){return cancel->load()?Block():original();}
    if(cancel->load())return {};
    int oldLetters=0,newLetters=0;QRectF oldBounds,newBounds;
    auto letters=[](const QString& text){int n=0;for(auto c:text)n+=c.isLetterOrNumber();return n;};
    for(auto w:parts)if(area.contains(w.box.center().toPoint())&&letters(w.text)){oldLetters+=letters(w.text);oldBounds=oldBounds.united(w.box);}
    for(auto& w:refined){
        w.box.translate(area.topLeft());
        // A Latin-only crop must not introduce new CJK/script boundaries.
        for(auto c:w.text)if(auto script=letterScript(c);script&&script!=latin)return original();
    }
    // Refinement fills lost letters and punctuation-shaped words. It must not
    // erase a readable word or change its spelling/case just because a larger
    // rendering confused the OCR model. Keep coarse words whose physical span
    // did not grow; genuinely recovered/merged words retain the refined text.
    for(auto original:parts){
        if(wordScript(original)!=latin||letters(original.text)<2||!area.contains(original.box.center().toPoint()))continue;
        QList<int> matches;QRectF bounds;
        for(int i=0;i<refined.size();++i){auto b=refined[i].box;
            if(b.intersected(original.box).width()>=std::min(b.width(),original.box.width())*.5){matches<<i;bounds=bounds.united(b);}
        }
        if(matches.isEmpty()||bounds.left()<original.box.left()-h*.3||bounds.right()>original.box.right()+h*.3)continue;
        for(auto it=matches.crbegin();it!=matches.crend();++it)refined.removeAt(*it);
        refined<<original;
    }
    for(auto w:refined){newLetters+=letters(w.text);if(letters(w.text))newBounds=newBounds.united(w.box);}
    if(refined.isEmpty()||newLetters<oldLetters||
        newBounds.left()>oldBounds.left()+h*.4||newBounds.right()<oldBounds.right()-h*.4)
        return original();
    QList<Block> merged;
    for(auto w:parts)if(!area.contains(w.box.center().toPoint()))merged<<w;
    merged.append(refined);
    auto phrase=phraseAt(merged,point,&image);
    return phrase.text.isEmpty()?original():phrase;
}
}
Block wrappedPhraseAt(const QList<Block>& words,QPointF point,const QImage* image)
{
    auto layout=pointerLayout(words,point,image);
    return layout.restore(phraseAt(layout.words,layout.point,image?&layout.image:nullptr));
}
Block recognizeAt(const QImage& image,Cancel cancel,QPointF point)
{
    auto read=[&](const QImage& pixels,bool recover){
        auto words=recognize(pixels,cancel,false,2.,recover);
        if(cancel->load())return Block();
        auto layout=pointerLayout(words,point,&pixels);
        return layout.restore(recognizeLineAt(layout.image,layout.words,cancel,layout.point));
    };
    auto original=read(image,false);if(cancel->load())return {};
    auto corrected=ocrContrast(image);
    if(corrected.isNull())return original;
    auto recovered=read(corrected,true);if(cancel->load())return {};
    // Keep the original phrase if the alternate image loses content.
    if(recovered.text.size()>=original.text.size()&&!recovered.text.isEmpty())return recovered;
    return original;
}
Selection selectionAt(POINT point,HWND target,int warmupMs)
{
    QElapsedTimer timing;timing.start();
    auto trace=[&](const char* phase){
        if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE")){
            std::fprintf(stderr,"[selection] %lldms %s\n",timing.elapsed(),phase);std::fflush(stderr);
        }
    };
    // UI Automation providers can block inside another application's COM
    // server. Production calls run in a disposable selection worker.
    static std::atomic_bool reading=false;
    bool idle=false;
    if(!reading.compare_exchange_strong(idle,true))throw std::runtime_error("上一次划词读取尚未结束，请稍后重试");
    struct ReadGuard{std::atomic_bool& flag;~ReadGuard(){flag=false;}} guard{reading};
    HWND focus=target;
    if(!focus){
        GUITHREADINFO info{sizeof info};auto foreground=GetForegroundWindow();
        if(foreground&&GetGUIThreadInfo(GetWindowThreadProcessId(foreground,nullptr),&info))focus=info.hwndFocus;
    }
    if(focus){
        DWORD pid=0;GetWindowThreadProcessId(focus,&pid);
        wchar_t name[128]{};GetClassNameW(focus,name,128);auto className=QString::fromWCharArray(name);
        bool edit=className.compare("Edit",Qt::CaseInsensitive)==0;
        bool rich=className.startsWith("RichEdit",Qt::CaseInsensitive);
        // Standard EDIT controls can lack TextPattern. Read their public
        // selection messages without copying, changing focus or sending keys.
        if((target||pid!=GetCurrentProcessId())&&(edit||rich)&&!(GetWindowLongPtrW(focus,GWL_STYLE)&ES_PASSWORD)){
            DWORD start=0,end=0;DWORD_PTR result=0;
            auto send=[&](UINT msg,WPARAM w,LPARAM l){
                return SendMessageTimeoutW(focus,msg,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,250,&result)!=0;
            };
            if(send(EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end))&&end>start&&
                send(WM_GETTEXTLENGTH,0,0)&&result>0&&result<INT_MAX){
                std::vector<wchar_t> text(size_t(result)+1);
                if(send(WM_GETTEXT,text.size(),reinterpret_cast<LPARAM>(text.data()))){
                    auto selected=QString::fromWCharArray(text.data(),int(result)).mid(start,end-start).trimmed();
                    if(!selected.isEmpty()){
                        RECT client{};GetClientRect(focus,&client);POINT origin{};ClientToScreen(focus,&origin);
                        QRect area(origin.x,origin.y,client.right,client.bottom),endArea;
                        if(edit&&send(EM_POSFROMCHAR,start,0)){
                            POINT first{GET_X_LPARAM(result),GET_Y_LPARAM(result)};
                            if(send(EM_POSFROMCHAR,end-1,0)){
                                POINT last{GET_X_LPARAM(result),GET_Y_LPARAM(result)};
                                int h=std::max(16,int(GetDpiForWindow(focus)*20/96));
                                if(first.x>=0&&first.y>=0&&last.x>=0&&last.y>=0){
                                    endArea=QRect(origin.x+last.x,origin.y+last.y,h,h);
                                    area=first.y==last.y?QRect(origin.x+first.x,origin.y+first.y,std::max(h,int(last.x-first.x)+h),h):
                                        QRect(origin.x,origin.y+first.y,client.right,last.y-first.y+h);
                                }
                            }
                        }
                        return {selected,area,endArea};
                    }
                }
            }
        }
    }
    trace("create automation");
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool initialized=SUCCEEDED(hr);
    struct Cleanup{bool initialized;~Cleanup(){if(initialized)CoUninitialize();}} cleanup{initialized};
    if(auto result=firefoxSelection(target?target:GetForegroundWindow());!result.text.isEmpty())return result;
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation2> automation2;ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationTreeWalker> walker;
    if(SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation8),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation2)))){
        automation2->put_ConnectionTimeout(300);automation2->put_TransactionTimeout(300);automation=automation2;
    }else if(FAILED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))))return {};
    automation->get_RawViewWalker(&walker);
    auto selected=[&](IUIAutomationElement* element)->Selection {
            if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE")){
                BSTR description=nullptr;
                if(SUCCEEDED(element->get_CurrentProviderDescription(&description))&&description){
                    std::fprintf(stderr,"[selection] provider %s\n",qPrintable(QString::fromWCharArray(description,SysStringLen(description))));
                    SysFreeString(description);
                }
            }
            BOOL password=FALSE;
            if(FAILED(element->get_CurrentIsPassword(&password))||password)return {};
            int pid=0;element->get_CurrentProcessId(&pid);
            if(!target&&pid==int(GetCurrentProcessId()))return {};
            ComPtr<IUIAutomationTextPattern> pattern;ComPtr<IUIAutomationTextRangeArray> ranges;
            if(SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern)))&&pattern&&
                SUCCEEDED(pattern->GetSelection(&ranges))&&ranges){
                int length=0;ranges->get_Length(&length);QStringList pieces;QRectF bounds,endArea;
                for(int i=0;i<length;++i){
                    ComPtr<IUIAutomationTextRange> range;BSTR text=nullptr;
                    if(FAILED(ranges->GetElement(i,&range))||!range)continue;
                    trace("selected range text");
                    if(SUCCEEDED(range->GetText(-1,&text))&&text){
                        auto value=QString::fromWCharArray(text,SysStringLen(text));SysFreeString(text);
                        if(value.trimmed().isEmpty())continue;pieces<<value;
                        // Asking a browser for every rectangle of a cross-page
                        // range can stall its layout. Text retrieval must not
                        // depend on laying out thousands of selected lines.
                        ComPtr<IUIAutomationTextRange> tail;int moved=0;
                        range->Clone(&tail);
                        if(tail){
                            tail->MoveEndpointByRange(TextPatternRangeEndpoint_Start,range.Get(),TextPatternRangeEndpoint_End);
                            tail->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,TextUnit_Character,-1,&moved);
                        }
                        trace("selected range endpoint");
                        SAFEARRAY* rectangles=nullptr;
                        if(tail&&SUCCEEDED(tail->GetBoundingRectangles(&rectangles))&&rectangles){
                            LONG lower=0,upper=-1;double* data=nullptr;
                            SafeArrayGetLBound(rectangles,1,&lower);SafeArrayGetUBound(rectangles,1,&upper);
                            if(SUCCEEDED(SafeArrayAccessData(rectangles,reinterpret_cast<void**>(&data)))){
                                for(LONG j=0;j+3<=upper-lower;j+=4){
                                    QRectF b(data[j],data[j+1],data[j+2],data[j+3]);
                                    if(!b.isEmpty()){bounds=bounds.isEmpty()?b:bounds.united(b);endArea=b;}
                                }SafeArrayUnaccessData(rectangles);
                            }SafeArrayDestroy(rectangles);
                        }
                    }
                }
                if(!pieces.isEmpty()){
                    BOOL focused=FALSE;element->get_CurrentHasKeyboardFocus(&focused);
                    // A page can retain a selection while an unrelated editor
                    // has keyboard focus. Never send copy keys to that editor.
                    return {pieces.join('\n').trimmed(),bounds.toAlignedRect(),endArea.toAlignedRect(),false,bool(focused)};
                }
            }
            return {};
    };
    // Chromium's focused native child can expose only the web-view host. The
    // selection belongs to its descendant Document/TextPattern, not an ancestor.
    HWND root=target?GetAncestor(target,GA_ROOT):GetForegroundWindow();
    // Browsers can publish their accessibility tree after the first query.
    // Keep this COM client alive while it warms up instead of tearing down a
    // fresh worker after an empty result. The parent still owns the hard timeout.
    QElapsedTimer warmup;warmup.start();
    for(int attempt=0;;++attempt){
    ComPtr<IUIAutomationElement> rootElement;
    trace("root element");
    if(root)automation->ElementFromHandle(root,&rootElement);
    if(rootElement){
        // Start with the document rather than hit-testing a potentially
        // offscreen, stale or unrelated focused link after focus returns.
        trace("document selections");
        VARIANT type{};type.vt=VT_I4;type.lVal=UIA_DocumentControlTypeId;
        ComPtr<IUIAutomationCondition> condition;ComPtr<IUIAutomationElementArray> documents;
        if(SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&condition))&&
            SUCCEEDED(rootElement->FindAll(TreeScope_Descendants,condition.Get(),&documents))&&documents){
            int count=0;documents->get_Length(&count);
            for(int i=0;i<count;++i){
                ComPtr<IUIAutomationElement> document;documents->GetElement(i,&document);
                if(document)if(auto result=selected(document.Get());!result.text.isEmpty())return result;
            }
        }
    }
    for(int route=0;route<3;++route){
        trace(route==0?"target route":route==1?"point route":"focus route");
        ComPtr<IUIAutomationElement> element;
        if(route==0&&target)automation->ElementFromHandle(target,&element);
        else if(route==1&&GetAncestor(WindowFromPoint(point),GA_ROOT)==root)automation->ElementFromPoint(point,&element);
        else if(route==2&&GetAncestor(GetForegroundWindow(),GA_ROOT)==root)automation->GetFocusedElement(&element);
        for(int depth=0;element&&depth<12;++depth){
            if(auto result=selected(element.Get());!result.text.isEmpty())return result;
            BOOL isRoot=FALSE;
            if(rootElement&&SUCCEEDED(automation->CompareElements(element.Get(),rootElement.Get(),&isRoot))&&isRoot)break;
            ComPtr<IUIAutomationElement> parent;
            if(!walker||FAILED(walker->GetParentElement(element.Get(),&parent)))break;
            element=parent;
        }
    }
    if(rootElement){
        trace("descendant text patterns");
        VARIANT available{};available.vt=VT_BOOL;available.boolVal=VARIANT_TRUE;
        ComPtr<IUIAutomationCondition> condition;
        ComPtr<IUIAutomationElementArray> containers;
        if(SUCCEEDED(automation->CreatePropertyCondition(UIA_IsTextPatternAvailablePropertyId,available,&condition))&&
            SUCCEEDED(rootElement->FindAll(TreeScope_Descendants,condition.Get(),&containers))&&containers){
            int count=0;containers->get_Length(&count);
            trace("read descendant selections");
            for(int i=0;i<std::min(count,128);++i){
                ComPtr<IUIAutomationElement> element;containers->GetElement(i,&element);
                if(element)if(auto result=selected(element.Get());!result.text.isEmpty())return result;
            }
        }
    }
    if(attempt>=3||warmup.elapsed()>=warmupMs||!IsWindow(root))break;
    const auto delay=std::min<qint64>(80*(attempt+1),warmupMs-warmup.elapsed());
    if(delay<=0)break;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    trace("retry after accessibility warmup");
    }
    trace("empty selection");
    return {};
}
HWND selectionTarget()
{
    auto foreground=GetForegroundWindow();DWORD pid=0;
    if(!foreground)return nullptr;
    GetWindowThreadProcessId(foreground,&pid);
    if(pid==GetCurrentProcessId())return nullptr;
    GUITHREADINFO info{sizeof info};
    if(GetGUIThreadInfo(GetWindowThreadProcessId(foreground,nullptr),&info)&&info.hwndFocus)return info.hwndFocus;
    return foreground;
}
namespace {
const char* elevatedSelectionMessage="当前插件权限不足。请以管理员身份启动 WinShun，在设置 → 插件 → 截图翻译中允许新增权限并确认管理员副本，然后重新按划词热键。也可使用鼠标 OCR。";
const char* restrictedSelectionMessage="目标程序限制了选区访问。若它以管理员权限运行，请以管理员身份启动 WinShun，并在设置 → 插件 → 截图翻译中确认管理员权限；仍无法读取时可使用鼠标 OCR。";
DWORD integrityOf(HANDLE token)
{
    alignas(TOKEN_MANDATORY_LABEL) BYTE bytes[256]{};DWORD size=0;
    if(!GetTokenInformation(token,TokenIntegrityLevel,bytes,sizeof bytes,&size))return 0;
    auto sid=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(bytes)->Label.Sid;
    if(!IsValidSid(sid))return 0;
    auto count=*GetSidSubAuthorityCount(sid);
    return count?*GetSidSubAuthority(sid,count-1):0;
}
QString selectionAccessProblem(HWND target)
{
    DWORD pid=0;GetWindowThreadProcessId(target,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return GetLastError()==ERROR_ACCESS_DENIED?"target-access-denied":"";
    HANDLE other=nullptr,own=nullptr;
    QString problem;
    if(!OpenProcessToken(process,TOKEN_QUERY,&other)){
        if(GetLastError()==ERROR_ACCESS_DENIED)problem="target-access-denied";
    }else if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&own)){
        DWORD uiAccess=0,size=0;
        GetTokenInformation(own,TokenUIAccess,&uiAccess,sizeof uiAccess,&size);
        DWORD source=integrityOf(other),reader=integrityOf(own);
        if(source&&reader&&source>reader&&!uiAccess)problem="target-elevated";
    }
    if(own)CloseHandle(own);if(other)CloseHandle(other);CloseHandle(process);
    return problem;
}
}
Selection isolatedSelection(POINT point,HWND target,Cancel cancel,int timeoutMs)
{
    if(!target||!IsWindow(target)||cancel->load())return {};
    QProcess worker;
    worker.setProcessChannelMode(QProcess::SeparateChannels);
    wchar_t desktop[256]{};DWORD bytes=0;
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof desktop,&bytes);
    worker.setCreateProcessArgumentsModifier([&](QProcess::CreateProcessArguments* args){
        args->flags|=CREATE_NO_WINDOW;
        auto startup=reinterpret_cast<STARTUPINFOW*>(args->startupInfo);
        startup->lpDesktop=desktop;startup->dwFlags|=STARTF_USESHOWWINDOW;startup->wShowWindow=SW_HIDE;
    });
    worker.start(QCoreApplication::applicationDirPath()+"/ScreenshotTranslate.exe",{"--selection-worker"});
    if(!worker.waitForStarted(800))throw std::runtime_error("无法启动选区读取，请重试");
    QJsonObject request{{"x",int(point.x)},{"y",int(point.y)},{"target",QString::number(quintptr(target))},
        {"warmupMs",std::clamp(timeoutMs-500,0,600)}};
    worker.write(QJsonDocument(request).toJson(QJsonDocument::Compact)+"\n");worker.closeWriteChannel();
    QElapsedTimer elapsed;elapsed.start();
    while(!worker.waitForFinished(25)){
        if(cancel->load()||elapsed.elapsed()>=timeoutMs){
            worker.kill();worker.waitForFinished(500);
            if(cancel->load())return {};
            throw std::runtime_error("选区读取超时，已释放读取任务，请重试或使用鼠标 OCR");
        }
    }
    if(cancel->load())return {};
    QJsonParseError parse{};auto response=QJsonDocument::fromJson(worker.readAllStandardOutput(),&parse).object();
    if(response.value("error").toString()=="target-elevated")throw std::runtime_error(elevatedSelectionMessage);
    if(response.value("error").toString()=="target-access-denied")throw std::runtime_error(restrictedSelectionMessage);
    if(worker.exitStatus()!=QProcess::NormalExit||worker.exitCode()!=0||parse.error!=QJsonParseError::NoError||
        !response.contains("text")||!response.value("error").toString().isEmpty())throw std::runtime_error("无法读取选区，请重试或使用鼠标 OCR");
    auto box=response.value("area").toArray(),end=response.value("endArea").toArray();
    return {response.value("text").toString(),box.size()==4?QRect(box[0].toInt(),box[1].toInt(),box[2].toInt(),box[3].toInt()):QRect(),
        end.size()==4?QRect(end[0].toInt(),end[1].toInt(),end[2].toInt(),end[3].toInt()):QRect(),false,
        response.value("copyShortcutEligible").toBool()};
}
namespace {
int selectionPrimer(HWND initial,bool watch)
{
    // Keep Firefox accessibility active before the user creates a selection.
    // A cold Firefox tree does not always backfill the existing selection;
    // repeated GetSelection calls cannot manufacture its missing event.
    // This helper only retains the document interface, never reads its text.
    std::thread([]{std::fgetc(stdin);ExitProcess(0);}).detach();
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool initialized=SUCCEEDED(hr);
    struct Cleanup{bool initialized;~Cleanup(){if(initialized)CoUninitialize();}} cleanup{initialized};
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation2> automation;
    if(FAILED(CoCreateInstance(__uuidof(CUIAutomation8),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))))return 3;
    automation->put_ConnectionTimeout(300);automation->put_TransactionTimeout(300);
    ComPtr<IUIAutomationElement> retained;HWND retainedWindow=nullptr;
    auto prime=[&](HWND window){
        auto root=GetAncestor(window,GA_ROOT);if(!root||(root==retainedWindow&&retained))return;
        wchar_t name[128]{};GetClassNameW(root,name,128);
        if(QString::fromWCharArray(name)!=QStringLiteral("MozillaWindowClass"))return;
        // Never reach across desktops (including from private QA into user tabs).
        wchar_t sourceDesktop[256]{},ownDesktop[256]{};DWORD size=0;
        auto thread=GetWindowThreadProcessId(root,nullptr);
        if(!GetUserObjectInformationW(GetThreadDesktop(thread),UOI_NAME,sourceDesktop,sizeof sourceDesktop,&size)||
            !GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,ownDesktop,sizeof ownDesktop,&size)||
            std::wcscmp(sourceDesktop,ownDesktop)!=0)return;
        ComPtr<IUIAutomationElement> element,document;ComPtr<IUIAutomationCondition> condition;
        if(FAILED(automation->ElementFromHandle(root,&element))||!element)return;
        VARIANT type{};type.vt=VT_I4;type.lVal=UIA_DocumentControlTypeId;
        if(SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&condition)))
            element->FindFirst(TreeScope_Descendants,condition.Get(),&document);
        retained=document?document:element;retainedWindow=root;
    };
    static thread_local std::function<void(HWND)> notify;
    notify=prime;
    auto hook=watch?SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,
        [](HWINEVENTHOOK,DWORD,HWND window,LONG,LONG,DWORD,DWORD){if(notify)notify(window);},
        0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS):nullptr;
    prime(initial?initial:GetForegroundWindow());
    auto heartbeat=[] {std::fputs("{\"ready\":true}\n",stdout);std::fflush(stdout);};
    heartbeat();auto timer=SetTimer(nullptr,0,1000,nullptr);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0){
        if(message.message==WM_TIMER)heartbeat();
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    if(timer)KillTimer(nullptr,timer);if(hook)UnhookWinEvent(hook);notify={};return 0;
}
}
int selectionWorker()
{
    QFile input;input.open(stdin,QIODevice::ReadOnly);
    auto request=QJsonDocument::fromJson(input.readLine(4096)).object();
    auto target=reinterpret_cast<HWND>(quintptr(request.value("target").toString().toULongLong()));
    if(request.value("prime").toBool())return selectionPrimer(target,request.value("watch").toBool());
    if(!target||!IsWindow(target))return 2;
    QJsonObject response;
    try{
        auto result=request.value("copyFirefox").toBool()?copyFirefoxSelection(target,std::make_shared<std::atomic_bool>(false)):selectionAt(POINT{request.value("x").toInt(),request.value("y").toInt()},target,
            std::clamp(request.value("warmupMs").toInt(600),0,600));
        // Some providers expose read-only text even across integrity levels.
        // Try the actual read first; report access restrictions only on failure.
        auto problem=result.text.isEmpty()?selectionAccessProblem(target):QString();
        if(!problem.isEmpty())response={{"error",problem}};
        else response={{"text",result.text},{"copyShortcutEligible",result.copyShortcutEligible},
            {"area",QJsonArray{result.area.x(),result.area.y(),result.area.width(),result.area.height()}},
            {"endArea",QJsonArray{result.endArea.x(),result.endArea.y(),result.endArea.width(),result.endArea.height()}}};
    }catch(...){response={{"error","read failed"}};}
    auto data=QJsonDocument(response).toJson(QJsonDocument::Compact);
    std::fwrite(data.constData(),1,size_t(data.size()),stdout);std::fflush(stdout);
    return 0;
}
QString selectedText()
{
    POINT point{};GetPhysicalCursorPos(&point);return selectionAt(point).text;
}
QImage textImage(const QString& text,int width,bool dark)
{
    width=std::clamp(width,320,1200);QTextDocument doc;doc.setPlainText(text);
    QFont font("Microsoft YaHei UI");font.setPixelSize(17);doc.setDefaultFont(font);doc.setTextWidth(width-32);
    int height=std::min(16000,std::max(96,qCeil(doc.size().height())+32));
    QImage image(width,height,QImage::Format_RGB32);image.fill(dark?QColor("#2B2B2B"):QColor("#FBFBFB"));
    QPainter p(&image);p.translate(16,16);QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text,dark?QColor("#F3F3F3"):QColor("#1B1B1B"));doc.documentLayout()->draw(&p,context);return image;
}
QImage translatedImage(const QImage& source,const QList<Block>& blocks,const QStringList& segments,bool dark,
    QList<ws::capture::RedactLine>* selectable)
{
    if(selectable)selectable->clear();
    QImage result=source.convertToFormat(QImage::Format_RGB32);QPainter p(&result);p.setRenderHint(QPainter::TextAntialiasing);
    for(int i=0;i<std::min(blocks.size(),segments.size());++i){
        auto block=blocks[i];if(numeric(block.text)||(segments[i]==block.text&&!block.text.contains('\n'))){
            if(selectable){
                ws::capture::RedactLine line;auto r=block.box;line.quad={r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()};line.text=block.text;
                for(int j=0;j<line.text.size();++j)line.chars<<qMakePair(j,float((j+.5)/line.text.size()));
                selectable->append(line);
            }
            continue;
        }
        QRect box=block.box.toAlignedRect().adjusted(-1,-1,1,2).intersected(result.rect());if(box.isEmpty())continue;
        double right=result.width(),bottom=result.height();
        for(int j=0;j<blocks.size();++j){
            if(i==j)continue;auto b=blocks[j].box;
            if(b.top()<block.box.bottom()&&b.bottom()>block.box.top()&&b.left()>=block.box.right())right=std::min(right,b.left()-5);
            if(b.left()<block.box.right()&&b.right()>block.box.left()&&b.top()>=block.box.bottom())bottom=std::min(bottom,b.top()-3);
        }
        QRect cell(box.x(),box.y(),std::max(1,qRound(std::min(right-block.box.x(),block.box.width()+std::max(12.,block.box.height())))),
            std::max(1,qRound(std::min(bottom-block.box.y(),std::max(26.,block.box.height()*2+6)))));
        cell=cell.intersected(result.rect());
        QList<QRgb> samples;
        for(int x=box.left();x<=box.right();x+=std::max(1,box.width()/20)){
            samples<<source.pixel(x,box.top())<<source.pixel(x,box.bottom());
        }
        std::sort(samples.begin(),samples.end(),[](QRgb a,QRgb b){return qGray(a)<qGray(b);});QColor background(samples[samples.size()/2]);
        // A short source label can become a wider translation. Use adjacent
        // empty space before wrapping, stopping at borders, icons and text.
        if(!block.text.contains('\n')){
            const double fontSize=sourceFontPixels(block);
            const int scanEnd=std::min(result.width(),qRound(std::min(right,block.box.right()+fontSize*8)));
            int safeRight=box.right()+1;
            for(int x=safeRight;x<scanEnd;++x){
                int occupied=0;
                for(int y=box.top();y<=box.bottom();++y){
                    const auto pixel=source.pixelColor(x,y);
                    const auto difference=std::max({std::abs(pixel.red()-background.red()),std::abs(pixel.green()-background.green()),std::abs(pixel.blue()-background.blue())});
                    if(difference>38)++occupied;
                }
                if(occupied>=2)break;
                safeRight=x+1;
            }
            // Never extend the paint area through a nearby graphical element.
            cell.setRight(std::max(box.right(),safeRight-3));
        }
        p.fillRect(box,background);p.setPen(qGray(background.rgb())>140?QColor("#1B1B1B"):QColor("#F5F5F5"));
        QFont font("Microsoft YaHei UI");
        const int originalSize=std::clamp(qRound(sourceFontPixels(block)),6,256);
        const int minimumSize=std::min(originalSize,12);
        // Draw and select from the very same glyph layout; never OCR the
        // translated picture again (that loses punctuation and small letters).
        QTextLayout layout;QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        if(!block.text.contains('\n')&&segments[i].size()<=80){
            QFont trial(font);trial.setPixelSize(minimumSize);
            if(QFontMetricsF(trial).horizontalAdvance(segments[i])<=cell.width())option.setWrapMode(QTextOption::NoWrap);
        }
        layout.setTextOption(option);layout.setText(segments[i]);QPointF origin;
        for(int size=originalSize;size>=minimumSize;--size){
            font.setPixelSize(size);layout.clearLayout();layout.setFont(font);
            const QFontMetricsF metrics(font);
            // OCR rectangles enclose visible ink, not the font's ascent/leading.
            // Align that ink with the original instead of adding another top gap.
            const auto ink=metrics.tightBoundingRect(segments[i].section('\n',0,0));
            origin={block.box.left(),block.box.top()-metrics.ascent()-ink.top()};
            layout.beginLayout();qreal y=0,width=0;
            while(true){
                auto line=layout.createLine();if(!line.isValid())break;
                line.setLineWidth(std::max(1.,cell.right()+1-origin.x()));line.setPosition(QPointF(0,y));
                y+=line.height();width=std::max(width,line.naturalTextWidth());
            }
            layout.endLayout();
            if(origin.y()+y-metrics.descent()<=cell.bottom()+1&&width<=cell.width())break;
        }
        p.save();p.setClipRect(cell);
        for(int j=0;j<layout.lineCount();++j)
            p.fillRect(layout.lineAt(j).naturalTextRect().translated(origin).intersected(cell),background);
        layout.draw(&p,origin);
        if(selectable)for(int j=0;j<layout.lineCount();++j){
            auto line=layout.lineAt(j);QRectF rect=line.naturalTextRect().translated(origin);rect=rect.intersected(cell);
            if(rect.isEmpty())continue;
            ws::capture::RedactLine pick;pick.text=segments[i].mid(line.textStart(),line.textLength());
            pick.quad={rect.topLeft(),rect.topRight(),rect.bottomRight(),rect.bottomLeft()};
            QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme,pick.text);int begin=0;
            for(int end=graphemes.toNextBoundary();end>=0;end=graphemes.toNextBoundary()){
                auto middle=(line.cursorToX(line.textStart()+begin)+line.cursorToX(line.textStart()+end))/2+origin.x();
                for(int k=begin;k<end;++k)pick.chars<<qMakePair(k,float((middle-rect.x())/rect.width()));
                begin=end;
            }
            selectable->append(pick);
        }
        p.restore();
    }return result;
}
QImage comparisonImage(const QImage& source,const QImage& translated,bool first,bool dark,bool chinese)
{
    int width=std::max(source.width(),translated.width()),height=source.height()+translated.height()+60;
    if(qint64(width)*height>24'000'000)throw std::runtime_error("对照图片过大，请缩小截图范围");
    QImage out(width,height,QImage::Format_RGB32);out.fill(dark?QColor("#2B2B2B"):QColor("#FBFBFB"));
    QPainter p(&out);p.setPen(dark?QColor("#B8B8B8"):QColor("#666666"));QFont f("Microsoft YaHei UI");f.setPixelSize(13);p.setFont(f);
    auto top=first?translated:source,bottom=first?source:translated;
    auto sourceTitle=chinese?"原文":"Source",translatedTitle=chinese?"译文":"Translation";
    p.drawText(12,21,first?translatedTitle:sourceTitle);p.drawImage(0,30,top);
    p.drawText(12,top.height()+51,first?sourceTitle:translatedTitle);p.drawImage(0,top.height()+60,bottom);return out;
}
Selector::Selector(Done done,std::function<QImage(const QRect&)> grab):m_done(std::move(done)),m_grab(std::move(grab)){}
Selector::~Selector(){cancel();}
Selector::Surface::~Surface()
{
    frame={};
    if(dc&&previous)SelectObject(dc,previous);
    if(bitmap)DeleteObject(bitmap);
    if(dc)DeleteDC(dc);
}
void Selector::Surface::createFrame()
{
    dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;
    auto info=bitmapInfo(rect.width(),rect.height());
    bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc||!bitmap)throw std::runtime_error("无法创建截图画面");
    previous=SelectObject(dc,bitmap);
    frame=QImage(static_cast<uchar*>(pixels),rect.width(),rect.height(),rect.width()*4,QImage::Format_RGB32);
    compose({},frame.rect());
}
void Selector::Surface::compose(const QRect& selection,const QRect& dirty)
{
    QPainter painter(&frame);painter.setClipRect(dirty);
    painter.drawImage(QPoint(),dim);
    auto local=selection.intersected(rect).translated(-rect.topLeft());
    if(!local.isEmpty()){
        painter.drawImage(local.topLeft(),image,local);
        QPen border(QColor(0,120,212));border.setWidth(2);painter.setPen(border);
        painter.setBrush(Qt::NoBrush);painter.drawRect(local.adjusted(0,0,-1,-1));
    }
}
bool Selector::show()
{
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ScreenshotTranslate.Select";wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    RegisterClassW(&wc);
    std::vector<QRect> rects;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT r,LPARAM data)->BOOL{
        reinterpret_cast<std::vector<QRect>*>(data)->emplace_back(r->left,r->top,r->right-r->left,r->bottom-r->top);return TRUE;
    },reinterpret_cast<LPARAM>(&rects));
    try{
        for(auto rect:rects){
            auto s=std::make_unique<Surface>();s->rect=rect;s->owner=this;s->image=m_grab(rect);s->dim=s->image.copy();
            QPainter p(&s->dim);p.fillRect(s->dim.rect(),QColor(0,0,0,88));p.end();
            s->createFrame();m_surfaces.push_back(std::move(s));
        }
        for(auto& s:m_surfaces){
            s->hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"ScreenshotTranslate.Select",L"截图翻译 · 框选",WS_POPUP,
                s->rect.x(),s->rect.y(),s->rect.width(),s->rect.height(),nullptr,nullptr,wc.hInstance,s.get());
            if(!s->hwnd)throw std::runtime_error("无法创建截图蒙版");
            m_windows.push_back(s->hwnd);
        }
        for(auto w:m_windows)ShowWindow(w,SW_SHOWNOACTIVATE);
        if(!m_windows.empty()){SetForegroundWindow(m_windows[0]);SetFocus(m_windows[0]);}
        return !m_windows.empty();
    }catch(...){cancel();throw;}
}
LRESULT CALLBACK Selector::proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l)
{
    auto s=reinterpret_cast<Surface*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){s=static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcW(hwnd,msg,w,l);
    if(msg==WM_PAINT){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);
        auto r=ps.rcPaint;
        BitBlt(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,s->dc,r.left,r.top,SRCCOPY);
        EndPaint(hwnd,&ps);return 0;
    }
    if(msg==WM_PRINTCLIENT){
        BitBlt(reinterpret_cast<HDC>(w),0,0,s->rect.width(),s->rect.height(),s->dc,0,0,SRCCOPY);return 0;
    }
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_DPICHANGED)return 0; // physical monitor dimensions always remain exact
    if(msg==WM_KEYDOWN&&w==VK_ESCAPE){s->owner->finish({});return 0;}
    if(msg==WM_RBUTTONDOWN){s->owner->finish({});return 0;}
    if(msg==WM_LBUTTONDOWN||msg==WM_MOUSEMOVE||msg==WM_LBUTTONUP){
        POINT point{};GetPhysicalCursorPos(&point);
        if(msg==WM_LBUTTONDOWN)SetCapture(hwnd);
        if(msg==WM_LBUTTONUP)ReleaseCapture();
        s->owner->input(msg,point);return 0;
    }return DefWindowProcW(hwnd,msg,w,l);
}
void Selector::input(UINT msg,POINT point)
{
    if(m_closed)return;
    if(msg==WM_LBUTTONDOWN){m_start={point.x,point.y};m_drag=true;}
    if(!m_drag)return;
    auto old=m_selection;
    m_selection=QRect(m_start,QPoint(point.x,point.y)).normalized().intersected(virtualScreen());
    if(m_selection==old&&msg!=WM_LBUTTONUP)return;
    for(auto& s:m_surfaces){
        auto invalid=old.united(m_selection).adjusted(-3,-3,3,3).intersected(s->rect).translated(-s->rect.topLeft());
        if(invalid.isEmpty())continue;
        s->compose(m_selection,invalid);
        RECT r{invalid.x(),invalid.y(),invalid.x()+invalid.width(),invalid.y()+invalid.height()};InvalidateRect(s->hwnd,&r,FALSE);
    }
    if(msg==WM_LBUTTONUP)finish(m_selection.width()>=3&&m_selection.height()>=3?m_selection:QRect());
}
void Selector::finish(QRect rect)
{
    if(m_closed)return;
    QImage image;
    if(!rect.isEmpty()){
        image=QImage(rect.size(),QImage::Format_RGB32);image.fill(Qt::black);QPainter p(&image);
        for(auto& s:m_surfaces){
            auto part=s->rect.intersected(rect);if(!part.isEmpty())p.drawImage(part.topLeft()-rect.topLeft(),s->image,part.translated(-s->rect.topLeft()));
        }
    }
    cancel();auto done=m_done;
    QTimer::singleShot(0,qApp,[done,image,rect]{done(image,rect);});
}
void Selector::cancel()
{
    if(m_closed)return;m_closed=true;
    for(auto w:m_windows)DestroyWindow(w);m_windows.clear();m_surfaces.clear();
}
RawClicks::RawClicks(std::function<void(POINT,DWORD)> callback,std::function<void(POINT,DWORD,USHORT)> observer):m_callback(std::move(callback)),m_observer(std::move(observer))
{
    m_thread=std::thread([this]{
        m_id=GetCurrentThreadId();MSG msg{};PeekMessageW(&msg,nullptr,0,0,PM_NOREMOVE);
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ScreenshotTranslate.Raw";RegisterClassW(&wc);
        HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,this);m_hwnd=hwnd;
        RAWINPUTDEVICE mouse{1,2,RIDEV_INPUTSINK,hwnd};bool active=hwnd&&RegisterRawInputDevices(&mouse,1,sizeof mouse);
        if(active)while(!m_stop.load()&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(active){RAWINPUTDEVICE remove{1,2,RIDEV_REMOVE,nullptr};RegisterRawInputDevices(&remove,1,sizeof remove);}
        if(hwnd)DestroyWindow(hwnd);m_hwnd=nullptr;
    });
}
RawClicks::~RawClicks(){m_stop=true;if(auto id=m_id.load())PostThreadMessageW(id,WM_QUIT,0,0);if(m_thread.joinable())m_thread.join();}
LRESULT CALLBACK RawClicks::proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l)
{
    auto self=reinterpret_cast<RawClicks*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<RawClicks*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self&&msg==WM_INPUT){
        RAWINPUT raw{};UINT size=sizeof raw;
        if(self->m_read.load()(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))!=UINT(-1)&&raw.header.dwType==RIM_TYPEMOUSE&&
            raw.data.mouse.usButtonFlags){
            POINT point{};if(self->m_cursor.load()(&point)){
                auto flags=raw.data.mouse.usButtonFlags;auto time=DWORD(GetMessageTime());
                if(flags&(RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_RIGHT_BUTTON_DOWN|RI_MOUSE_MIDDLE_BUTTON_DOWN))self->m_callback(point,time);
                if(self->m_observer)self->m_observer(point,time,flags);
            }
        }
    }return DefWindowProcW(hwnd,msg,w,l);
}
}

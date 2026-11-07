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
#include <QCoreApplication>
#include <QFont>
#include <QPainter>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QAbstractTextDocumentLayout>
#include <QtMath>
#include <QTimer>
#include <QMap>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
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
QList<Block> groupWords(QList<Block> words)
{
    std::sort(words.begin(),words.end(),[](auto& a,auto& b){return a.box.y()==b.box.y()?a.box.x()<b.box.x():a.box.y()<b.box.y();});
    QList<QList<Block>> rows;
    for(auto word:words){
        if(word.text.isEmpty()||word.box.isEmpty())continue;
        bool found=false;
        for(auto& row:rows){
            auto b=row[0].box;
            if(std::min(b.bottom(),word.box.bottom())-std::max(b.top(),word.box.top())>=std::min(b.height(),word.box.height())*.5){
                row<<word;found=true;break;
            }
        }if(!found)rows.append(QList<Block>{word});
    }
    QList<Block> out;int paragraph=-1;
    for(auto row:rows){
        std::sort(row.begin(),row.end(),[](auto& a,auto& b){return a.box.x()<b.box.x();});
        QList<double> heights;for(auto w:row)heights<<w.box.height();std::sort(heights.begin(),heights.end());
        double h=heights[heights.size()/2],limit=std::max(14.,h*1.2);int gaps=0,latin=0,han=0;
        for(int i=0;i<row.size();++i){
            if(i&&row[i].box.x()-row[i-1].box.right()>limit)++gaps;
            for(auto c:row[i].text){latin+=(c.unicode()<128&&c.isLetter());han+=cjk(c);}
        }
        bool navigation=gaps>=2&&latin>han*3;
        QList<Block> blocks;Block run;Block prev;
        auto flush=[&]{if(!run.text.isEmpty()){run.glyphHeight=h;blocks<<run;run={};}};
        for(int i=0;i<row.size();++i){
            auto w=row[i];bool symbol=!QRegularExpression("[A-Za-z]").match(w.text).hasMatch();
            bool square=(w.box.width()>=h*.65&&w.box.width()<=h*1.6)||QRegularExpression("^[0丿丨]+$").match(w.text).hasMatch();
            if(navigation&&square&&symbol&&w.text.size()<=3&&i+1<row.size()&&
                QRegularExpression("^[A-Za-z]").match(row[i+1].text).hasMatch()&&(!i||w.box.x()-row[i-1].box.right()>h)){flush();continue;}
            if(!run.text.isEmpty()&&w.box.x()-prev.box.right()>limit)flush();
            if(run.text.isEmpty())run=w;
            else{
                if(!(cjk(run.text.back())&&cjk(w.text.front()))&&w.box.x()-prev.box.right()>std::max(1.,std::min(prev.box.height(),w.box.height())*.16))run.text+=" ";
                run.text+=w.text;run.box=run.box.united(w.box);
            }prev=w;
        }flush();
        if(blocks.size()==1){
            auto b=blocks[0];
            bool list=QRegularExpression("^(?:[-*]|\\d+[.)])\\s").match(b.text).hasMatch();
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
Block phraseAt(const QList<Block>& words,QPointF point)
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
        return b.left()-a.right()<=std::max(12.,height*1.4);
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
QList<Block> recognize(const QImage& source,Cancel cancel,bool grouped)
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
        int maxSide=int(OcrEngine::MaxImageDimension());double scale=std::min(2.0,double(maxSide)/std::max(source.width(),source.height()));
        QImage image=source.scaled(qRound(source.width()*scale),qRound(source.height()*scale),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<image.height();++y){auto px=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x)px[x]|=0xff000000;}
        auto size=uint32_t(image.sizeInBytes());winrt::Windows::Storage::Streams::Buffer buffer(size);
        std::memcpy(buffer.data(),image.constBits(),size);buffer.Length(size);
        SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8,image.width(),image.height(),BitmapAlphaMode::Premultiplied);bitmap.CopyFromBuffer(buffer);
        if(cancel->load())return {};
        auto result=engine.RecognizeAsync(bitmap).get();
        QList<Block> words;
        for(auto line:result.Lines())for(auto word:line.Words()){
            auto r=word.BoundingRect();words.append({from(word.Text()),QRectF(r.X/scale,r.Y/scale,r.Width/scale,r.Height/scale),r.Height/scale});
        }return cancel->load()?QList<Block>():grouped?groupWords(words):words;
    }catch(const winrt::hresult_error&){throw std::runtime_error("Windows OCR 识别失败；请检查语言包");}
}
Selection selectionAt(POINT point,HWND target)
{
    // UI Automation providers can block inside another application's COM
    // server. A UI timeout releases the busy state, but never piles up reads.
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
                send(WM_GETTEXTLENGTH,0,0)&&result>0&&result<=100000){
                std::vector<wchar_t> text(size_t(result)+1);
                if(send(WM_GETTEXT,text.size(),reinterpret_cast<LPARAM>(text.data()))){
                    auto selected=QString::fromWCharArray(text.data(),int(result)).mid(start,end-start).trimmed();
                    if(!selected.isEmpty()){
                        RECT client{};GetClientRect(focus,&client);POINT origin{};ClientToScreen(focus,&origin);
                        QRect area(origin.x,origin.y,client.right,client.bottom);
                        if(edit&&send(EM_POSFROMCHAR,start,0)){
                            POINT first{GET_X_LPARAM(result),GET_Y_LPARAM(result)};
                            if(send(EM_POSFROMCHAR,end-1,0)){
                                POINT last{GET_X_LPARAM(result),GET_Y_LPARAM(result)};
                                int h=std::max(16,int(GetDpiForWindow(focus)*20/96));
                                if(first.x>=0&&first.y>=0&&last.x>=0&&last.y>=0){
                                    area=first.y==last.y?QRect(origin.x+first.x,origin.y+first.y,std::max(h,int(last.x-first.x)+h),h):
                                        QRect(origin.x,origin.y+first.y,client.right,last.y-first.y+h);
                                }
                            }
                        }
                        return {selected,area};
                    }
                }
            }
        }
    }
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool initialized=SUCCEEDED(hr);
    struct Cleanup{bool initialized;~Cleanup(){if(initialized)CoUninitialize();}} cleanup{initialized};
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationTreeWalker> walker;
    if(FAILED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))))return {};
    automation->get_ControlViewWalker(&walker);
    for(int route=0;route<(target?1:2);++route){
        ComPtr<IUIAutomationElement> element;
        if(target)automation->ElementFromHandle(target,&element);
        else if(route==0)automation->GetFocusedElement(&element);else automation->ElementFromPoint(point,&element);
        for(int depth=0;element&&depth<6;++depth){
            int pid=0;element->get_CurrentProcessId(&pid);
            if(!target&&pid==int(GetCurrentProcessId()))break;
            ComPtr<IUIAutomationTextPattern> pattern;ComPtr<IUIAutomationTextRangeArray> ranges;
            if(SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern)))&&pattern&&
                SUCCEEDED(pattern->GetSelection(&ranges))&&ranges){
                int length=0;ranges->get_Length(&length);QStringList pieces;QRectF bounds;
                for(int i=0;i<std::min(length,32);++i){
                    ComPtr<IUIAutomationTextRange> range;BSTR text=nullptr;
                    if(FAILED(ranges->GetElement(i,&range))||!range)continue;
                    if(SUCCEEDED(range->GetText(100000,&text))&&text){
                        auto value=QString::fromWCharArray(text,SysStringLen(text));SysFreeString(text);
                        if(value.trimmed().isEmpty())continue;pieces<<value;
                        SAFEARRAY* rectangles=nullptr;
                        if(SUCCEEDED(range->GetBoundingRectangles(&rectangles))&&rectangles){
                            LONG lower=0,upper=-1;double* data=nullptr;
                            SafeArrayGetLBound(rectangles,1,&lower);SafeArrayGetUBound(rectangles,1,&upper);
                            if(SUCCEEDED(SafeArrayAccessData(rectangles,reinterpret_cast<void**>(&data)))){
                                for(LONG j=0;j+3<=upper-lower;j+=4){
                                    QRectF b(data[j],data[j+1],data[j+2],data[j+3]);
                                    if(!b.isEmpty())bounds=bounds.isEmpty()?b:bounds.united(b);
                                }SafeArrayUnaccessData(rectangles);
                            }SafeArrayDestroy(rectangles);
                        }
                    }
                }
                if(!pieces.isEmpty())return {pieces.join('\n').trimmed(),bounds.toAlignedRect()};
            }
            ComPtr<IUIAutomationElement> parent;
            if(!walker||FAILED(walker->GetParentElement(element.Get(),&parent)))break;
            element=parent;
        }
    }return {};
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
QImage translatedImage(const QImage& source,const QList<Block>& blocks,const QStringList& segments,bool dark)
{
    QImage result=source.convertToFormat(QImage::Format_RGB32);QPainter p(&result);p.setRenderHint(QPainter::TextAntialiasing);
    for(int i=0;i<std::min(blocks.size(),segments.size());++i){
        auto block=blocks[i];if(numeric(block.text))continue;
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
        p.fillRect(box,background);p.setPen(qGray(background.rgb())>140?QColor("#1B1B1B"):QColor("#F5F5F5"));
        QFont font("Microsoft YaHei UI");int size=std::clamp(qRound(block.glyphHeight*.95),14,28);QRect bounds;
        for(;size>=12;--size){font.setPixelSize(size);p.setFont(font);bounds=p.boundingRect(cell,Qt::TextWordWrap|Qt::AlignLeft|Qt::AlignTop,segments[i]);if(bounds.height()<=cell.height()&&bounds.width()<=cell.width())break;}
        font.setPixelSize(std::max(12,size));p.setFont(font);
        p.save();p.setClipRect(cell);
        p.fillRect(QRect(cell.topLeft(),QSize(std::min(bounds.width(),cell.width()),std::min(bounds.height(),cell.height()))),background);
        p.drawText(cell,Qt::TextWordWrap|Qt::AlignLeft|Qt::AlignTop,segments[i]);p.restore();
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
RawClicks::RawClicks(std::function<void(POINT,DWORD)> callback):m_callback(std::move(callback))
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
            (raw.data.mouse.usButtonFlags&(RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_RIGHT_BUTTON_DOWN|RI_MOUSE_MIDDLE_BUTTON_DOWN))){
            POINT point{};if(self->m_cursor.load()(&point))self->m_callback(point,DWORD(GetMessageTime()));
        }
    }return DefWindowProcW(hwnd,msg,w,l);
}
}

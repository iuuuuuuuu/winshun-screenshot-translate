#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include "Desktop.h"
#include "FirefoxSelection.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <algorithm>

namespace st {
namespace {
QString edgeText(QString text,int limit,bool before)
{
    // Embedded controls are a boundary, never an invitation to crawl the page.
    const auto object=before?text.lastIndexOf(QChar(0xfffc)):text.indexOf(QChar(0xfffc));
    if(object>=0)text=before?text.mid(object+1):text.left(object);
    text=before?text.right(limit):text.left(limit);
    if(!text.isEmpty()&&text.front().isLowSurrogate())text.remove(0,1);
    if(!text.isEmpty()&&text.back().isHighSurrogate())text.chop(1);
    return text.trimmed();
}
using Microsoft::WRL::ComPtr;
QString rangeText(IUIAutomationTextRange* range,int limit)
{
    BSTR raw=nullptr;
    const auto hr=range->GetText(limit,&raw);
    auto value=SUCCEEDED(hr)&&raw?QString::fromWCharArray(raw,SysStringLen(raw)):QString();
    SysFreeString(raw);return value;
}
}
bool contextSelectionMatches(const QString& actual,const QString& expected)
{
    return !expected.trimmed().isEmpty()&&expected.size()<=2000&&actual.size()<=4000&&actual.simplified()==expected.simplified();
}
QJsonObject boundedTranslationContext(const QJsonObject& value,int maxChars)
{
    const int limit=std::clamp(maxChars,0,1000);
    auto before=edgeText(value.value("before").toString(),limit,true);
    auto after=edgeText(value.value("after").toString(),limit,false);
    const int left=std::min(int(before.size()),limit/2+std::max(0,limit-limit/2-int(after.size())));
    before=edgeText(before,left,true);after=edgeText(after,limit-int(before.size()),false);
    if(before.isEmpty()&&after.isEmpty())return {};
    return {{"before",before},{"after",after}};
}
QJsonObject TranslationContext::get(Cancel cancel)
{
    std::lock_guard lock(m_mutex);
    if(cancel->load())return {};
    if(!m_done){
        QJsonObject value;
        try{value=m_read?m_read(cancel):QJsonObject();}catch(...){/* Context is optional. */}
        if(cancel->load())return {};
        m_value=boundedTranslationContext(value,m_limit);m_done=true;m_read={};
    }
    return m_value;
}
QJsonObject TranslationContext::snapshot()
{
    std::lock_guard lock(m_mutex);return m_value;
}
QJsonObject selectionContextAt(POINT point,HWND target,const QString& expected,int maxChars)
{
    if(!target||!IsWindow(target)||expected.trimmed().isEmpty()||expected.size()>2000)return {};
    maxChars=std::clamp(maxChars,0,1000);if(!maxChars)return {};
    // Read only this target. Do not follow a newly focused application.
    auto root=GetAncestor(target,GA_ROOT);
    wchar_t targetClass[128]{};GetClassNameW(target,targetClass,128);
    const auto className=QString::fromWCharArray(targetClass);
    if(className.compare("Edit",Qt::CaseInsensitive)==0||className.startsWith("RichEdit",Qt::CaseInsensitive)){
        if(GetWindowLongPtrW(target,GWL_STYLE)&ES_PASSWORD)return {};
        DWORD start=0,end=0;DWORD_PTR result=0;
        auto send=[&](UINT msg,WPARAM w,LPARAM l){return SendMessageTimeoutW(target,msg,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,80,&result)!=0;};
        // Standard EDIT has no text-range API. Only small fields qualify for
        // its bounded read; large documents use UIA below or omit context.
        if(send(EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end))&&end>start&&end-start<=4000&&
            send(WM_GETTEXTLENGTH,0,0)&&result<=4000){
            wchar_t buffer[4001]{};
            if(send(WM_GETTEXT,4001,reinterpret_cast<LPARAM>(buffer))&&end<=result){
                const auto text=QString::fromWCharArray(buffer,int(result));
                DWORD freshStart=0,freshEnd=0;
                if(send(EM_GETSEL,reinterpret_cast<WPARAM>(&freshStart),reinterpret_cast<LPARAM>(&freshEnd))&&freshStart==start&&freshEnd==end&&contextSelectionMatches(text.mid(start,end-start),expected))
                    return boundedTranslationContext({{"before",text.left(start)},{"after",text.mid(end)}},maxChars);
                return {};
            }
        }
    }
    const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Cleanup{bool init;~Cleanup(){if(init)CoUninitialize();}} cleanup{SUCCEEDED(hr)};
    wchar_t rootClass[64]{};GetClassNameW(root,rootClass,64);
    // Firefox's IA2 offsets are more reliable than its UIA projection. A
    // mismatch is deliberately empty instead of attaching a child fragment.
    if(wcscmp(rootClass,L"MozillaWindowClass")==0)return firefoxSelectionContext(target,expected,maxChars);
    ComPtr<IUIAutomation2> automation;
    if(FAILED(CoCreateInstance(__uuidof(CUIAutomation8),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))))return {};
    automation->put_ConnectionTimeout(80);automation->put_TransactionTimeout(80);
    ComPtr<IUIAutomationTreeWalker> walker;automation->get_RawViewWalker(&walker);
    auto read=[&](IUIAutomationElement* element)->QJsonObject {
        BOOL password=TRUE;
        if(FAILED(element->get_CurrentIsPassword(&password))||password)return {};
        ComPtr<IUIAutomationTextPattern> pattern;ComPtr<IUIAutomationTextRangeArray> ranges;
        if(FAILED(element->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern)))||!pattern||
            FAILED(pattern->GetSelection(&ranges))||!ranges)return {};
        int count=0;if(FAILED(ranges->get_Length(&count))||count!=1)return {};
        ComPtr<IUIAutomationTextRange> selection;
        if(FAILED(ranges->GetElement(0,&selection))||!selection||!contextSelectionMatches(rangeText(selection.Get(),4001),expected))return {};
        QJsonObject context;
        for(bool before:{true,false}){
            ComPtr<IUIAutomationTextRange> side;int moved=0;
            if(FAILED(selection->Clone(&side))||!side)continue;
            auto moving=before?TextPatternRangeEndpoint_Start:TextPatternRangeEndpoint_End;
            auto fixed=before?TextPatternRangeEndpoint_End:TextPatternRangeEndpoint_Start;
            if(FAILED(side->MoveEndpointByRange(fixed,selection.Get(),moving))||
                FAILED(side->MoveEndpointByUnit(moving,TextUnit_Character,before?-maxChars:maxChars,&moved)))continue;
            auto text=rangeText(side.Get(),maxChars*2+1);
            // A provider that ignores GetText's bound must not leak distant text.
            if(text.size()<=maxChars*2)context[before?"before":"after"]=text;
        }
        // The user may have changed selection while the provider was reading.
        ComPtr<IUIAutomationTextRangeArray> fresh;ComPtr<IUIAutomationTextRange> current;
        if(FAILED(pattern->GetSelection(&fresh))||!fresh||FAILED(fresh->get_Length(&count))||count!=1||
            FAILED(fresh->GetElement(0,&current))||!current)return {};
        BOOL same=FALSE;if(FAILED(selection->Compare(current.Get(),&same))||!same)return {};
        return boundedTranslationContext(context,maxChars);
    };
    ComPtr<IUIAutomationElement> rootElement;automation->ElementFromHandle(root,&rootElement);
    for(int route=0;route<2;++route){
        ComPtr<IUIAutomationElement> element;
        if(route==0)automation->ElementFromHandle(target,&element);
        else if(GetAncestor(WindowFromPoint(point),GA_ROOT)==root)automation->ElementFromPoint(point,&element);
        for(int depth=0;element&&depth<10;++depth){
            if(auto context=read(element.Get());!context.isEmpty())return context;
            BOOL isRoot=FALSE;if(rootElement&&SUCCEEDED(automation->CompareElements(element.Get(),rootElement.Get(),&isRoot))&&isRoot)break;
            ComPtr<IUIAutomationElement> parent;if(!walker||FAILED(walker->GetParentElement(element.Get(),&parent)))break;
            element=parent;
        }
    }
    if(rootElement){
        VARIANT type{};type.vt=VT_I4;type.lVal=UIA_DocumentControlTypeId;
        ComPtr<IUIAutomationCondition> condition;ComPtr<IUIAutomationElementArray> documents;
        if(SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&condition))&&
            SUCCEEDED(rootElement->FindAll(TreeScope_Descendants,condition.Get(),&documents))&&documents){
            int count=0;documents->get_Length(&count);
            for(int i=0;i<std::min(count,16);++i){
                ComPtr<IUIAutomationElement> element;documents->GetElement(i,&element);
                if(element)if(auto context=read(element.Get());!context.isEmpty())return context;
            }
        }
    }
    return {};
}
QJsonObject isolatedSelectionContext(POINT point,HWND target,const QString& expected,int maxChars,Cancel cancel,int timeoutMs)
{
    if(!target||!IsWindow(target)||cancel->load()||expected.isEmpty()||expected.size()>2000)return {};
    QElapsedTimer elapsed;elapsed.start();QProcess worker;
    wchar_t desktop[256]{};DWORD bytes=0;
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof desktop,&bytes);
    worker.setCreateProcessArgumentsModifier([&](QProcess::CreateProcessArguments* args){
        args->flags|=CREATE_NO_WINDOW;auto startup=reinterpret_cast<STARTUPINFOW*>(args->startupInfo);
        startup->lpDesktop=desktop;startup->dwFlags|=STARTF_USESHOWWINDOW;startup->wShowWindow=SW_HIDE;
    });
    worker.start(QCoreApplication::applicationDirPath()+"/ScreenshotTranslate.exe",{"--selection-worker"});
    if(!worker.waitForStarted(std::max(1,timeoutMs)))return {};
    worker.write(QJsonDocument(QJsonObject{{"context",true},{"expected",expected},{"maxChars",std::clamp(maxChars,0,1000)},
        {"target",QString::number(quintptr(target))},{"x",int(point.x)},{"y",int(point.y)}}).toJson(QJsonDocument::Compact)+"\n");
    worker.closeWriteChannel();
    while(!worker.waitForFinished(10)){
        if(cancel->load()||elapsed.elapsed()>=timeoutMs){worker.kill();worker.waitForFinished(100);return {};}
    }
    if(cancel->load()||elapsed.elapsed()>timeoutMs||worker.exitStatus()!=QProcess::NormalExit||worker.exitCode()!=0)return {};
    return boundedTranslationContext(QJsonDocument::fromJson(worker.readAllStandardOutput()).object().value("context").toObject(),maxChars);
}
}

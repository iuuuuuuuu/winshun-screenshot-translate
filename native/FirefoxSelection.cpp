#include "FirefoxSelection.h"
#include "vendor/ia2/SelectionInterfaces.h"
#include <servprov.h>
#include <wrl/client.h>
#include <QStringList>
#include <algorithm>
#include <cstdio>

namespace st {
namespace {
using Microsoft::WRL::ComPtr;
template<class T> ComPtr<T> query(IUnknown* object)
{
    ComPtr<T> result;
    if(!object)return result;
    object->QueryInterface(IID_PPV_ARGS(&result));
    if(!result){
        ComPtr<IServiceProvider> service;object->QueryInterface(IID_PPV_ARGS(&service));
        if(service)service->QueryService(IID_IAccessible,IID_PPV_ARGS(&result));
    }
    return result;
}
struct Variant {
    VARIANT value{};
    ~Variant(){VariantClear(&value);}
};
long property(IAccessible* object,bool state=false)
{
    VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;
    Variant result;
    const auto hr=state?object->get_accState(self,&result.value):object->get_accRole(self,&result.value);
    return SUCCEEDED(hr)&&result.value.vt==VT_I4?result.value.lVal:0;
}
struct Read {
    QString text;
    QRect end;
    bool valid=true;
    int lineBreaks=0;
};
int blockBreaks(IUnknown* object)
{
    auto accessible=query<IAccessible2>(object);
    if(!accessible)return 0;
    BSTR raw=nullptr;accessible->get_attributes(&raw);
    const auto attributes=raw?QString::fromWCharArray(raw,SysStringLen(raw)).split(';'):QStringList();
    SysFreeString(raw);
    if(attributes.contains("tag:p"))return 2;
    return attributes.contains("display:block")||attributes.contains("display:list-item")||
        attributes.contains("display:table-row")?1:0;
}
Read selected(IAccessibleText* text,int depth=0)
{
    // A malformed/cyclic provider must never produce a silently partial result.
    if(depth>96)return {{},{},false};
    if(auto accessible=query<IAccessible>(text);accessible&&
        (property(accessible.Get(),true)&STATE_SYSTEM_PROTECTED))return {};
    long count=0;
    if(FAILED(text->get_nSelections(&count))||count<0)return {{},{},false};
    QStringList ranges;QRect endArea;
    auto hypertext=query<IAccessibleHypertext>(text);
    for(long i=0;i<count;++i){
        long start=0,end=0;
        if(FAILED(text->get_selection(i,&start,&end))){
            // Firefox reports one phantom document selection for Windows
            // Suggested Actions. Its get_selection returns E_INVALIDARG.
            if(count==1)return {};
            return {{},{},false};
        }
        if(start<0||end<start)return {{},{},false};
        if(start==end)continue;
        BSTR raw=nullptr;
        const auto hr=text->get_text(start,end,&raw);
        QString value=raw?QString::fromWCharArray(raw,SysStringLen(raw)):QString();
        SysFreeString(raw);
        if(FAILED(hr)||value.size()!=end-start)return {{},{},false};
        QString expanded;qsizetype cursor=0;int pendingBreaks=0;
        auto append=[&](const QString& part,int lineBreaks=0){
            if(part.isEmpty())return;
            int required=std::max(pendingBreaks,lineBreaks);
            if(!expanded.isEmpty()&&required){
                for(auto j=expanded.size();j>0&&expanded[j-1]=='\n';--j)--required;
                for(auto c:part){if(c!='\n')break;--required;}
                if(required>0)expanded+=QString(required,'\n');
            }
            expanded+=part;pendingBreaks=lineBreaks;
        };
        for(auto pos=value.indexOf(QChar(0xfffc));pos>=0;pos=value.indexOf(QChar(0xfffc),cursor)){
            append(value.mid(cursor,pos-cursor));cursor=pos+1;
            long index=-1;ComPtr<IAccessibleHyperlink> child;
            if(!hypertext||FAILED(hypertext->get_hyperlinkIndex(start+long(pos),&index))||index<0){
                append(QString(QChar(0xfffc))); // A literal character, not an object.
                continue;
            }
            if(FAILED(hypertext->get_hyperlink(index,&child))||!child)return {{},{},false};
            if(auto childText=query<IAccessibleText>(child.Get())){
                // Hypertext counts a link/control as ONE character. Expand its
                // own selected offsets, not the parent's offsets in a flattened
                // string. This preserves partial first/last links and long text.
                auto part=selected(childText.Get(),depth+1);
                if(!part.valid)return part;
                if(part.text.isEmpty()){
                    long characters=0;
                    if(FAILED(childText->get_nCharacters(&characters))||characters>0)
                        return {{},{},false};
                }
                append(part.text,part.lineBreaks);
                if(!part.end.isEmpty())endArea=part.end;
            }else{
                auto accessible=query<IAccessible>(child.Get());
                if(!accessible||property(accessible.Get())!=ROLE_SYSTEM_GRAPHIC)
                    return {{},{},false};
                // A selected avatar is an object, not its alt text or "OBJ".
            }
        }
        append(value.mid(cursor));
        if(!expanded.isEmpty())ranges<<expanded;
        if(value.back()!=QChar(0xfffc)){
            long x=0,y=0,w=0,h=0;
            if(SUCCEEDED(text->get_characterExtents(end-1,IA2_COORDTYPE_SCREEN_RELATIVE,&x,&y,&w,&h))&&w>0&&h>0)
                endArea=QRect(x,y,w,h);
        }
    }
    return {ranges.join('\n'),endArea,true,blockBreaks(text)};
}
Selection documentSelection(IAccessible* object,int& visited,int depth=0)
{
    if(depth>64||++visited>512)return {};
    const auto state=property(object,true);
    if(state&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_PROTECTED))return {};
    if(property(object)==ROLE_SYSTEM_DOCUMENT){
        if(auto text=query<IAccessibleText>(object)){
            auto result=selected(text.Get());
            if(result.valid&&!result.text.trimmed().isEmpty())
                return {result.text.trimmed(),result.end,result.end};
            if(!result.valid)return {}; // Never replace a failed full range with a child fragment.
        }
    }
    long count=0;if(FAILED(object->get_accChildCount(&count)))return {};
    for(long i=0;i<count&&visited<512;++i){
        Variant child;long read=0;
        if(FAILED(AccessibleChildren(object,i,1,&child.value,&read))||!read||child.value.vt!=VT_DISPATCH)continue;
        auto accessible=query<IAccessible>(child.value.pdispVal);
        if(accessible)if(auto result=documentSelection(accessible.Get(),visited,depth+1);!result.text.isEmpty())return result;
    }
    return {};
}
}
Selection firefoxSelection(HWND target)
{
    auto root=target?GetAncestor(target,GA_ROOT):nullptr;
    wchar_t name[64]{};
    if(!root||!GetClassNameW(root,name,64)||wcscmp(name,L"MozillaWindowClass")!=0)return {};
    ComPtr<IAccessible> accessible;
    if(FAILED(AccessibleObjectFromWindow(root,OBJID_CLIENT,IID_PPV_ARGS(&accessible)))||!accessible)return {};
    // Go straight from the focused object to its containing document. Walking
    // every toolbar button first needlessly adds hundreds of cross-process calls.
    Variant focused;ComPtr<IAccessible> current;
    if(SUCCEEDED(accessible->get_accFocus(&focused.value))&&focused.value.vt==VT_DISPATCH)
        current=query<IAccessible>(focused.value.pdispVal);
    for(int depth=0;current&&depth<64;++depth){
        if(property(current.Get())==ROLE_SYSTEM_DOCUMENT){
            int visited=0;
            if(auto result=documentSelection(current.Get(),visited);!result.text.isEmpty()){
                if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE"))
                    std::fprintf(stderr,"[selection] Firefox IA2 focused document chars=%lld\n",qint64(result.text.size()));
                return result;
            }
            break;
        }
        ComPtr<IDispatch> parent;
        if(FAILED(current->get_accParent(&parent))||!parent)break;
        current=query<IAccessible>(parent.Get());
    }
    int visited=0;
    auto result=documentSelection(accessible.Get(),visited);
    if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE"))
        std::fprintf(stderr,"[selection] Firefox IA2 nodes=%d chars=%lld\n",visited,qint64(result.text.size()));
    return result;
}
}

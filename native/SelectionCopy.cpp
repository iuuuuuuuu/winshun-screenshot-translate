#include "SelectionCopy.h"
#include <ole2.h>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <windowsx.h>
#include <cstdio>
#include <optional>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace st {
namespace {
struct ClipboardFormat {
    UINT format=0;HANDLE data=nullptr;
    void release(){
        if(!data)return;
        if(format==CF_BITMAP||format==CF_DSPBITMAP||format==CF_PALETTE)DeleteObject(data);
        else if(format==CF_ENHMETAFILE||format==CF_DSPENHMETAFILE)DeleteEnhMetaFile(HENHMETAFILE(data));
        else if(format==CF_METAFILEPICT||format==CF_DSPMETAFILEPICT){
            if(auto picture=static_cast<METAFILEPICT*>(GlobalLock(data))){DeleteMetaFile(picture->hMF);GlobalUnlock(data);}
            GlobalFree(data);
        }else GlobalFree(data);
        data=nullptr;
    }
};
struct ClipboardLock {
    bool opened=false;
    explicit ClipboardLock(HWND window):opened(OpenClipboard(window)){}
    ~ClipboardLock(){if(opened)CloseClipboard();}
};
struct CopyTransaction {
    HWND window=nullptr;
    HANDLE mutex=nullptr;
    bool ownsMutex=false;
    DWORD baseline=0,copiedSequence=0;
    std::vector<ClipboardFormat> saved;
    bool snapshot(){
        ClipboardLock lock(window);if(!lock.opened){
            if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE"))std::fprintf(stderr,"[selection-copy] OpenClipboard error=%lu\n",GetLastError());
            return false;
        }
        UINT format=0;
        for(;;){
            SetLastError(ERROR_SUCCESS);format=EnumClipboardFormats(format);
            if(!format){if(GetLastError()!=ERROR_SUCCESS){
                if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE"))std::fprintf(stderr,"[selection-copy] EnumClipboardFormats error=%lu\n",GetLastError());
                return false;
            }break;}
            // Unknown private/owner-display handles cannot be safely restored.
            // Leave the clipboard untouched instead of saving only plain text.
            if(format==CF_OWNERDISPLAY||(format>=CF_PRIVATEFIRST&&format<=CF_PRIVATELAST)||
                (format>=CF_GDIOBJFIRST&&format<=CF_GDIOBJLAST))return false;
            auto data=GetClipboardData(format);if(!data)return false;
            HANDLE duplicate=format==CF_ENHMETAFILE||format==CF_DSPENHMETAFILE?
                CopyEnhMetaFileW(HENHMETAFILE(data),nullptr):OleDuplicateData(data,CLIPFORMAT(format),GMEM_MOVEABLE);
            if(!duplicate)return false;
            try{saved.push_back({format,duplicate});}
            catch(...){ClipboardFormat{format,duplicate}.release();throw;}
        }
        baseline=GetClipboardSequenceNumber();return true;
    }
    bool changedElsewhere=false;
    QString copiedText;
    bool readCopy(DWORD pid){
        if(GetClipboardSequenceNumber()==baseline)return false;
        ClipboardLock lock(window);if(!lock.opened)return false;
        DWORD owner=0;GetWindowThreadProcessId(GetClipboardOwner(),&owner);
        if(owner!=pid){changedElsewhere=true;return true;}
        copiedSequence=GetClipboardSequenceNumber();
        if(auto data=GetClipboardData(CF_UNICODETEXT)){
            const auto size=GlobalSize(data)/sizeof(wchar_t);
            if(auto value=static_cast<const wchar_t*>(GlobalLock(data))){
                const auto end=std::find(value,value+size,L'\0');
                if(end!=value+size)copiedText=QString::fromWCharArray(value,qsizetype(end-value));
                GlobalUnlock(data);
            }
        }
        copiedSequence=GetClipboardSequenceNumber();
        copiedText.replace("\r\n","\n");copiedText.replace('\r','\n');copiedText=copiedText.trimmed();
        return true;
    }
    bool restore(){
        if(!copiedSequence)return true;
        QElapsedTimer timer;timer.start();
        do{
            {
                ClipboardLock lock(window);
                if(lock.opened){
                    // A newer copy by the user or another app always wins.
                    if(GetClipboardSequenceNumber()!=copiedSequence){copiedSequence=0;return true;}
                    if(!EmptyClipboard())return false;
                    // Restoring an old item must not create a history/cloud copy.
                    if(auto marker=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DWORD))){
                        const auto format=RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
                        if(!format||!SetClipboardData(format,marker))GlobalFree(marker);
                    }
                    bool restored=true;
                    for(auto& entry:saved)if(entry.data){
                        if(SetClipboardData(entry.format,entry.data))entry.data=nullptr;
                        else restored=false;
                    }
                    copiedSequence=0;return restored;
                }
            }
            Sleep(10);
        }while(timer.elapsed()<600);
        return false;
    }
    ~CopyTransaction(){
        restore();
        for(auto& entry:saved)entry.release();
        if(window)DestroyWindow(window);
        if(ownsMutex)ReleaseMutex(mutex);
        if(mutex)CloseHandle(mutex);
    }
};
QString windowClass(HWND window)
{
    wchar_t name[256]{};GetClassNameW(window,name,256);return QString::fromWCharArray(name);
}
bool sameDesktop(HDESK a,HDESK b)
{
    wchar_t first[256]{},second[256]{};DWORD size=0;
    return a&&b&&GetUserObjectInformationW(a,UOI_NAME,first,sizeof first,&size)&&
        GetUserObjectInformationW(b,UOI_NAME,second,sizeof second,&size)&&std::wcscmp(first,second)==0;
}
bool onInputDesktop()
{
    const auto input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    const bool same=sameDesktop(input,GetThreadDesktop(GetCurrentThreadId()));
    if(input)CloseDesktop(input);return same;
}
bool keysReleased()
{
    for(int key=1;key<256;++key)if(GetAsyncKeyState(key)&0x8000)return false;
    return true;
}
bool passwordControl(HWND window)
{
    const auto name=windowClass(window);
    return (name.compare("Edit",Qt::CaseInsensitive)==0||name.startsWith("RichEdit",Qt::CaseInsensitive))&&
        (GetWindowLongPtrW(window,GWL_STYLE)&ES_PASSWORD);
}
struct SourceWindow {
    HWND target=nullptr,root=nullptr,focus=nullptr;
    DWORD pid=0;
    QString cacheKey;
    explicit SourceWindow(HWND window):target(window),root(GetAncestor(window,GA_ROOT)){
        const auto thread=GetWindowThreadProcessId(root,&pid);
        GUITHREADINFO info{sizeof info};
        if(GetGUIThreadInfo(thread,&info)&&GetAncestor(info.hwndFocus,GA_ROOT)==root)focus=info.hwndFocus;
        // Successful strategies are scoped to process lifetime and control class,
        // never to selected text or a permanent per-application allow-list.
        if(auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)){
            FILETIME created{},exited{},kernel{},user{};
            if(GetProcessTimes(process,&created,&exited,&kernel,&user))
                cacheKey=QString::number(pid)+":"+QString::number((quint64(created.dwHighDateTime)<<32)|created.dwLowDateTime)+
                    ":"+windowClass(root)+":"+windowClass(target);
            CloseHandle(process);
        }
    }
    bool valid()const{
        DWORD current=0;GetWindowThreadProcessId(root,&current);
        return target&&root&&IsWindow(target)&&IsWindow(root)&&current==pid&&GetAncestor(target,GA_ROOT)==root&&
            sameDesktop(GetThreadDesktop(GetWindowThreadProcessId(root,nullptr)),GetThreadDesktop(GetCurrentThreadId()));
    }
    CopyShortcutState shortcutState()const{
        GUITHREADINFO info{sizeof info};GetGUIThreadInfo(GetWindowThreadProcessId(root,nullptr),&info);
        return {true,valid()&&GetAncestor(GetForegroundWindow(),GA_ROOT)==root,
            focus&&info.hwndFocus==focus&&GetAncestor(focus,GA_ROOT)==root,onInputDesktop(),keysReleased(),
            !passwordControl(target)&&!passwordControl(focus)};
    }
};
enum class CopyRoute { FocusMessage, RootMessage, ApplicationCommand, Shortcut };
struct LearnedRoute {CopyRoute route;ULONGLONG expires;};
QMutex routeMutex;
QHash<QString,LearnedRoute> routes;
std::optional<CopyRoute> learnedRoute(const SourceWindow& source)
{
    QMutexLocker lock(&routeMutex);
    auto it=routes.find(source.cacheKey);
    if(it==routes.end())return {};
    if(it->expires<GetTickCount64()){routes.erase(it);return {};}
    return it->route;
}
void rememberRoute(const SourceWindow& source,CopyRoute route)
{
    if(source.cacheKey.isEmpty())return;
    QMutexLocker lock(&routeMutex);
    if(routes.size()>=128)routes.clear();
    routes.insert(source.cacheKey,{route,GetTickCount64()+10*60*1000});
}
void forgetRoute(const SourceWindow& source,CopyRoute route)
{
    QMutexLocker lock(&routeMutex);
    auto it=routes.find(source.cacheKey);if(it!=routes.end()&&it->route==route)routes.erase(it);
}
SelectionCopyResult stopped(QString error){return {SelectionCopyStatus::Stopped,{},error};}
SelectionCopyResult copySelection(const SourceWindow& source,Cancel cancel,SelectionCopyMode mode)
{
    if(cancel->load())return {};
    if(!source.valid())return stopped("原窗口已关闭或变化，请重新选中文字");
    if(passwordControl(source.target)||passwordControl(source.focus))return stopped("此输入框不允许读取选中文字");
    if(mode==SelectionCopyMode::Shortcut){
        // Ctrl+Insert is a copy shortcut, not the terminal's Ctrl+C interrupt.
        // Never activate a window or inject into a private/background desktop.
        QElapsedTimer released;released.start();
        for(;;){
            if(cancel->load())return {};
            const auto state=source.shortcutState();
            if(!state.sameWindow||!state.sameFocus||!state.inputDesktop||!state.readableControl)return {};
            if(copyShortcutAllowed(state))break;
            if(released.elapsed()>=400)return {};
            Sleep(10);
        }
    }
    CopyTransaction transaction;
    transaction.mutex=CreateMutexW(nullptr,FALSE,L"Local\\WinShun.ScreenshotTranslate.SelectionCopy");
    if(transaction.mutex){
        const auto wait=WaitForSingleObject(transaction.mutex,0);
        transaction.ownsMutex=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
    }
    if(!transaction.ownsMutex)return {};
    transaction.window=CreateWindowExW(0,L"STATIC",L"WinShun selection clipboard",0,0,0,0,0,
        HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!transaction.window||!transaction.snapshot()){
        if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE"))std::fprintf(stderr,"[selection-copy] clipboard snapshot unavailable\n");
        return {};
    }
    auto cached=learnedRoute(source);
    QList<CopyRoute> attempts;
    if(mode==SelectionCopyMode::Shortcut)attempts<<CopyRoute::Shortcut;
    else{
        if(cached&&*cached!=CopyRoute::Shortcut)attempts<<*cached;
        // Firefox has a reliable top-level native copy command even when its
        // accessibility selection stops at an embedded web component.
        if(firefoxCopyTarget(source.root))attempts<<CopyRoute::RootMessage;
        attempts<<CopyRoute::FocusMessage;
        if(source.root!=source.target)attempts<<CopyRoute::RootMessage;
        attempts<<CopyRoute::ApplicationCommand;
    }
    QList<CopyRoute> tried;QElapsedTimer budget;budget.start();
    for(const auto route:attempts){
        if(tried.contains(route))continue;tried<<route;
        if(cancel->load())break;
        if(!source.valid())return stopped("原窗口已关闭或变化，请重新选中文字");
        // A delayed response from an earlier message belongs to this same
        // transaction. Do not send another command after it has arrived.
        if(transaction.readCopy(source.pid))break;
        if(GetClipboardSequenceNumber()!=transaction.baseline)return stopped("剪贴板刚刚变化，请重新触发翻译");
        int waitMs=100;DWORD_PTR handled=0;bool timedOut=false;
        if(route==CopyRoute::Shortcut){
            if(!copyShortcutAllowed(source.shortcutState()))return {};
            INPUT keys[4]{};for(auto& key:keys){key.type=INPUT_KEYBOARD;key.ki.dwExtraInfo=0x53544350;}
            keys[0].ki.wVk=VK_CONTROL;keys[1].ki.wVk=keys[2].ki.wVk=VK_INSERT;keys[3].ki.wVk=VK_CONTROL;
            keys[1].ki.dwFlags=KEYEVENTF_EXTENDEDKEY;
            keys[2].ki.dwFlags=KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP;keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
            const auto sent=SendInput(4,keys,sizeof(INPUT));
            if(sent!=4){
                if(sent){INPUT up[2]{keys[2],keys[3]};SendInput(2,up,sizeof(INPUT));}
                if(!sent)return {};
                // A partial injection may already have delivered Ctrl+Insert.
                // Keep waiting/restoring instead of abandoning its clipboard.
            }
            waitMs=900;
        }else{
            const HWND recipient=route==CopyRoute::RootMessage?source.root:source.target;
            const UINT message=route==CopyRoute::ApplicationCommand?WM_APPCOMMAND:WM_COPY;
            SetLastError(ERROR_SUCCESS);
            const auto sent=SendMessageTimeoutW(recipient,message,message==WM_APPCOMMAND?WPARAM(source.target):0,
                message==WM_APPCOMMAND?MAKELPARAM(0,APPCOMMAND_COPY):0,SMTO_ABORTIFHUNG|SMTO_BLOCK,200,&handled);
            const auto sendError=GetLastError();
            if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE"))std::fprintf(stderr,"[selection-copy] route=%d sent=%d error=%lu sequenceChanged=%d\n",int(route),bool(sent),sendError,GetClipboardSequenceNumber()!=transaction.baseline);
            if(!sent&&sendError==ERROR_ACCESS_DENIED)continue;
            if(!sent){timedOut=true;waitMs=900;}
            if(firefoxCopyTarget(source.root)||cached==route||(message==WM_APPCOMMAND&&handled))waitMs=700;
        }
        QElapsedTimer timer;timer.start();
        do{
            if(transaction.readCopy(source.pid))break;
            // After issuing copy, cancellation still waits for the response so
            // the original clipboard can be restored before this thread exits.
            Sleep(10);
        }while(timer.elapsed()<waitMs&&budget.elapsed()<1200);
        if(transaction.changedElsewhere||transaction.copiedSequence){
            if(!transaction.copiedText.isEmpty()&&!transaction.changedElsewhere)rememberRoute(source,route);
            break;
        }
        forgetRoute(source,route);
        if(timedOut)return stopped("原窗口复制响应超时，请稍后重新选中文字；也可手动复制翻译");
        if(budget.elapsed()>=1200)break;
    }
    if(!transaction.restore())return stopped("读取结束，但剪贴板被占用而未能还原，请检查剪贴板");
    if(cancel->load())return {};
    if(transaction.changedElsewhere)return stopped("检测到另一处复制，本次读取已停止。请重新选中文字");
    if(!transaction.copiedText.isEmpty()){
        if(qEnvironmentVariableIsSet("ST_SELECTION_TRACE"))
            std::fprintf(stderr,"[selection-copy] %lldms mode=%s chars=%lld\n",budget.elapsed(),
                mode==SelectionCopyMode::Native?"message":"shortcut",qint64(transaction.copiedText.size()));
        return {SelectionCopyStatus::Copied,transaction.copiedText,{}};
    }
    return {};
}
}
bool copyShortcutAllowed(const CopyShortcutState& state)
{
    return state.hasSelection&&state.sameWindow&&state.sameFocus&&state.inputDesktop&&state.keysReleased&&state.readableControl;
}
bool firefoxCopyTarget(HWND target)
{
    return target&&windowClass(GetAncestor(target,GA_ROOT))=="MozillaWindowClass";
}
Selection resolveSelection(const std::function<Selection()>& read,
    const std::function<SelectionCopyResult(SelectionCopyMode)>& copy,Cancel cancel,bool preferShortcut,bool requireCopy)
{
    auto attempt=[&](SelectionCopyMode mode){
        if(cancel->load())return SelectionCopyResult{};
        auto result=copy(mode);
        if(cancel->load())return SelectionCopyResult{};
        if(result.status==SelectionCopyStatus::Stopped&&!cancel->load())throw std::runtime_error(result.error.toStdString());
        return result;
    };
    if(!preferShortcut){
        auto native=attempt(SelectionCopyMode::Native);
        if(native.status==SelectionCopyStatus::Copied)return {native.text,{},{}};
    }
    if(cancel->load())return {};
    Selection selected;
    try{selected=read();}catch(const std::exception&){ /* manual copy remains available */ }
    if(cancel->load())return {};
    // A nonempty accessibility selection is evidence for a copy shortcut,
    // not proof that the text is complete. Always prefer a fresh copy result.
    if(!selected.text.trimmed().isEmpty()&&selected.copyShortcutEligible){
        auto shortcut=attempt(SelectionCopyMode::Shortcut);
        if(shortcut.status==SelectionCopyStatus::Copied)return {shortcut.text,selected.area,selected.endArea};
    }
    if(preferShortcut){
        auto native=attempt(SelectionCopyMode::Native);
        if(native.status==SelectionCopyStatus::Copied)return {native.text,selected.area,selected.endArea};
    }
    if(cancel->load())return {};
    if(!requireCopy&&!selected.text.trimmed().isEmpty())return selected;
    // All applications get the same manual-copy continuation; do not reuse an
    // old icon snapshot when the fresh selection cannot be obtained.
    return {{},{},{},true};
}
Selection readSelectionForTranslation(POINT point,HWND target,Cancel cancel,const Selection& hint,const SelectionReader& reader)
{
    const SourceWindow source(target);
    const auto learned=learnedRoute(source);
    auto result=resolveSelection([&]{return reader(point,target,cancel);},
        [&](SelectionCopyMode mode){return copySelection(source,cancel,mode);},cancel,
        learned==CopyRoute::Shortcut,firefoxCopyTarget(target));
    if(result.area.isEmpty()){result.area=hint.area;result.endArea=hint.endArea;}
    return result;
}
Selection copyFirefoxSelection(HWND target,Cancel cancel)
{
    // Compatibility entry for the Firefox regression driver. The implementation
    // is the same clipboard transaction used for every explicit selection.
    auto result=copySelection(SourceWindow(target),cancel,SelectionCopyMode::Native);
    if(cancel->load())return {};
    if(result.status!=SelectionCopyStatus::Copied)
        throw std::runtime_error(result.error.isEmpty()?"没有读到可复制的选中文字":result.error.toStdString());
    return {result.text,{},{}};
}
#ifdef ST_SELECTION_COPY_TESTS
// Compiled into native_tests only. The callback supplies synthetic clipboard
// writes on the disposable GitHub runner, never in the installed plugin.
bool testClipboardNewerCopy(const std::function<void(HWND)>& first,const std::function<void(HWND)>& newer)
{
    CopyTransaction transaction;
    transaction.window=CreateWindowExW(0,L"STATIC",L"Clipboard transaction fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
    if(!transaction.window||!transaction.snapshot())return false;
    first(transaction.window);
    if(!transaction.readCopy(GetCurrentProcessId())||!transaction.copiedSequence)return false;
    newer(transaction.window);const auto sequence=GetClipboardSequenceNumber();
    return transaction.restore()&&GetClipboardSequenceNumber()==sequence;
}
#endif
}

#include "SelectionCopy.h"
#include <ole2.h>
#include <QElapsedTimer>
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
        ClipboardLock lock(window);if(!lock.opened)return false;
        UINT format=0;
        for(;;){
            SetLastError(ERROR_SUCCESS);format=EnumClipboardFormats(format);
            if(!format){if(GetLastError()!=ERROR_SUCCESS)return false;break;}
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
}
bool firefoxCopyTarget(HWND target)
{
    wchar_t name[64]{};
    return target&&GetClassNameW(GetAncestor(target,GA_ROOT),name,64)&&wcscmp(name,L"MozillaWindowClass")==0;
}
Selection copyFirefoxSelection(HWND target,Cancel cancel)
{
    if(!firefoxCopyTarget(target)||cancel->load())return {};
    target=GetAncestor(target,GA_ROOT);
    DWORD targetPid=0;GetWindowThreadProcessId(target,&targetPid);
    CopyTransaction transaction;
    transaction.mutex=CreateMutexW(nullptr,FALSE,L"Local\\WinShun.ScreenshotTranslate.SelectionCopy");
    if(transaction.mutex){
        const auto wait=WaitForSingleObject(transaction.mutex,0);
        transaction.ownsMutex=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
    }
    if(!transaction.ownsMutex)throw std::runtime_error("正在读取另一处浏览器选区，请稍后重试");
    transaction.window=CreateWindowExW(0,L"STATIC",L"WinShun selection clipboard",0,0,0,0,0,
        HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!transaction.window||!transaction.snapshot())
        throw std::runtime_error("剪贴板正忙或含有无法还原的内容。请在原窗口复制文字，再使用剪贴板翻译");
    if(cancel->load())return {};
    if(GetClipboardSequenceNumber()!=transaction.baseline)
        throw std::runtime_error("剪贴板内容刚刚发生变化，请重新触发翻译");
    // Firefox's accessibility ranges can stop at a link/shadow component while
    // its painted selection and native copy include the entire line. WM_COPY
    // targets the saved browser HWND directly: no Ctrl+C or foreground changes.
    DWORD_PTR ignored=0;
    SetLastError(ERROR_SUCCESS);
    const auto sent=SendMessageTimeoutW(target,WM_COPY,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,350,&ignored);
    if(!sent&&GetLastError()==ERROR_ACCESS_DENIED)
        throw std::runtime_error("当前插件权限不足。请以管理员身份启动 WinShun，并在插件设置中确认管理员权限后重试");
    QElapsedTimer timer;timer.start();QString text;
    while(timer.elapsed()<900){
        if(GetClipboardSequenceNumber()!=transaction.baseline){
            ClipboardLock lock(transaction.window);
            if(lock.opened){
                DWORD ownerPid=0;GetWindowThreadProcessId(GetClipboardOwner(),&ownerPid);
                if(ownerPid!=targetPid)break;
                transaction.copiedSequence=GetClipboardSequenceNumber();
                if(auto data=GetClipboardData(CF_UNICODETEXT)){
                    const auto size=GlobalSize(data)/sizeof(wchar_t);
                    if(auto value=static_cast<const wchar_t*>(GlobalLock(data))){
                        auto end=std::find(value,value+size,L'\0');
                        if(end!=value+size)text=QString::fromWCharArray(value,qsizetype(end-value));
                        GlobalUnlock(data);
                    }
                }
                // Delayed rendering can advance the sequence during GetData.
                transaction.copiedSequence=GetClipboardSequenceNumber();break;
            }
        }
        // Once WM_COPY was sent, cancellation must still restore the clipboard.
        Sleep(10);
    }
    if(!transaction.restore())throw std::runtime_error("读取结束，但剪贴板被占用而未能还原，请检查剪贴板");
    if(cancel->load())return {};
    text.replace("\r\n","\n");text.replace('\r','\n');text=text.trimmed();
    if(text.isEmpty())throw std::runtime_error("浏览器没有返回可复制的选中文字。请重新选中，或复制后使用剪贴板翻译");
    return {text,{},{}};
}
}

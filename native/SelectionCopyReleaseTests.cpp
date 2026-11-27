#include "SelectionCopy.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QMap>
#include <future>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <windowsx.h>

namespace st {
void testSelectionPolicy();
bool testNativeSelectionCopy();
bool testClipboardNewerCopy(const std::function<void(HWND)>&,const std::function<void(HWND)>&);
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){QCoreApplication::processEvents();QThread::msleep(2);}
bool openClipboard(HWND window){
    QElapsedTimer timer;timer.start();
    do{if(OpenClipboard(window))return true;QThread::msleep(5);}while(timer.elapsed()<600);
    return false;
}
void put(UINT format,QByteArray bytes){
    auto memory=GlobalAlloc(GMEM_MOVEABLE,size_t(bytes.size()));check(memory,"clipboard allocation");
    auto data=GlobalLock(memory);check(data,"clipboard memory lock");std::memcpy(data,bytes.data(),size_t(bytes.size()));GlobalUnlock(memory);
    if(!SetClipboardData(format,memory)){GlobalFree(memory);check(false,"clipboard format set");}
}
void writeText(HWND owner,QString text,bool rich=false){
    check(openClipboard(owner),"open synthetic clipboard");
    struct Close{~Close(){CloseClipboard();}} close;
    check(EmptyClipboard(),"empty synthetic clipboard");
    put(CF_UNICODETEXT,QByteArray(reinterpret_cast<const char*>(text.utf16()),(text.size()+1)*sizeof(wchar_t)));
    if(rich){
        auto html=QByteArray("<html><b>SYNTHETIC ORIGINAL</b></html>");html.append('\0');
        put(RegisterClipboardFormatW(L"HTML Format"),html);
        put(RegisterClipboardFormatW(L"WinShun.Selection.QA"),QByteArray::fromHex("000102abff00"));
        BITMAPINFOHEADER header{};header.biSize=sizeof header;header.biWidth=2;header.biHeight=2;
        header.biPlanes=1;header.biBitCount=32;header.biSizeImage=16;
        QByteArray dib(reinterpret_cast<const char*>(&header),sizeof header);dib+=QByteArray(16,'\x48');put(CF_DIB,dib);
    }
}
QMap<UINT,QByteArray> signature(){
    check(openClipboard(nullptr),"read synthetic clipboard formats");
    struct Close{~Close(){CloseClipboard();}} close;QMap<UINT,QByteArray> result;
    UINT format=0;
    while((format=EnumClipboardFormats(format))!=0){
        if(format==CF_BITMAP||format==CF_PALETTE||format==CF_ENHMETAFILE||format==CF_METAFILEPICT)continue;
        auto handle=GetClipboardData(format);auto size=GlobalSize(handle);auto data=GlobalLock(handle);
        check(data,"read clipboard format bytes");
        result[format]=QCryptographicHash::hash(QByteArrayView(static_cast<const char*>(data),qsizetype(size)),QCryptographicHash::Sha256);
        GlobalUnlock(handle);
    }
    return result;
}
void checkPreserved(const QMap<UINT,QByteArray>& before){
    const auto after=signature();
    for(auto it=before.cbegin();it!=before.cend();++it)check(after.value(it.key())==it.value(),"text, HTML, custom format and bitmap restored exactly");
}
struct Fixture {
    QString text="Complete synthetic selection with a final tail.";
    int nativeCopies=0,applicationCopies=0,keyCopies=0;
    bool applicationOnly=false,shortcutOnly=false;
    Cancel cancel;
};
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    auto fixture=reinterpret_cast<Fixture*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));return TRUE;}
    if(fixture){
        bool copy=false;
        if(message==WM_COPY){if(!fixture->applicationOnly&&!fixture->shortcutOnly){++fixture->nativeCopies;copy=true;}else return 0;}
        if(message==WM_APPCOMMAND&&GET_APPCOMMAND_LPARAM(l)==APPCOMMAND_COPY){
            if(!fixture->shortcutOnly){++fixture->applicationCopies;copy=true;}else return 0;
        }
        if(message==WM_KEYDOWN&&w==VK_INSERT&&(GetKeyState(VK_CONTROL)&0x8000)){++fixture->keyCopies;copy=true;}
        if(copy){
            writeText(window,fixture->text);
            if(fixture->cancel)fixture->cancel->store(true);
            return TRUE;
        }
    }
    return DefWindowProcW(window,message,w,l);
}
Selection run(HWND target,Cancel cancel,SelectionReader reader){
    auto future=std::async(std::launch::async,[=]{return readSelectionForTranslation({},target,cancel,{},reader);});
    QElapsedTimer timer;timer.start();
    while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){pump();check(timer.elapsed()<6000,"selection completes within bounded time");}
    return future.get();
}
Cancel token(){return std::make_shared<std::atomic_bool>(false);}
}
void testSelectionRelease(){
    // This test is intentionally unavailable on developer/user computers: it
    // owns synthetic clipboard contents and foreground windows on a fresh VM.
    check(qEnvironmentVariable("GITHUB_ACTIONS")=="true"&&qEnvironmentVariable("RUNNER_ENVIRONMENT")=="github-hosted",
        "interactive release test requires disposable GitHub-hosted Windows runner");
    testSelectionPolicy();qputenv("ST_SELECTION_TRACE","1");
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=windowProc;cls.lpszClassName=L"WinShunSelectionReleaseFixture";
    check(RegisterClassW(&cls),"register generic custom control");
    Fixture fixture;auto window=CreateWindowExW(0,cls.lpszClassName,L"Synthetic selection release test",WS_OVERLAPPEDWINDOW,
        50,50,500,200,nullptr,nullptr,cls.hInstance,&fixture);
    check(window,"create copy fixture");
    struct Cleanup{HWND window;~Cleanup(){DestroyWindow(window);}} cleanup{window};
    writeText(window,"SYNTHETIC ORIGINAL — 中文 😀",true);auto baseline=signature();
    auto partial=[](POINT,HWND,Cancel){return Selection{"Complete",{},{},false,true};};
    check(run(window,token(),partial).text==fixture.text,"unknown application uses shared WM_COPY path");checkPreserved(baseline);
    fixture.cancel=token();check(run(window,fixture.cancel,partial).text.isEmpty(),"cancelled copy result discarded");
    checkPreserved(baseline);fixture.cancel.reset();
    fixture.applicationOnly=true;
    check(run(window,token(),partial).text==fixture.text&&fixture.applicationCopies>0,"copy application command fallback works");checkPreserved(baseline);
    // Real SendInput uses the original HWND. Only this test activates a window;
    // production must preserve the existing foreground and keyboard focus.
    fixture.shortcutOnly=true;ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);SetFocus(window);
    QElapsedTimer focus;focus.start();
    while(GetForegroundWindow()!=window&&focus.elapsed()<2000)pump();
    check(GetForegroundWindow()==window&&GetFocus()==window,"synthetic window owns foreground and focus");
    const auto nativeBefore=fixture.nativeCopies,applicationBefore=fixture.applicationCopies;
    check(run(window,token(),partial).text==fixture.text&&fixture.keyCopies==1,"real Ctrl+Insert copy completes");checkPreserved(baseline);
    check(run(window,token(),partial).text==fixture.text&&fixture.keyCopies==2,"learned shortcut works on next request");checkPreserved(baseline);
    check(fixture.nativeCopies==nativeBefore&&fixture.applicationCopies==applicationBefore,"shortcut path does not call unrelated commands");
    auto noSelection=[](POINT,HWND,Cancel){return Selection{};};
    check(run(window,token(),noSelection).needsManualCopy&&fixture.keyCopies==2,"empty selection never sends global copy keys");
    checkPreserved(baseline);
    check(testClipboardNewerCopy([](HWND w){writeText(w,"first copied text");},[](HWND w){writeText(w,"newer user copy");}),
        "newer clipboard content wins instead of restoring the older snapshot");
    writeText(window,"SYNTHETIC ORIGINAL — 中文 😀",true);
    std::puts("{\"passed\":true,\"nativeCopy\":true,\"applicationCommand\":true,\"realKeyboardCopy\":true,\"clipboardFormatsRestored\":true,\"cancelRestoresClipboard\":true,\"newCopyWins\":true,\"strategyCache\":true}");
}
}

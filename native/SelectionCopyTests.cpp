#include "SelectionCopy.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <future>
#include <stdexcept>

namespace st {
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Cancel token(){return std::make_shared<std::atomic_bool>(false);}
}
void testSelectionPolicy()
{
    int reads=0,native=0,shortcuts=0;
    auto read=[&]{++reads;return Selection{"author",{1,2,3,4},{},false,true};};
    auto copy=[&](SelectionCopyMode mode){
        if(mode==SelectionCopyMode::Native){++native;return SelectionCopyResult{};}
        ++shortcuts;return SelectionCopyResult{SelectionCopyStatus::Copied,"author released this 1 hour ago",{}};
    };
    auto selected=resolveSelection(read,copy,token());
    check(selected.text=="author released this 1 hour ago"&&selected.area==QRect(1,2,3,4)&&reads==1&&native==1&&shortcuts==1,
        "nonempty partial UIA must still try complete native copy");
    reads=native=shortcuts=0;
    selected=resolveSelection(read,copy,token(),true);
    check(selected.text.endsWith("ago")&&reads==1&&native==0&&shortcuts==1,"successful shortcut route skips redundant native attempts");
    reads=0;
    selected=resolveSelection(read,[](SelectionCopyMode){return SelectionCopyResult{SelectionCopyStatus::Copied,"full selection",{}};},token());
    check(selected.text=="full selection"&&!reads,"native copy fast path does not need UIA");
    auto unavailable=[](SelectionCopyMode){return SelectionCopyResult{};};
    selected=resolveSelection(read,unavailable,token());
    check(selected.text=="author"&&!selected.needsManualCopy,"UIA remains usable when copying is unsupported");
    selected=resolveSelection(read,unavailable,token(),false,true);
    check(selected.text.isEmpty()&&selected.needsManualCopy,"known truncated provider never silently falls back to partial text");
    shortcuts=0;
    selected=resolveSelection([]{return Selection{};},[&](SelectionCopyMode mode){
        if(mode==SelectionCopyMode::Shortcut)++shortcuts;return SelectionCopyResult{};
    },token());
    check(!shortcuts&&selected.needsManualCopy,"empty selection cannot send a copy shortcut to an arbitrary app");
    selected=resolveSelection([]{return Selection{"old document selection",{},{}};},[&](SelectionCopyMode mode){
        if(mode==SelectionCopyMode::Shortcut)++shortcuts;return SelectionCopyResult{};
    },token());
    check(!shortcuts&&selected.text=="old document selection","unrelated focused control must not receive copy keys");
    selected=resolveSelection([]()->Selection{throw std::runtime_error("provider timeout");},unavailable,token());
    check(selected.needsManualCopy,"provider timeout offers generic manual copy");
    bool stopped=false;reads=0;
    try{resolveSelection(read,[](SelectionCopyMode){return SelectionCopyResult{SelectionCopyStatus::Stopped,{},"other copy"};},token());}
    catch(const std::exception&){stopped=true;}
    check(stopped&&!reads,"clipboard race must stop instead of translating stale text");
    auto cancel=token();cancel->store(true);reads=0;native=0;
    selected=resolveSelection(read,[&](SelectionCopyMode){++native;return SelectionCopyResult{};},cancel);
    check(!reads&&!native&&selected.text.isEmpty(),"cancelled selection never reads or copies");
    cancel=token();
    selected=resolveSelection(read,[&](SelectionCopyMode){
        cancel->store(true);return SelectionCopyResult{SelectionCopyStatus::Copied,"cancelled result",{}};
    },cancel);
    check(selected.text.isEmpty(),"copy completed after cancellation cannot reach translation");
    CopyShortcutState state{true,true,true,true,true,true};
    check(copyShortcutAllowed(state),"valid focused selected text may use copy shortcut");
    for(auto flag:{&CopyShortcutState::hasSelection,&CopyShortcutState::sameWindow,&CopyShortcutState::sameFocus,
                  &CopyShortcutState::inputDesktop,&CopyShortcutState::keysReleased,&CopyShortcutState::readableControl}){
        auto blocked=state;blocked.*flag=false;
        check(!copyShortcutAllowed(blocked),"selection, focus, desktop, held keys and password guards are mandatory");
    }
}
// This fixture runs only on a private desktop. Native WM_COPY exercises the
// real clipboard transaction, preserving existing formats; SendInput is denied.
bool testNativeSelectionCopy()
{
    qputenv("ST_SELECTION_TRACE","1");
    wchar_t desktop[256]{};DWORD bytes=0;
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof desktop,&bytes);
    check(QString::fromWCharArray(desktop).startsWith("WinShun_ST_Probe_"),"copy fixture requires a private desktop");
    auto parent=CreateWindowExW(0,L"STATIC",L"Native copy fixture",WS_OVERLAPPEDWINDOW,0,0,500,300,nullptr,nullptr,nullptr,nullptr);
    auto edit=CreateWindowExW(0,L"EDIT",L"author released this 1 hour ago",WS_CHILD|ES_MULTILINE,0,0,400,200,parent,nullptr,nullptr,nullptr);
    check(parent&&edit,"native text controls created");
    struct Cleanup{HWND window;~Cleanup(){DestroyWindow(window);}} cleanup{parent};
    auto run=[&](SelectionReader reader){
        auto result=std::async(std::launch::async,[=]{return readSelectionForTranslation({},edit,token(),{},reader);});
        QElapsedTimer timer;timer.start();
        while(result.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){
            QCoreApplication::processEvents();QThread::msleep(2);
            check(timer.elapsed()<5000,"copy transaction deadline");
        }
        return result.get();
    };
    auto partial=[](POINT,HWND,Cancel){return Selection{"author",{},{}};};
    const auto clipboardBefore=GetClipboardSequenceNumber();
    if(!OpenClipboard(nullptr)){
        SendMessageW(edit,EM_SETSEL,0,-1);
        check(run(partial).text=="author","clipboard access denial falls back to the read-only interface");
        check(run([](POINT,HWND,Cancel){return Selection{};}).needsManualCopy,"clipboard denial and missing UIA offer manual continuation");
        check(GetClipboardSequenceNumber()==clipboardBefore,"denied clipboard never triggers a copy or changes clipboard");
        return false;
    }
    CloseClipboard();
    SendMessageW(edit,EM_SETSEL,0,-1);
    check(run(partial).text=="author released this 1 hour ago","generic Edit copy wins over truncated accessibility");
    SendMessageW(edit,EM_SETSEL,7,20);
    check(run(partial).text=="released this","partial selection copies selected characters only");
    const auto longText=QString("跨页 text 😀 line\r\n").repeated(9000)+"TAIL";
    SetWindowTextW(edit,reinterpret_cast<LPCWSTR>(longText.utf16()));SendMessageW(edit,EM_SETSEL,0,-1);
    const auto longResult=run(partial).text;
    check(longResult.endsWith("TAIL")&&longResult.size()>100000,"native selection keeps long text tail");
    SendMessageW(edit,EM_SETSEL,0,0);
    check(run([](POINT,HWND,Cancel){return Selection{};}).needsManualCopy,"unselected arbitrary app offers manual continuation");
    // A nonempty fallback on the private desktop must never inject keys into
    // the user's active desktop, even if native copy returned nothing.
    check(run(partial).text=="author","private desktop denies global keyboard input and retains read-only fallback");
    return true;
}
}

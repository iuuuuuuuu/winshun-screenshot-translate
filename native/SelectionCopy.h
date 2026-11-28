#pragma once
#include "Desktop.h"
namespace st {
enum class SelectionCopyMode { Native, Shortcut };
enum class SelectionCopyStatus { Unavailable, Copied, Stopped };
struct SelectionCopyResult {
    SelectionCopyStatus status=SelectionCopyStatus::Unavailable;
    QString text,error;
};
struct CopyShortcutState {
    bool hasSelection=false, sameWindow=false, sameFocus=false, inputDesktop=false;
    bool keysReleased=false, readableControl=false;
};
bool copyShortcutAllowed(const CopyShortcutState& state);
struct CopyProcessIdentity {DWORD pid=0,parent=0;quint64 created=0;};
// A clipboard broker must be this exact source process or one of its live
// descendants. Never admit a sibling merely because its executable matches.
bool copyProcessMatches(CopyProcessIdentity source,DWORD owner,const std::function<CopyProcessIdentity(DWORD)>& lookup);
bool selectionCopyOwnerMatches(HWND target,DWORD owner);
using SelectionReader=std::function<Selection(POINT,HWND,Cancel)>;
// Shared policy, separated from Windows I/O so failures/cancellation can be tested.
Selection resolveSelection(const std::function<Selection()>& read,
    const std::function<SelectionCopyResult(SelectionCopyMode)>& copy, Cancel cancel,
    bool preferShortcut=false,bool requireCopy=false);
Selection readSelectionForTranslation(POINT point,HWND target,Cancel cancel,
    const Selection& hint,const SelectionReader& reader);
bool firefoxCopyTarget(HWND target);
// Explicit translation requests only. Never used by passive selection polling.
// Runs on a background thread; cancellation still finishes clipboard restoration.
Selection copyFirefoxSelection(HWND target,Cancel cancel);
}

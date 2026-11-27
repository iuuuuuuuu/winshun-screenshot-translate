#pragma once
#include "Desktop.h"
namespace st {
bool firefoxCopyTarget(HWND target);
// Explicit translation requests only. Never used by passive selection polling.
// Runs on a background thread; cancellation still finishes clipboard restoration.
Selection copyFirefoxSelection(HWND target,Cancel cancel);
}

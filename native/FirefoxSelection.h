#pragma once
#include "Desktop.h"
namespace st {
// Called in the isolated COM selection worker. Reads only selected IA2 ranges.
Selection firefoxSelection(HWND target,const POINT* pointer=nullptr);
QJsonObject firefoxSelectionContext(HWND target,const QString& expected,int maxChars);
}

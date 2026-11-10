#pragma once
#include <atlbase.h>
#include <atlstr.h>
#include <atlapp.h>
extern WTL::CAppModule _Module;
#include <atlwin.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atlmisc.h>
#include <atldlgs.h>
#include <atlctrlw.h>
#include <atltheme.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <iterator>
#include "Scintilla.h"
#include "SciLexer.h"
using namespace ATL;
using namespace WTL;
using std::min;
using std::max;
constexpr UINT WM_DOCUMENT_CHANGED = WM_APP + 0x100;
constexpr UINT WM_CLOSE_TAB = WM_APP + 0x101;
constexpr UINT WM_PANE_ACTION = WM_APP + 0x102;
#include "Frameworks.h"


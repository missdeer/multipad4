#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <cstddef>
TBBUTTON UpstreamToolbarButton(int index);
void RestoreUpstreamToolbar(HWND toolbar, const std::wstring& buttons);
UINT NormalizeCommand(UINT id);
class MainFrame;
void UpdateMenuCommands(MainFrame& frame, HMENU menu);
#ifdef MULTIPAD4_TESTING
struct StatusMeasurementCounters {
    size_t extents = 0, margins = 0, localeQueries = 0, formatLoads = 0;
};
extern StatusMeasurementCounters statusMeasurementCounters;
extern bool statusPresentationBaseline;
#endif

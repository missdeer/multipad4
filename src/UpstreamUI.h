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
#include "../tests/hooks/UpstreamUI.h.01.inc"
#endif

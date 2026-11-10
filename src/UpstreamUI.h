#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
TBBUTTON UpstreamToolbarButton(int index);
void RestoreUpstreamToolbar(HWND toolbar, const std::wstring& buttons);
UINT NormalizeCommand(UINT id);
class MainFrame;
void UpdateMenuCommands(MainFrame& frame, HMENU menu);

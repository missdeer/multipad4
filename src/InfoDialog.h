#pragma once
#include <windows.h>
#include <string>
class AppSettings;
INT_PTR ShowInfoDialog(HWND owner, AppSettings& settings, const std::wstring& message, const wchar_t* title,
    UINT type = MB_OK | MB_ICONINFORMATION, const wchar_t* suppressionKey = L"");

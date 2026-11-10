#pragma once
#include <windows.h>
#include <string>
void ShowSystemIntegration(HWND owner);

// The key overload also permits callers to supply an already opened key with
// limited access. Both return the registry error instead of hiding write failures.
LSTATUS ApplyNotepadReplacementToKey(HKEY key, const std::wstring& executable, bool enable);
LSTATUS ApplyNotepadReplacement(HKEY hive, const wchar_t* path, const std::wstring& executable, bool enable, REGSAM view = 0);

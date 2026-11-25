#pragma once
#include <windows.h>
#include <string>
void ShowSystemIntegration(HWND owner);

inline constexpr wchar_t NotepadKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\notepad.exe";
inline constexpr wchar_t NotepadReplacementHelper[] = L"multipad4-registry.exe";

// The key overload also permits callers to supply an already opened key with
// limited access. Both return the registry error instead of hiding write failures.
LSTATUS ApplyNotepadReplacementToKey(HKEY key, const std::wstring& executable, bool enable);
LSTATUS ApplyNotepadReplacement(HKEY hive, const wchar_t* path, const std::wstring& executable, bool enable, REGSAM view = 0);

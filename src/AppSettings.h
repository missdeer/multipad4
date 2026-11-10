#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <windows.h>
class AppSettings {
public:
    std::filesystem::path path;
    bool persistent = true;
    void Initialize(bool testMode = false);
    int Get(UINT command, int fallback = 0) const;
    void Set(UINT command, int value);
    std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") const;
    void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value);
    bool Save() const;
    bool SaveOption(UINT command) const;
private:
    bool EnsureProfile() const;
    mutable std::map<UINT, int> values;
    mutable std::map<std::pair<std::wstring, std::wstring>, std::wstring> strings;
};

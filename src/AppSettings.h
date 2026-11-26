#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <set>
#include <vector>
#include <windows.h>
class AppSettings {
public:
    std::filesystem::path path;
    bool persistent = true;
    bool portable = false;
    void Initialize(bool testMode = false);
    int Get(UINT command, int fallback = 0) const;
    void Set(UINT command, int value);
    std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback = L"") const;
    void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value);
    void RecordRecentFile(const std::filesystem::path& file, int count);
    bool Save() const;
    bool SaveOption(UINT command) const;
private:
    bool EnsureProfile() const;
    mutable std::set<UINT> dirtyOptions;
    mutable std::set<std::pair<std::wstring,std::wstring>> dirtyStrings, missingStrings;
    mutable std::vector<wchar_t> readBuffer;
    std::vector<std::wstring> recentSlots;
    bool recentValid = false;
    mutable std::map<UINT, int> values;
    mutable std::map<std::pair<std::wstring, std::wstring>, std::wstring> strings;
};

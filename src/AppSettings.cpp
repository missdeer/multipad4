#include "AppSettings.h"
#include <shlobj.h>
#include <cwctype>
namespace {
constexpr wchar_t EncodedPrefix[] = L"@MultiPad4:utf16:";
std::wstring ProfileValue(const std::wstring& value) {
    bool encode = value.starts_with(EncodedPrefix) || value.find_first_of(L"\r\n") != std::wstring::npos || value.find(L'\0') != std::wstring::npos;
    if (!value.empty()) encode |= iswspace(value.front()) || iswspace(value.back()) || value.front() == L'"' || value.front() == L'\'';
    if (!encode) return value;
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result = EncodedPrefix;
    for (wchar_t character : value) for (int shift : {12, 8, 4, 0}) result += digits[(static_cast<unsigned>(character) >> shift) & 15];
    return result;
}
std::wstring DecodeProfileValue(const std::wstring& value) {
    constexpr size_t prefixLength = std::size(EncodedPrefix) - 1;
    if (!value.starts_with(EncodedPrefix) || (value.size() - prefixLength) % 4) return value;
    std::wstring result;
    for (size_t i = prefixLength; i < value.size(); i += 4) {
        unsigned code = 0;
        for (size_t j = 0; j < 4; ++j) {
            wchar_t digit = value[i + j];
            unsigned number = digit >= L'0' && digit <= L'9' ? digit - L'0' : digit >= L'A' && digit <= L'F' ? digit - L'A' + 10 : 99;
            if (number > 15) return value; code = code * 16 + number;
        }
        result += static_cast<wchar_t>(code);
    }
    return result;
}
}
void AppSettings::Initialize(bool testMode) {
    persistent = !testMode;
    if (testMode) {
        path = std::filesystem::temp_directory_path() / (L"MultiPad4-test-settings-" + std::to_wstring(GetCurrentProcessId())) / L"Settings.ini";
        return;
    }
    wchar_t overridePath[32768]{};
    if (GetEnvironmentVariableW(L"MULTIPAD4_SETTINGS", overridePath, _countof(overridePath))) path = overridePath;
    else {
        PWSTR folder = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder))) {
            path = std::filesystem::path(folder) / L"MultiPad4" / L"MultiPad4.ini";
            CoTaskMemFree(folder);
        }
    }
    if (path.empty()) persistent = false;
    else path = std::filesystem::absolute(path);
}
int AppSettings::Get(UINT command, int fallback) const {
    if (auto found = values.find(command); found != values.end()) return found->second;
    auto key = L"Command" + std::to_wstring(command);
    auto text = ReadString(L"Options", key.c_str(), std::to_wstring(fallback).c_str());
    wchar_t* end = nullptr; auto value = wcstol(text.c_str(), &end, 10);
    int result = end != text.c_str() && !*end ? static_cast<int>(value) : fallback;
    values.emplace(command, result); return result;
}
void AppSettings::Set(UINT command, int value) { values[command] = value; }
std::wstring AppSettings::ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) const {
    auto name = std::make_pair(std::wstring(section), std::wstring(key));
    if (auto found = strings.find(name); found != strings.end()) return found->second;
    std::wstring text(32768, L'\0');
    if (persistent) {
        for (;;) {
            auto length = GetPrivateProfileStringW(section, key, fallback, text.data(), static_cast<DWORD>(text.size()), path.c_str());
            if (length < text.size() - 1 || text.size() >= 1024 * 1024) { text.resize(length); break; }
            text.resize(text.size() * 2);
        }
        text = DecodeProfileValue(text);
    }
    else text = fallback;
    strings.emplace(std::move(name), text); return text;
}
void AppSettings::WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    strings[std::make_pair(std::wstring(section), std::wstring(key))] = value;
}
bool AppSettings::EnsureProfile() const {
    if (!persistent) return true;
    std::error_code error; std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    // A UTF-16 BOM lets the profile APIs preserve non-ASCII fonts and paths.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) { WORD bom = 0xFEFF; DWORD written = 0; WriteFile(file, &bom, sizeof(bom), &written, nullptr); CloseHandle(file); }
    return true;
}
bool AppSettings::SaveOption(UINT command) const {
    if (!persistent) return true;
    if (!EnsureProfile()) return false;
    auto key = L"Command" + std::to_wstring(command), value = std::to_wstring(Get(command));
    return WritePrivateProfileStringW(L"Options", key.c_str(), value.c_str(), path.c_str()) != FALSE;
}
bool AppSettings::Save() const {
    if (!persistent) return true;
    if (!EnsureProfile()) return false;
    bool ok = true;
    for (const auto& [command, value] : values) {
        auto key = L"Command" + std::to_wstring(command), text = std::to_wstring(value);
        ok &= WritePrivateProfileStringW(L"Options", key.c_str(), text.c_str(), path.c_str()) != FALSE;
    }
    for (const auto& [name, value] : strings) if (name.first != L"Options") {
        auto encoded = ProfileValue(value);
        ok &= WritePrivateProfileStringW(name.first.c_str(), name.second.c_str(), encoded.c_str(), path.c_str()) != FALSE;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str()); return ok;
}

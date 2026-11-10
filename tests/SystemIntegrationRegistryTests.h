#pragma once
#include "../src/SystemIntegration.h"
#include <stdexcept>
#include <source_location>

// Uses only a disposable HKCU branch. Never invokes the dialog or touches IFEO.
inline void RunSystemIntegrationRegistryTests() {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Notepad registry assertion failed at line " + std::to_string(where.line()));
    };
    struct Fixture {
        std::wstring root = L"Software\\MultiPad4-Tests-" + std::to_wstring(GetCurrentProcessId());
        std::wstring path = root + L"\\NotepadReplacement";
        HKEY key = nullptr;
        ~Fixture() {
            if (key) RegCloseKey(key);
            RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
            RegDeleteKeyW(HKEY_CURRENT_USER, root.c_str()); // succeeds only if empty
        }
    } fixture;
    require(RegCreateKeyExW(HKEY_CURRENT_USER, fixture.path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
        nullptr, &fixture.key, nullptr) == ERROR_SUCCESS);
    const std::wstring executable = L"C:\\MultiPad4 Registry Test\\multipad4.exe";
    const std::wstring command = L"\"" + executable + L"\" /z";
    auto setDword = [&](const wchar_t* name, DWORD value) {
        require(RegSetValueExW(fixture.key, name, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS);
    };
    auto setString = [&](const wchar_t* name, const std::wstring& value) {
        require(RegSetValueExW(fixture.key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    };
    auto dword = [&](const wchar_t* name) {
        DWORD value = 0, type = 0, bytes = sizeof(value);
        require(RegQueryValueExW(fixture.key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) == ERROR_SUCCESS);
        require(type == REG_DWORD && bytes == sizeof(value)); return value;
    };
    auto string = [&](const wchar_t* name) {
        wchar_t value[512]{}; DWORD type = 0, bytes = sizeof(value);
        require(RegQueryValueExW(fixture.key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS);
        require(type == REG_SZ); return std::wstring(value);
    };
    auto absent = [&](const wchar_t* name) {
        require(RegQueryValueExW(fixture.key, name, nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND);
    };
    auto apply = [&](bool enable) {
        return ApplyNotepadReplacement(HKEY_CURRENT_USER, fixture.path.c_str(), executable, enable);
    };

    setDword(L"UseFilter", 1);
    setDword(L"AppExecutionAliasRedirect", 1);
    setString(L"AppExecutionAliasRedirectPackages", L"*");
    setString(nullptr, L"Unrelated default value");
    HKEY child = nullptr;
    require(RegCreateKeyExW(fixture.key, L"0", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &child, nullptr) == ERROR_SUCCESS);
    const DWORD marker = 42;
    const LSTATUS childStatus = RegSetValueExW(child, L"Unrelated", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&marker), sizeof(marker));
    RegCloseKey(child); require(childStatus == ERROR_SUCCESS);

    require(apply(true) == ERROR_SUCCESS);
    require(string(L"Debugger") == command && dword(L"UseFilter") == 0);
    absent(L"AppExecutionAliasRedirect"); absent(L"AppExecutionAliasRedirectPackages");
    require(apply(true) == ERROR_SUCCESS); // repeated enable is safe
    require(apply(false) == ERROR_SUCCESS);
    absent(L"Debugger");
    require(dword(L"UseFilter") == 1 && dword(L"AppExecutionAliasRedirect") == 1);
    require(string(L"AppExecutionAliasRedirectPackages") == L"*");
    require(string(nullptr) == L"Unrelated default value");
    DWORD actualMarker = 0, markerBytes = sizeof(actualMarker);
    require(RegGetValueW(fixture.key, L"0", L"Unrelated", RRF_RT_REG_DWORD, nullptr, &actualMarker, &markerBytes) == ERROR_SUCCESS);
    require(actualMarker == marker);

    // Substring ownership used to mistake wrapper commands for our replacement.
    const std::wstring foreign = L"\"C:\\Other Editor\\wrapper.exe\" " + command;
    setString(L"Debugger", foreign);
    setDword(L"UseFilter", 7);
    setDword(L"AppExecutionAliasRedirect", 8);
    setString(L"AppExecutionAliasRedirectPackages", L"other-package");
    require(apply(false) == ERROR_SUCCESS);
    require(apply(true) == ERROR_ALREADY_EXISTS);
    require(string(L"Debugger") == foreign && dword(L"UseFilter") == 7);
    require(dword(L"AppExecutionAliasRedirect") == 8 && string(L"AppExecutionAliasRedirectPackages") == L"other-package");
    setDword(L"Debugger", 99); // non-string ownership also remains untouched
    require(apply(false) == ERROR_SUCCESS && apply(true) == ERROR_ALREADY_EXISTS);
    require(dword(L"Debugger") == 99);

    // RegSetValueEx checks the granted access of this real read-only HKCU handle.
    setString(L"Debugger", command);
    HKEY readOnly = nullptr;
    require(RegOpenKeyExW(HKEY_CURRENT_USER, fixture.path.c_str(), 0, KEY_QUERY_VALUE, &readOnly) == ERROR_SUCCESS);
    const LSTATUS enableStatus = ApplyNotepadReplacementToKey(readOnly, executable, true);
    const LSTATUS disableStatus = ApplyNotepadReplacementToKey(readOnly, executable, false);
    RegCloseKey(readOnly);
    require(enableStatus == ERROR_ACCESS_DENIED && disableStatus == ERROR_ACCESS_DENIED);
    require(string(L"Debugger") == command && dword(L"UseFilter") == 7);
    require(apply(false) == ERROR_SUCCESS);
    setDword(L"UseFilter", 5);
    require(apply(false) == ERROR_SUCCESS && dword(L"UseFilter") == 5); // no owned replacement, no writes
}

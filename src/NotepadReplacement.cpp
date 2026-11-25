#include "SystemIntegration.h"
#include <filesystem>
#include <string_view>

namespace {
bool IsMultiPad4FromAnotherPath(const std::wstring& debugger) {
    constexpr std::wstring_view suffix = L"\" /z";
    if (debugger.size() <= suffix.size() || debugger.front() != L'"' ||
        debugger.find(L'\0') != std::wstring::npos) return false;
    const auto closingQuote = debugger.find(L'"', 1);
    if (closingQuote != debugger.size() - suffix.size() ||
        _wcsicmp(debugger.c_str() + closingQuote, suffix.data()) != 0) return false;
    const std::filesystem::path previous(debugger.substr(1, closingQuote - 1));
    return previous.is_absolute() && _wcsicmp(previous.filename().c_str(), L"multipad4.exe") == 0;
}
}

LSTATUS ApplyNotepadReplacementToKey(HKEY key, const std::wstring& executable, bool enable) {
    const std::wstring command = L"\"" + executable + L"\" /z";
    DWORD type = 0, bytes = 0;
    LSTATUS status = RegQueryValueExW(key, L"Debugger", nullptr, &type, nullptr, &bytes);
    bool ownDebugger = false;
    if (status == ERROR_SUCCESS) {
        // Treat other registry value types and malformed commands as foreign.
        if (type != REG_SZ || bytes % sizeof(wchar_t) != 0) return enable ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
        std::wstring debugger(bytes / sizeof(wchar_t) + 1, L'\0');
        status = RegQueryValueExW(key, L"Debugger", nullptr, &type, reinterpret_cast<BYTE*>(debugger.data()), &bytes);
        if (status != ERROR_SUCCESS) return status;
        if (type != REG_SZ || bytes % sizeof(wchar_t) != 0) return enable ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
        debugger.resize(bytes / sizeof(wchar_t));
        if (!debugger.empty() && debugger.back() == L'\0') debugger.pop_back();
        ownDebugger = _wcsicmp(debugger.c_str(), command.c_str()) == 0 && debugger.size() == command.size();
        // Enabling from a new location can migrate an older MultiPad4 command.
        if (!ownDebugger && (!enable || !IsMultiPad4FromAnotherPath(debugger)))
            return enable ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
    } else if (status != ERROR_FILE_NOT_FOUND) return status;
    if (!enable && !ownDebugger) return ERROR_SUCCESS;

    auto setDword = [key](const wchar_t* name, DWORD value) {
        return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    };
    auto setString = [key](const wchar_t* name, const std::wstring& value) {
        return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    };
    auto remove = [key](const wchar_t* name) {
        const LSTATUS result = RegDeleteValueW(key, name);
        return result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
    };
    // Windows 11 supplies alias redirection here. Follow notepad4's replacement
    // and restoration values while preserving the system's key and filter subkeys.
    status = setDword(L"UseFilter", enable ? 0 : 1);
    if (status != ERROR_SUCCESS) return status;
    status = enable ? remove(L"AppExecutionAliasRedirect") : setDword(L"AppExecutionAliasRedirect", 1);
    if (status != ERROR_SUCCESS) return status;
    status = enable ? remove(L"AppExecutionAliasRedirectPackages") : setString(L"AppExecutionAliasRedirectPackages", L"*");
    if (status != ERROR_SUCCESS) return status;
    // Change Debugger last so a failed restore remains identifiable and retryable.
    return enable ? setString(L"Debugger", command) : remove(L"Debugger");
}

LSTATUS ApplyNotepadReplacement(HKEY hive, const wchar_t* path, const std::wstring& executable, bool enable, REGSAM view) {
    HKEY key = nullptr;
    const REGSAM access = KEY_QUERY_VALUE | KEY_SET_VALUE | view;
    LSTATUS status = enable ? RegCreateKeyExW(hive, path, 0, nullptr, 0, access, nullptr, &key, nullptr)
        : RegOpenKeyExW(hive, path, 0, access, &key);
    if (!enable && status == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (status != ERROR_SUCCESS) return status;
    status = ApplyNotepadReplacementToKey(key, executable, enable);
    RegCloseKey(key);
    return status;
}

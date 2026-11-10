#include "App.h"
#include "resource.h"
#include "SystemIntegration.h"
#include <shlobj.h>
#include <stdexcept>
namespace {
constexpr wchar_t ContextKey[] = L"Software\\Classes\\*\\shell\\MultiPad4";
constexpr wchar_t ApplicationKey[] = L"Software\\Classes\\Applications\\multipad4.exe";
constexpr wchar_t NotepadKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\notepad.exe";
std::wstring Executable() { std::wstring path(32768, L'\0'); path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))); return path; }
std::wstring AppId() {
    PWSTR value = nullptr;
    if (SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&value))) { std::wstring result = value; CoTaskMemFree(value); return result; }
    return L"MultiPad4.Editor";
}
std::wstring Read(HKEY hive, const wchar_t* key, const wchar_t* value, REGSAM view = 0) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(hive, key, 0, KEY_QUERY_VALUE | view, &handle) != ERROR_SUCCESS) return {};
    DWORD type = 0, bytes = 0; RegQueryValueExW(handle, value, nullptr, &type, nullptr, &bytes);
    std::wstring result(bytes / sizeof(wchar_t) + 1, L'\0');
    if (type != REG_SZ || RegQueryValueExW(handle, value, nullptr, &type, reinterpret_cast<BYTE*>(result.data()), &bytes) != ERROR_SUCCESS) result.clear();
    else result.resize(wcsnlen(result.c_str(), result.size()));
    RegCloseKey(handle); return result;
}
void Write(HKEY hive, const std::wstring& key, const wchar_t* name, const std::wstring& value, REGSAM view = 0) {
    HKEY handle = nullptr;
    if (RegCreateKeyExW(hive, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE | view, nullptr, &handle, nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot write registry integration. Administrator privileges may be required.");
    LSTATUS status = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle); if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot save registry value.");
}
INT_PTR CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetWindowTextW(window, L"MultiPad4 System Integration");
        SetDlgItemTextW(window, IDC_ENABLE_CONTEXT_MENU, L"Add MultiPad4 to Windows Explorer's context menu.");
        auto command = Read(HKEY_CURRENT_USER, (std::wstring(ContextKey) + L"\\command").c_str(), nullptr);
        CheckDlgButton(window, IDC_ENABLE_CONTEXT_MENU, command.find(Executable()) != std::wstring::npos);
        auto label = Read(HKEY_CURRENT_USER, ContextKey, nullptr);
        SetDlgItemTextW(window, IDC_CONTEXT_MENU_TEXT, label.empty() ? L"Edit with MultiPad4" : label.c_str());
        command = Read(HKEY_CURRENT_USER, (std::wstring(ApplicationKey) + L"\\shell\\open\\command").c_str(), nullptr);
        CheckDlgButton(window, IDC_ENABLE_JUMP_LIST, command.find(Executable()) != std::wstring::npos && Read(HKEY_CURRENT_USER, ApplicationKey, L"AppUserModelID") == AppId());
        label = Read(HKEY_CURRENT_USER, ApplicationKey, L"FriendlyAppName");
        SetDlgItemTextW(window, IDC_APPLICATION_NAME, label.empty() ? L"MultiPad4" : label.c_str());
        auto debugger = Read(HKEY_LOCAL_MACHINE, NotepadKey, L"Debugger", KEY_WOW64_64KEY);
        CheckDlgButton(window, IDC_REPLACE_WINDOWS_NOTEPAD, _wcsicmp(debugger.c_str(), (L"\"" + Executable() + L"\" /z").c_str()) == 0);
        // Replacement is available only in an elevated process, like upstream.
        BOOL administrator = FALSE; SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY; PSID sid = nullptr;
        if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid)) {
            CheckTokenMembership(nullptr, sid, &administrator); FreeSid(sid);
        }
        EnableWindow(GetDlgItem(window, IDC_REPLACE_WINDOWS_NOTEPAD), administrator);
        CWindow(window).CenterWindow(GetParent(window)); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) { EndDialog(window, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND || LOWORD(w) != IDOK) return FALSE;
    try {
        auto executable = Executable(); auto quoted = L"\"" + executable + L"\"";
        wchar_t label[512]{};
        if (IsDlgButtonChecked(window, IDC_ENABLE_CONTEXT_MENU)) {
            GetDlgItemTextW(window, IDC_CONTEXT_MENU_TEXT, label, _countof(label));
            Write(HKEY_CURRENT_USER, ContextKey, nullptr, label);
            Write(HKEY_CURRENT_USER, ContextKey, L"Icon", executable);
            Write(HKEY_CURRENT_USER, std::wstring(ContextKey) + L"\\command", nullptr, quoted + L" \"%1\"");
        } else RegDeleteTreeW(HKEY_CURRENT_USER, ContextKey);
        if (IsDlgButtonChecked(window, IDC_ENABLE_JUMP_LIST)) {
            GetDlgItemTextW(window, IDC_APPLICATION_NAME, label, _countof(label));
            Write(HKEY_CURRENT_USER, ApplicationKey, L"FriendlyAppName", label);
            Write(HKEY_CURRENT_USER, ApplicationKey, L"AppUserModelID", AppId());
            Write(HKEY_CURRENT_USER, std::wstring(ApplicationKey) + L"\\shell\\open\\command", nullptr, quoted + L" \"%1\"");
            Write(HKEY_CURRENT_USER, std::wstring(ApplicationKey) + L"\\SupportedTypes", L".txt", L"");
            Write(HKEY_CURRENT_USER, std::wstring(ApplicationKey) + L"\\SupportedTypes", L".log", L"");
        } else RegDeleteTreeW(HKEY_CURRENT_USER, ApplicationKey);
        if (IsWindowEnabled(GetDlgItem(window, IDC_REPLACE_WINDOWS_NOTEPAD))) {
            const LSTATUS status = ApplyNotepadReplacement(HKEY_LOCAL_MACHINE, NotepadKey, executable,
                IsDlgButtonChecked(window, IDC_REPLACE_WINDOWS_NOTEPAD) == BST_CHECKED, KEY_WOW64_64KEY);
            if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot update Notepad replacement registry values.");
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        EndDialog(window, IDOK);
    } catch (const std::exception&) { MessageBoxW(window, L"Cannot apply system integration. Check registry access and privileges.", L"MultiPad4", MB_ICONERROR); }
    return TRUE;
}
}
LSTATUS ApplyNotepadReplacementToKey(HKEY key, const std::wstring& executable, bool enable) {
    const std::wstring command = L"\"" + executable + L"\" /z";
    DWORD type = 0, bytes = 0;
    LSTATUS status = RegQueryValueExW(key, L"Debugger", nullptr, &type, nullptr, &bytes);
    bool ownDebugger = false;
    if (status == ERROR_SUCCESS) {
        // Never change another application's replacement, including malformed or
        // non-string Debugger values. Match the entire command, not a substring.
        if (type != REG_SZ || bytes % sizeof(wchar_t) != 0) return enable ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
        std::wstring debugger(bytes / sizeof(wchar_t) + 1, L'\0');
        status = RegQueryValueExW(key, L"Debugger", nullptr, &type, reinterpret_cast<BYTE*>(debugger.data()), &bytes);
        if (status != ERROR_SUCCESS) return status;
        debugger.resize(bytes / sizeof(wchar_t));
        if (!debugger.empty() && debugger.back() == L'\0') debugger.pop_back();
        ownDebugger = _wcsicmp(debugger.c_str(), command.c_str()) == 0 && debugger.size() == command.size();
        if (!ownDebugger) return enable ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
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

void ShowSystemIntegration(HWND owner) {
    DialogBoxParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDD_SYSTEM_INTEGRATION), owner, Procedure, 0);
}

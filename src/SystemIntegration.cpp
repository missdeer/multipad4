#include "App.h"
#include "resource.h"
#include "SystemIntegration.h"
#include <shlobj.h>
#include <filesystem>
#include <system_error>
namespace {
constexpr wchar_t ContextKey[] = L"Software\\Classes\\*\\shell\\MultiPad4";
constexpr wchar_t ApplicationKey[] = L"Software\\Classes\\Applications\\multipad4.exe";
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
void CheckRegistryStatus(LSTATUS status) {
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
        throw std::system_error(status, std::system_category());
}

void ShowError(HWND window, std::wstring message, DWORD status) {
    wchar_t detail[512]{};
    if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        status, 0, detail, _countof(detail), nullptr)) {
        message += L"\n\n";
        message += detail;
    }
    message += L"\nWindows error " + std::to_wstring(status) + L".";
    MessageBoxW(window, message.c_str(), L"MultiPad4", MB_ICONERROR);
}

bool ChangeNotepadReplacement(HWND window, const std::wstring& executable, bool enable) {
    auto helper = std::filesystem::path(executable).replace_filename(NotepadReplacementHelper);
    SHELLEXECUTEINFOW launch{sizeof(launch)};
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    launch.hwnd = window;
    launch.lpVerb = L"runas";
    launch.lpFile = helper.c_str();
    launch.lpParameters = enable ? L"--enable" : L"--disable";
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch)) {
        const DWORD error = GetLastError();
        if (error != ERROR_CANCELLED) ShowError(window, L"Could not start the MultiPad4 registry helper.", error);
        return false;
    }
    if (!launch.hProcess) {
        ShowError(window, L"Could not get the MultiPad4 registry helper's result.", ERROR_INVALID_HANDLE);
        return false;
    }
    DWORD status = ERROR_SUCCESS;
    if (WaitForSingleObject(launch.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(launch.hProcess, &status)) {
        status = GetLastError();
    }
    CloseHandle(launch.hProcess);
    if (status == ERROR_SUCCESS) return true;
    if (status == ERROR_ALREADY_EXISTS) {
        const bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
        MessageBoxW(window, chinese
            ? L"Windows 记事本已被其他程序替换。要改用 MultiPad4，请先在原程序中取消替换。\n\n"
              L"如果只想设置右键菜单或任务栏跳转列表，请取消勾选“通过注册表替换 Windows 记事本”后再保存。"
            : L"Another application is registered as the Windows Notepad replacement. Turn off the replacement in that application first.\n\n"
              L"To save only the Explorer context menu or Jump List settings, clear the Notepad replacement checkbox and try again.",
            L"MultiPad4", MB_ICONERROR);
    } else {
        ShowError(window, L"Could not update the Windows Notepad replacement.", status);
    }
    return false;
}

void Write(HKEY hive, const std::wstring& key, const wchar_t* name, const std::wstring& value, REGSAM view = 0) {
    HKEY handle = nullptr;
    const LSTATUS openStatus = RegCreateKeyExW(hive, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE | view, nullptr, &handle, nullptr);
    if (openStatus != ERROR_SUCCESS) throw std::system_error(openStatus, std::system_category());
    const LSTATUS status = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    if (status != ERROR_SUCCESS) throw std::system_error(status, std::system_category());
}
INT_PTR CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetWindowTextW(window, L"MultiPad4 System Integration");
        SetDlgItemTextW(window, IDC_GROUP_SYSTEM_INTEGRATION, L"System Integration");
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
        const bool replaced = _wcsicmp(debugger.c_str(), (L"\"" + Executable() + L"\" /z").c_str()) == 0;
        CheckDlgButton(window, IDC_REPLACE_WINDOWS_NOTEPAD, replaced);
        SetWindowLongPtrW(window, DWLP_USER, replaced);
        CenterWindow(window, GetParent(window)); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) { EndDialog(window, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND || LOWORD(w) != IDOK) return FALSE;
    try {
        auto executable = Executable(); auto quoted = L"\"" + executable + L"\"";
        const bool replaceNotepad = IsDlgButtonChecked(window, IDC_REPLACE_WINDOWS_NOTEPAD) == BST_CHECKED;
        if (replaceNotepad != (GetWindowLongPtrW(window, DWLP_USER) != 0)) {
            if (!ChangeNotepadReplacement(window, executable, replaceNotepad)) return TRUE;
            SetWindowLongPtrW(window, DWLP_USER, replaceNotepad);
        }
        wchar_t label[512]{};
        if (IsDlgButtonChecked(window, IDC_ENABLE_CONTEXT_MENU)) {
            GetDlgItemTextW(window, IDC_CONTEXT_MENU_TEXT, label, _countof(label));
            Write(HKEY_CURRENT_USER, ContextKey, nullptr, label);
            Write(HKEY_CURRENT_USER, ContextKey, L"Icon", executable);
            Write(HKEY_CURRENT_USER, std::wstring(ContextKey) + L"\\command", nullptr, quoted + L" \"%1\"");
        } else CheckRegistryStatus(RegDeleteTreeW(HKEY_CURRENT_USER, ContextKey));
        if (IsDlgButtonChecked(window, IDC_ENABLE_JUMP_LIST)) {
            GetDlgItemTextW(window, IDC_APPLICATION_NAME, label, _countof(label));
            Write(HKEY_CURRENT_USER, ApplicationKey, L"FriendlyAppName", label);
            Write(HKEY_CURRENT_USER, ApplicationKey, L"AppUserModelID", AppId());
            Write(HKEY_CURRENT_USER, std::wstring(ApplicationKey) + L"\\shell\\open\\command", nullptr, quoted + L" \"%1\"");
            Write(HKEY_CURRENT_USER, std::wstring(ApplicationKey) + L"\\SupportedTypes", L".txt", L"");
            Write(HKEY_CURRENT_USER, std::wstring(ApplicationKey) + L"\\SupportedTypes", L".log", L"");
        } else CheckRegistryStatus(RegDeleteTreeW(HKEY_CURRENT_USER, ApplicationKey));
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        EndDialog(window, IDOK);
    } catch (const std::system_error& error) {
        ShowError(window, L"Could not update your Explorer integration settings.", error.code().value());
    }
    return TRUE;
}
}
void ShowSystemIntegration(HWND owner) {
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SYSTEM_INTEGRATION), owner, Procedure, 0);
}

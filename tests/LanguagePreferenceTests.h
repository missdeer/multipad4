#pragma once
#include "../src/MainFrame.h"
#include "../src/FrameCommands.h"
#include "QAArtifacts.h"
#include <functional>
#include <source_location>
#include <stdexcept>

namespace languagepreferencetest {
inline thread_local HWND owner = nullptr;
inline thread_local std::function<void(HWND)> inspect;
inline thread_local UINT answer = IDCANCEL;
inline thread_local bool activated = false;
inline thread_local std::exception_ptr failure;
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(owner);
    wchar_t type[32]{};
    GetClassNameW(dialog, type, _countof(type));
    if (!dialog || dialog == owner || wcscmp(type, L"#32770") != 0) return;
    KillTimer(nullptr, timer); activated = true;
    try { inspect(dialog); } catch (...) { failure = std::current_exception(); }
    PostMessageW(dialog, WM_COMMAND, failure ? IDCANCEL : answer, 0);
}
inline bool Select(MainFrame& frame, UINT command, UINT result, std::function<void(HWND)> callback) {
    owner = frame; inspect = std::move(callback); answer = result; activated = false; failure = nullptr;
    const auto timer = SetTimer(nullptr, 0, 10, Timer);
    if (!timer) throw std::runtime_error("Language preference timer failed");
    ExecuteFrameCommand(frame, command);
    KillTimer(nullptr, timer); owner = nullptr; inspect = {};
    if (failure) std::rethrow_exception(failure);
    return activated;
}
// Consume the posted restart before the application message loop can dispatch it.
// Preserve any unrelated commands that were queued for this frame.
inline int TakeRestarts(HWND frame) {
    MSG message{};
    std::vector<MSG> other;
    int count = 0;
    while (PeekMessageW(&message, frame, WM_COMMAND, WM_COMMAND, PM_REMOVE)) {
        if (message.hwnd == frame && LOWORD(message.wParam) == IDM_FILE_RESTART) ++count;
        else other.push_back(message);
    }
    for (const auto& item : other) PostMessageW(item.hwnd, item.message, item.wParam, item.lParam);
    return count;
}
inline void require(bool ok, std::source_location where = std::source_location::current()) {
    if (!ok) throw std::runtime_error("Language preference assertion at line " + std::to_string(where.line()));
}
inline std::wstring menuText(HMENU menu, UINT item, UINT mode) {
    wchar_t text[128]{};
    GetMenuStringW(menu, item, text, _countof(text), mode);
    return text;
}
}

inline void RunWindowMenuTests(MainFrame& frame) {
    using namespace languagepreferencetest;
    const HMENU menuBar = frame.GetMenu();
    int windowPosition = 0;
    while (windowPosition < GetMenuItemCount(menuBar) && GetSubMenu(menuBar, windowPosition) != frame.windowMenu) ++windowPosition;
    require(menuText(menuBar, windowPosition, MF_BYPOSITION) == L"&Window");
    require(menuText(frame.windowMenu, ID_NEXTTAB, MF_BYCOMMAND) == L"Next Tab\tCtrl+Tab");
    require(menuText(frame.windowMenu, ID_PREVTAB, MF_BYCOMMAND) == L"Previous Tab\tCtrl+Shift+Tab");

    const auto language = GetThreadUILanguage();
    SetThreadUILanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
    UniqueMenu chineseMenu(LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_MAINFRAME)));
    SetThreadUILanguage(language);
    require(chineseMenu != nullptr);
    require(menuText(chineseMenu.get(), 0, MF_BYPOSITION) == L"文件(&F)");
    const int position = GetMenuItemCount(chineseMenu.get()) - 2;
    require(menuText(chineseMenu.get(), position, MF_BYPOSITION) == L"窗口(&W)");
    const auto popup = GetSubMenu(chineseMenu.get(), position);
    require(menuText(popup, ID_NEXTTAB, MF_BYCOMMAND) == L"下一个标签页\tCtrl+Tab");
    require(menuText(popup, ID_WINDOW_TILE_VERT, MF_BYCOMMAND) == L"垂直平铺");
}

inline void RunLanguagePreferenceTests(MainFrame& frame) {
    using namespace languagepreferencetest;
    const HMENU menuBar = frame.GetMenu(), pluginMenu = frame.plugins.menu;
    int pluginPosition = 0;
    while (pluginPosition < GetMenuItemCount(menuBar) && GetSubMenu(menuBar, pluginPosition) != pluginMenu) ++pluginPosition;
    require(menuText(menuBar, pluginPosition, MF_BYPOSITION) == L"&Plugins");
    require(menuText(pluginMenu, IDM_SETTING_PLUGINADM, MF_BYCOMMAND) == L"Plugins Admin...");
    require(menuText(pluginMenu, IDM_SETTING_OPENPLUGINSDIR, MF_BYCOMMAND) == L"Open Plugins Folder...");
    if (frame.plugins.plugins.empty()) {
        require(menuText(pluginMenu, 3, MF_BYPOSITION) == L"No compatible plugins loaded");
    } else {
        for (int position = 3; position < GetMenuItemCount(pluginMenu); ++position) {
            const auto name = menuText(pluginMenu, position, MF_BYPOSITION);
            const auto submenu = GetSubMenu(pluginMenu, position);
            if (name == L"Matepath") require(menuText(submenu, 0, MF_BYPOSITION) == L"File Browser");
            if (name == L"OpenDocuments") require(menuText(submenu, 0, MF_BYPOSITION) == L"Open Documents");
        }
    }
    {
        PluginAdmin admin;
        struct RestoreAdmin {
            PluginAdmin& admin;
            LANGID language;
            ~RestoreAdmin() { admin.Close(); SetThreadUILanguage(language); }
        } restoreAdmin{admin, GetThreadUILanguage()};
        const struct {
            LANGID language;
            const wchar_t* title;
            const wchar_t* install;
            const wchar_t* incompatible;
            const wchar_t* column;
            const wchar_t* nameField;
            const wchar_t* artifact;
        } locales[] = {
            {MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), L"Plugins Admin", L"&Install", L"Incompatible / Load Failed", L"Plugin", L"Name: ", L"PluginAdmin-English"},
            {MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED), L"插件管理", L"安装(&I)", L"不兼容 / 加载失败", L"插件", L"名称：", L"PluginAdmin-Chinese"}
        };
        for (const auto& locale : locales) {
            SetThreadUILanguage(locale.language);
            admin.Show(frame);
            require(IsWindow(admin.hwnd) && TabCtrl_GetItemCount(admin.tab) == 5);
            wchar_t text[4096]{};
            GetWindowTextW(admin.hwnd, text, _countof(text)); require(std::wstring_view(text) == locale.title);
            GetDlgItemTextW(admin.hwnd, IDC_PLUGINADM_INSTALL, text, _countof(text)); require(std::wstring_view(text) == locale.install);
            TCITEMW tab{}; tab.mask = TCIF_TEXT; tab.pszText = text; tab.cchTextMax = _countof(text);
            require(TabCtrl_GetItem(admin.tab, 3, &tab) && std::wstring_view(text) == locale.incompatible);
            LVCOLUMNW column{}; column.mask = LVCF_TEXT; column.pszText = text; column.cchTextMax = _countof(text);
            require(ListView_GetColumn(admin.list, 0, &column) && std::wstring_view(text) == locale.column);
            require(!admin.visible.empty());
            admin.Describe();
            GetDlgItemTextW(admin.hwnd, IDC_PLUGINADM_EDIT, text, _countof(text));
            require(wcsstr(text, locale.nameField) != nullptr);
            CaptureQAWindow(admin.hwnd, locale.artifact);
            admin.Close();
        }
    }
    struct Restore {
        MainFrame& frame;
        AppSettings settings;
        ~Restore() { TakeRestarts(frame); frame.settings = settings; }
    } restore{frame, frame.settings};
    frame.settings = AppSettings{};
    frame.settings.persistent = false;
    frame.settings.Set(IDM_LANG_USER_DEFAULT, IDM_LANG_ENGLISH_US);
    frame.settings.WriteString(L"Language Regression", L"Unrelated", L"preserved");
    require(TakeRestarts(frame) == 0);
    auto selection = [&] { return frame.settings.Get(IDM_LANG_USER_DEFAULT, IDM_LANG_USER_DEFAULT); };
    auto unchanged = [&] {
        require(selection() == IDM_LANG_ENGLISH_US);
        require(frame.State(IDM_LANG_ENGLISH_US).checked && !frame.State(IDM_LANG_JAPANESE).checked);
        require(frame.settings.ReadString(L"Language Regression", L"Unrelated") == L"preserved");
    };
    auto inspectPrompt = [&](HWND dialog) {
        require(GetDlgItem(dialog, IDYES) && GetDlgItem(dialog, IDNO) && GetDlgItem(dialog, IDCANCEL));
        wchar_t expected[1024]{}, actual[1024]{};
        require(LoadStringW(GetModuleHandleW(nullptr), IDS_CHANGE_LANG_RESTART, expected, _countof(expected)) > 0);
        GetDlgItemTextW(dialog, 0xFFFF, actual, _countof(actual));
        require(std::wstring(actual) == expected);
        unchanged(); // No preference changes before an answer is accepted.
    };
    require(Select(frame, IDM_LANG_JAPANESE, IDCANCEL, inspectPrompt));
    unchanged(); require(TakeRestarts(frame) == 0);
    require(!Select(frame, IDM_LANG_ENGLISH_US, IDYES, inspectPrompt));
    unchanged(); require(TakeRestarts(frame) == 0);
    require(Select(frame, IDM_LANG_JAPANESE, IDNO, inspectPrompt));
    require(selection() == IDM_LANG_JAPANESE && frame.State(IDM_LANG_JAPANESE).checked);
    require(!frame.State(IDM_LANG_ENGLISH_US).checked && TakeRestarts(frame) == 0);
    require(!Select(frame, IDM_LANG_JAPANESE, IDYES, [](HWND) {}));
    require(selection() == IDM_LANG_JAPANESE && TakeRestarts(frame) == 0);
    frame.settings.Set(IDM_LANG_USER_DEFAULT, IDM_LANG_ENGLISH_US);
    require(Select(frame, IDM_LANG_JAPANESE, IDYES, inspectPrompt));
    require(selection() == IDM_LANG_JAPANESE);
    require(TakeRestarts(frame) == 1);
    // User default is a distinct preference even when it resolves to English.
    frame.settings.Set(IDM_LANG_USER_DEFAULT, IDM_LANG_USER_DEFAULT);
    require(!Select(frame, IDM_LANG_USER_DEFAULT, IDYES, [](HWND) {}));
    require(selection() == IDM_LANG_USER_DEFAULT && TakeRestarts(frame) == 0);
    require(frame.settings.ReadString(L"Language Regression", L"Unrelated") == L"preserved");
}

#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <shellapi.h>

inline HWND MatepathTestWindow() {
    HWND result = nullptr;
    auto find = [](HWND window, LPARAM context) -> BOOL {
        wchar_t name[96]{}; GetClassNameW(window, name, _countof(name));
        if (wcscmp(name, L"MultiPad4.MatepathPane") == 0) *reinterpret_cast<HWND*>(context) = window;
        return TRUE;
    };
    std::pair<WNDENUMPROC, HWND*> state{find, &result};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND top, LPARAM context) -> BOOL {
        auto* state = reinterpret_cast<std::pair<WNDENUMPROC, HWND*>*>(context);
        state->first(top, reinterpret_cast<LPARAM>(state->second));
        EnumChildWindows(top, state->first, reinterpret_cast<LPARAM>(state->second));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    return result;
}
inline void RunMatepathInputRegression(MainFrame& frame) {
    HWND pane = MatepathTestWindow();
    if (!pane) return;
    HWND list = GetDlgItem(pane, 200), bar = GetParent(pane);
    MSG input{}; input.hwnd = list; input.message = WM_KEYDOWN; input.wParam = VK_TAB;
    if (!frame.plugins.PreTranslate(&input) || GetParent(pane) != bar)
        throw std::runtime_error("Matepath plugin Tab input corrupts docking ownership");
    for (UINT pointer : {WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP}) {
        input.message = pointer;
        if (frame.plugins.PreTranslate(&input)) throw std::runtime_error("Matepath filters native pointer input");
    }
}
inline void RunMatepathPaneTests(MainFrame& frame, const std::filesystem::path& root) {
    bool loaded = false;
    for (const auto& plugin : frame.plugins.plugins) if (plugin.module == L"Matepath.dll") loaded = true;
#ifdef MULTIPAD4_TEST_MATEPATH
    if (frame.plugins.onlyModule == L"Matepath.dll" && !loaded) throw std::runtime_error("Matepath DLL was not loaded");
#endif
    if (!loaded) return;
    HWND pane = MatepathTestWindow();
    auto require = [](bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("Matepath plugin integration failed at line " + std::to_string(where.line())); };
    require(pane != nullptr);
    constexpr UINT action = WM_APP + 0x110;
    constexpr int back = 100, forward = 101, up = 102, go = 104, filter = 105, hidden = 106, address = 114, pattern = 115;
    // Control IDs are private UI details; all calls cross the actual DLL boundary.
    HWND list = GetDlgItem(pane, 200);
    auto command = [&](int id) { SendMessageW(pane, WM_COMMAND, id, 0); };
    auto navigate = [&](const std::filesystem::path& path) { SetDlgItemTextW(pane, address, path.c_str()); command(go); };
    auto select = [&](const wchar_t* name) {
        SHFILEINFOW display{};
        require(SHGetFileInfoW(name, 0, &display, sizeof(display), SHGFI_DISPLAYNAME) != 0);
        LVFINDINFOW info{}; info.flags = LVFI_STRING; info.psz = display.szDisplayName;
        int index = static_cast<int>(SendMessageW(list, LVM_FINDITEMW, -1, reinterpret_cast<LPARAM>(&info)));
        require(index >= 0);
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    };
    auto activate = [&] {
        MSG input{}; input.hwnd = list; input.message = WM_KEYDOWN; input.wParam = VK_RETURN;
        require(frame.plugins.PreTranslate(&input));
    };
    const auto cwd = std::filesystem::current_path();
    const auto folder = root / L"浏览目录";
    std::filesystem::create_directories(folder / L"子目录");
    std::ofstream(folder / L"中文.txt") << "matepath text";
    std::ofstream(folder / L"other.cpp") << "int other;";
    std::ofstream(folder / L"hidden.txt") << "hidden";
    require(SetFileAttributesW((folder / L"hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE);
    SendMessageW(pane, action, 2, 0);
    SetDlgItemTextW(pane, pattern, L"*.*"); command(filter);
    navigate(folder);
    // Normalize persisted hidden-file visibility using the actual list.
    if (ListView_GetItemCount(list) == 4) command(hidden);
    require(ListView_GetItemCount(list) == 3);
    const auto dismissTimer = SetTimer(nullptr, 0, 50, [](HWND, UINT, UINT_PTR, DWORD) {
        EnumThreadWindows(GetCurrentThreadId(), [](HWND dialog, LPARAM) -> BOOL {
            wchar_t caption[64]{}; GetWindowTextW(dialog, caption, _countof(caption));
            if (wcscmp(caption, L"Matepath") == 0) SendMessageW(dialog, WM_CLOSE, 0, 0);
            return TRUE;
        }, 0);
    });
    navigate(folder / L"missing"); require(ListView_GetItemCount(list) == 3);
    navigate(std::wstring(MAX_PATH + 5, L'x')); require(ListView_GetItemCount(list) == 3);
    KillTimer(nullptr, dismissTimer);
    SetDlgItemTextW(pane, pattern, L"*.txt"); command(filter);
    require(ListView_GetItemCount(list) == 2);
    command(hidden); require(ListView_GetItemCount(list) == 3); command(hidden);
    select((folder / L"子目录").c_str()); activate(); require(ListView_GetItemCount(list) == 0);
    command(back); require(ListView_GetItemCount(list) == 2);
    command(forward); require(ListView_GetItemCount(list) == 0);
    command(up); require(ListView_GetItemCount(list) == 2);
    select((folder / L"中文.txt").c_str());
    const auto count = frame.documents.size();
    auto* original = frame.Active(); const auto originalText = original->Text();
    activate(); require(frame.documents.size() == count + 1 && frame.Active()->Text() == "matepath text");
    require(original->Text() == originalText);
    activate(); require(frame.documents.size() == count + 1);
    SendMessageW(pane, action, 10, 0);
    require(ListView_GetNextItem(list, -1, LVNI_SELECTED) >= 0);
    RunMatepathInputRegression(frame);
    for (int side : {1, 3, 4, 2}) {
        SendMessageW(pane, action, side, 0);
        VerifyPaneLayoutMembership(frame, pane, side != 4);
        SendMessageW(pane, action, 0, 0); require(!IsWindowVisible(pane));
        SendMessageW(pane, action, 0, 0);
        VerifyPaneLayoutMembership(frame, pane, side != 4);
    }
    frame.UpdateLayout(); CaptureQAWindow(frame, L"Matepath-plugin-docked");
    SendMessageW(pane, action, 4, 0); CaptureQAWindow(pane, L"Matepath-plugin-floating");
    SendMessageW(pane, action, 2, 0);
    require(frame.Close(frame.tabs.GetActivePage()));
    navigate(cwd);
    require(std::filesystem::current_path() == cwd);
}

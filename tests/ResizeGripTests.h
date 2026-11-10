#pragma once
#include "../src/MainFrame.h"
#include "../src/FileDialogs.h"
#include "../src/EditorCommands.h"
#include "../src/StyleManager.h"
#include "QAArtifacts.h"
#include <functional>

namespace ResizeGripTest {
inline thread_local HWND owner = nullptr;
inline thread_local int gripId = 0;
inline thread_local bool opened = false;
inline thread_local unsigned ticks = 0;
inline thread_local std::exception_ptr failure;

inline void Check(HWND dialog) {
    HWND grip = GetDlgItem(dialog, gripId);
    wchar_t type[32]{};
    GetClassNameW(grip, type, _countof(type));
    if (!grip || _wcsicmp(type, L"ScrollBar") || !IsWindowVisible(grip))
        throw std::runtime_error("Dialog sizing grip is missing or hidden.");
    RECT corner{}, client{};
    GetWindowRect(grip, &corner);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&corner), 2);
    GetClientRect(dialog, &client);
    if (corner.right != client.right || corner.bottom != client.bottom || corner.left < 0 || corner.top < 0)
        throw std::runtime_error("Dialog sizing grip is not at the client corner.");
    for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (child == grip) continue;
        GetClassNameW(child, type, _countof(type));
        if (_wcsicmp(type, L"Edit") == 0 && !IsWindowVisible(child))
            throw std::runtime_error("Dialog text input was hidden while positioning its sizing grip.");
        if (!IsWindowVisible(child)) continue;
        // Labels and text inputs exposed the placeholder regression. Buttons
        // can have transparent corner padding within the native grip bounds.
        if (_wcsicmp(type, L"Static") && _wcsicmp(type, L"Edit") && _wcsicmp(type, L"ComboBox") &&
            _wcsicmp(type, L"SysListView32") && _wcsicmp(type, L"SysTreeView32") && _wcsicmp(type, L"SysLink")) continue;
        RECT control{}, overlap{};
        GetWindowRect(child, &control);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&control), 2);
        if (IntersectRect(&overlap, &corner, &control))
            throw std::runtime_error("Dialog sizing grip overlaps content control " + std::to_string(GetDlgCtrlID(child)));
    }
}
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(owner);
    wchar_t type[32]{};
    GetClassNameW(dialog, type, _countof(type));
    // Shell icon loading can pump timer messages during WM_INITDIALOG,
    // before DialogBox shows the dialog and its child controls.
    if (dialog && dialog != owner && wcscmp(type, L"#32770") == 0 && IsWindowVisible(dialog)) {
        wchar_t dialogTitle[128]{};
        GetWindowTextW(dialog, dialogTitle, _countof(dialogTitle));
        fwprintf(stderr, L"Sizing grip: dialog=%ls visible=%d gripId=%d gripVisible=%d\n",
                 dialogTitle, IsWindowVisible(dialog), gripId, IsWindowVisible(GetDlgItem(dialog, gripId)));
        KillTimer(nullptr, timer);
        opened = true;
        try {
            Check(dialog);
            wchar_t title[128]{};
            GetWindowTextW(dialog, title, _countof(title));
            for (auto& character : title)
                if (wcschr(L"\\/:*?\"<>|", character) && character) character = L'-';
            CaptureQAWindow(dialog, (std::wstring(title) + L"-grip-initial").c_str());
            RECT outer{};
            GetWindowRect(dialog, &outer);
            SetWindowPos(dialog, nullptr, 0, 0, outer.right - outer.left + 160, outer.bottom - outer.top + 100,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            Check(dialog);
            CaptureQAWindow(dialog, (std::wstring(title) + L"-grip-enlarged").c_str());
        } catch (...) { failure = std::current_exception(); }
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    } else if (++ticks > 200) {
        KillTimer(nullptr, timer);
        failure = std::make_exception_ptr(std::runtime_error("Sizing grip test dialog did not open."));
        PostMessageW(owner, WM_CANCELMODE, 0, 0);
    }
}
inline void Run(HWND frame, int id, const std::function<void()>& show) {
    owner = frame; gripId = id; opened = false; ticks = 0; failure = nullptr;
    UINT_PTR timer = SetTimer(nullptr, 0, 10, Timer);
    if (!timer) throw std::runtime_error("Cannot install sizing grip regression timer.");
    try { show(); } catch (...) { KillTimer(nullptr, timer); owner = nullptr; throw; }
    KillTimer(nullptr, timer); owner = nullptr;
    if (failure) std::rethrow_exception(failure);
    if (!opened) throw std::runtime_error("Sizing grip test did not reach modal dialog.");
}
}

inline void RunResizeGripTests(MainFrame& frame) {
    using ResizeGripTest::Run;
    auto* document = frame.NewDocument();
    if (!document) throw std::runtime_error("Cannot create sizing grip test document.");
    document->Load({"sizing grip"});
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-grips-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    document->path = directory / L"target.txt";
    const auto oldFavorites = frame.settings.ReadString(L"Files", L"FavoritesDirectory");
    const auto oldOpenWith = frame.settings.ReadString(L"Files", L"OpenWithDirectory");
    const auto oldRunWidth = frame.settings.ReadString(L"Dialog Widths", std::to_wstring(IDD_RUN).c_str());
    const auto oldFavoriteWidth = frame.settings.ReadString(L"Dialog Widths", std::to_wstring(IDD_ADDTOFAV).c_str());
    std::vector<std::pair<std::wstring, std::wstring>> sizes;
    for (const auto* name : {L"Recent", L"Favorites", L"OpenWith"}) {
        sizes.emplace_back(name, frame.settings.ReadString(L"Dialog Sizes", name));
        frame.settings.WriteString(L"Dialog Sizes", name, L"");
    }
    auto restore = [&] {
        frame.settings.WriteString(L"Files", L"FavoritesDirectory", oldFavorites);
        frame.settings.WriteString(L"Files", L"OpenWithDirectory", oldOpenWith);
        frame.settings.WriteString(L"Dialog Widths", std::to_wstring(IDD_RUN).c_str(), oldRunWidth);
        frame.settings.WriteString(L"Dialog Widths", std::to_wstring(IDD_ADDTOFAV).c_str(), oldFavoriteWidth);
        for (const auto& item : sizes) frame.settings.WriteString(L"Dialog Sizes", item.first.c_str(), item.second);
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    };
    try {
        frame.settings.WriteString(L"Files", L"FavoritesDirectory", directory.wstring());
        frame.settings.WriteString(L"Files", L"OpenWithDirectory", directory.wstring());
        frame.settings.WriteString(L"Dialog Widths", std::to_wstring(IDD_RUN).c_str(), L"");
        frame.settings.WriteString(L"Dialog Widths", std::to_wstring(IDD_ADDTOFAV).c_str(), L"");
        Run(frame, IDC_RESIZEGRIP, [&] { SelectHistoryFile(frame, false); });
        Run(frame, IDC_RESIZEGRIP3, [&] { SelectHistoryFile(frame, true); });
        Run(frame, IDC_RESIZEGRIP3, [&] { OpenWithFileDialog(frame); });
        Run(frame, IDC_RESIZEGRIP3, [&] { RunFileDialog(frame); });
        Run(frame, IDC_RESIZEGRIP, [&] { AddFavoriteFile(frame); });
        files::Encoding encoding = files::Encoding::Utf8;
        Run(frame, IDC_RESIZEGRIP, [&] { SelectFileEncoding(frame, encoding); });
        for (UINT command : {IDM_EDIT_MODIFYLINES, IDM_EDIT_ENCLOSESELECTION, IDM_EDIT_INSERT_XMLTAG}) {
            document->Send(SCI_SELECTALL);
            Run(frame, IDC_RESIZEGRIP2, [&] { ExecuteEditorCommand(*document, command); });
        }
        for (UINT command : {IDM_VIEW_SCHEME, IDM_VIEW_SCHEME_CONFIG, IDM_VIEW_SCHEME_FAVORITE})
            Run(frame, IDC_RESIZEGRIP3, [&] { ExecuteStyleCommand(frame, *document, command); });
        if (!frame.Close(frame.tabs.GetActivePage())) throw std::runtime_error("Sizing grip test document did not close.");
        restore();
    } catch (...) { restore(); throw; }
}

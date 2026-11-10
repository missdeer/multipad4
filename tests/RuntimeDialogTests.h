#pragma once
#include "../src/MainFrame.h"
#include "../src/InfoDialog.h"
#include "../src/EditorDialogs.h"
#include "../src/SystemIntegration.h"
#include "../src/FrameCommands.h"
#include "../src/AutoSave.h"
#include "QAArtifacts.h"
#include <functional>
#include <source_location>
namespace runtimedialogtest {
inline thread_local HWND owner = nullptr;
inline thread_local std::function<void(HWND)> action;
inline thread_local std::exception_ptr failure;
inline thread_local bool activated = false;
inline thread_local unsigned ticks = 0;
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(owner); wchar_t type[32]{}; GetClassNameW(dialog, type, _countof(type));
    if (dialog && dialog != owner && wcscmp(type, L"#32770") == 0) {
        KillTimer(nullptr, timer); activated = true;
        try { if (action) action(dialog); CaptureQAWindow(dialog, L"runtime-dialog"); }
        catch (...) { failure = std::current_exception(); PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0); }
    } else if (++ticks > 100) { KillTimer(nullptr, timer); failure = std::make_exception_ptr(std::runtime_error("Runtime dialog did not activate.")); }
}
inline void Run(HWND frame, const std::function<void()>& show, const std::function<void(HWND)>& callback) {
    owner = frame; action = callback; activated = false; ticks = 0; failure = nullptr;
    auto timer = SetTimer(nullptr, 0, 10, Timer);
    if (!timer) throw std::runtime_error("Runtime dialog test timer failed.");
    show(); KillTimer(nullptr, timer); action = {}; owner = nullptr;
    if (failure) std::rethrow_exception(failure);
    if (!activated) throw std::runtime_error("Runtime dialog was not exercised.");
}
}
inline void RunRuntimeDialogTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Runtime dialog assertion at line " + std::to_string(where.line()));
    };
    using runtimedialogtest::Run;
    AppSettings savedSettings = frame.settings;
    Run(frame, [&] { require(ShowInfoDialog(frame, frame.settings, L"Regression message", L"MultiPad4", MB_OK, L"RegressionInfo") == IDOK); },
        [](HWND dialog) { CheckDlgButton(dialog, IDC_INFOBOXCHECK, BST_CHECKED); PostMessageW(dialog, WM_COMMAND, IDOK, 0); });
    require(frame.settings.ReadString(L"Suppressed Messages", L"RegressionInfo") == L"1");
    require(ShowInfoDialog(frame, frame.settings, L"Suppressed", L"MultiPad4", MB_YESNO, L"RegressionInfo") == IDYES);
    Run(frame, [&] { require(ShowInfoDialog(frame, frame.settings, L"Decline this conversion.", L"MultiPad4", MB_OKCANCEL, L"DeclinedInfo") == IDCANCEL); },
        [](HWND dialog) { CheckDlgButton(dialog, IDC_INFOBOXCHECK, BST_CHECKED); PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0); });
    require(ShowInfoDialog(frame, frame.settings, L"Declined remains declined.", L"MultiPad4", MB_OKCANCEL, L"DeclinedInfo") == IDCANCEL);
    frame.settings.WriteString(L"Suppressed Messages", L"NeverSuppress", L"2");
    Run(frame, [&] { require(ShowInfoDialog(frame, frame.settings, L"Never suppress this confirmation.", L"MultiPad4", MB_OKCANCEL, L"NeverSuppress") == IDCANCEL); },
        [&](HWND dialog) { require(!IsWindowEnabled(GetDlgItem(dialog, IDC_INFOBOXCHECK))); PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0); });
    Run(frame, [&] { ShowAboutDialog(frame); }, [&](HWND dialog) {
        wchar_t version[256]{}; GetDlgItemTextW(dialog, IDC_VERSION, version, _countof(version));
        require(wcsstr(version, L"MultiPad4") != nullptr); PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    Run(frame, [&] { ShowSystemIntegration(frame); }, [](HWND dialog) { PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0); });
    auto* document = frame.Active(); require(document != nullptr);
    auto oldMode = document->runtime.monitorMode; auto oldLog = document->runtime.monitorLog;
    auto oldKeep = document->runtime.keepAtEnd; auto oldReset = document->runtime.resetWatch;
    Run(frame, [&] { ExecuteFrameCommand(frame, IDM_VIEW_CHANGENOTIFY); }, [](HWND dialog) {
        CheckRadioButton(dialog, IDC_CHANGENOTIFY_NONE, IDC_CHANGENOTIFY_AUTO_RELOAD, IDC_CHANGENOTIFY_AUTO_RELOAD);
        CheckDlgButton(dialog, IDC_CHANGENOTIFY_LOG_FILE, BST_CHECKED);
        CheckDlgButton(dialog, IDC_CHANGENOTIFY_KEEP_AT_END, BST_CHECKED);
        PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require(document->runtime.monitorMode == 2 && document->runtime.monitorLog && document->runtime.keepAtEnd);
    Run(frame, [&] { ExecuteFrameCommand(frame, IDM_VIEW_CHANGENOTIFY); }, [](HWND dialog) {
        CheckRadioButton(dialog, IDC_CHANGENOTIFY_NONE, IDC_CHANGENOTIFY_AUTO_RELOAD, IDC_CHANGENOTIFY_NONE);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(document->runtime.monitorMode == 2);
    document->runtime.monitorMode = oldMode; document->runtime.monitorLog = oldLog;
    document->runtime.keepAtEnd = oldKeep; document->runtime.resetWatch = oldReset; SnapshotDocument(*document);
    Run(frame, [&] { ExecuteFrameCommand(frame, IDM_SET_FILE_AUTOSAVE); }, [](HWND dialog) {
        CheckDlgButton(dialog, IDC_AUTOSAVE_ENABLE, BST_CHECKED);
        CheckDlgButton(dialog, IDC_AUTOSAVE_SUSPEND, BST_CHECKED);
        SetDlgItemInt(dialog, IDC_AUTOSAVE_PERIOD, 31, FALSE); PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require((frame.settings.Get(IDM_SET_FILE_AUTOSAVE) & 3) == 3 && AutoSavePeriodMilliseconds(frame) == 31000);
    Run(frame, [&] { ExecuteFrameCommand(frame, IDM_SET_FILE_AUTOSAVE); }, [](HWND dialog) {
        SetDlgItemInt(dialog, IDC_AUTOSAVE_PERIOD, 2, FALSE); PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(AutoSavePeriodMilliseconds(frame) == 31000);
    frame.settings = savedSettings;
}

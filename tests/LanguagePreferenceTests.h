#pragma once
#include "../src/MainFrame.h"
#include "../src/FrameCommands.h"
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
}

inline void RunLanguagePreferenceTests(MainFrame& frame) {
    using namespace languagepreferencetest;
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Language preference assertion at line " + std::to_string(where.line()));
    };
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
        require(LoadStringW(_Module.GetModuleInstance(), IDS_CHANGE_LANG_RESTART, expected, _countof(expected)) > 0);
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

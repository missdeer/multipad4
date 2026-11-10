#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <exception>
#include <stdexcept>

namespace SearchHelpTest {
inline thread_local HWND owner = nullptr;
inline thread_local std::wstring expected;
inline thread_local std::wstring artifact;
inline thread_local bool opened = false;
inline thread_local std::exception_ptr failure;

inline void Require(bool condition) {
    if (!condition)
        throw std::runtime_error("Search help resource regression failed.");
}
inline std::wstring Resource(UINT id) {
    const wchar_t* text = nullptr;
    const int length = LoadStringW(_Module.GetModuleInstance(), id, reinterpret_cast<LPWSTR>(&text), 0);
    Require(length > 0);
    return std::wstring(text, static_cast<size_t>(length));
}
inline std::wstring Text(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), length + 1));
    return text;
}
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    // The search dialog is itself owned by the frame. GetLastActivePopup
    // tracks the root owner's popup. Find the help box by its title on this
    // UI thread, regardless of which owner Windows assigns to the message box.
    HWND dialog = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM result) -> BOOL {
        wchar_t type[32]{};
        GetClassNameW(window, type, _countof(type));
        if (IsWindowVisible(window) && wcscmp(type, L"#32770") == 0 &&
            Text(window) == expected.substr(0, expected.find(L'\n'))) {
            *reinterpret_cast<HWND*>(result) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&dialog));
    if (!dialog) return;
    fwprintf(stderr,L"Search help: closing %ls\n",Text(dialog).c_str());
    KillTimer(nullptr, timer);
    opened = true;
    try {
        // MessageBox's body is a Static control with identifier -1.
        const auto body = Text(GetDlgItem(dialog, 0xFFFF));
        Require(body == expected);
        Require(body.find(L"\n\n") != std::wstring::npos);
        Require(body.find(L'\t') != std::wstring::npos);
        CaptureQAWindow(dialog, artifact.c_str());
    } catch (...) {
        failure = std::current_exception();
    }
    PostMessageW(dialog, WM_CLOSE, 0, 0);
}
inline void Click(HWND dialog, int controlId, UINT resource, const wchar_t* title, LANGID language) {
    expected = Resource(resource);
    Require(expected.starts_with(title)); // Detect accidental English fallback in the Chinese run.
    owner = dialog;
    opened = false;
    failure = nullptr;
    artifact = L"Search-help-" + std::to_wstring(language) + L"-" + std::to_wstring(resource);
    HWND link = GetDlgItem(dialog, controlId);
    wchar_t type[32]{};
    GetClassNameW(link, type, _countof(type));
    Require(link && wcscmp(type, WC_LINK) == 0);
    NMLINK notification{};
    notification.hdr.hwndFrom = link;
    notification.hdr.idFrom = controlId;
    notification.hdr.code = NM_CLICK;
    notification.item.iLink = 0;
    const auto timer = SetTimer(nullptr, 0, 10, Timer);
    Require(timer != 0);
    SendMessageW(dialog, WM_NOTIFY, controlId, reinterpret_cast<LPARAM>(&notification));
    KillTimer(nullptr, timer);
    owner = nullptr;
    if (failure)
        std::rethrow_exception(failure);
    Require(opened);
}
} // namespace SearchHelpTest

inline void RunSearchHelpTests(MainFrame& frame) {
    using namespace SearchHelpTest;
    auto* document = frame.Active();
    Require(document != nullptr);
    SearchDialog search;
    ULONG languageCount = 0, bufferSize = 0;
    Require(GetThreadPreferredUILanguages(MUI_LANGUAGE_ID, &languageCount, nullptr, &bufferSize) != FALSE);
    std::vector<wchar_t> preferredLanguages(bufferSize);
    Require(GetThreadPreferredUILanguages(MUI_LANGUAGE_ID, &languageCount, preferredLanguages.data(), &bufferSize) != FALSE);
    const auto originalLanguage = GetThreadUILanguage();
    const auto originalSearch = document->search;
    const HWND originalFocus = GetFocus();
    struct Restore {
        SearchDialog& search;
        EditorDocument& document;
        decltype(document.search) state;
        LANGID language;
        const std::vector<wchar_t>& preferred;
        HWND focus;
        ~Restore() {
            search.Close();
            document.search = std::move(state);
            SetThreadUILanguage(language);
            SetThreadPreferredUILanguages(MUI_LANGUAGE_ID, preferred.data(), nullptr);
            if (IsWindow(focus)) SetFocus(focus);
        }
    } restore{search, *document, originalSearch, originalLanguage, preferredLanguages, originalFocus};
    // A separate SearchDialog leaves the frame's dialog and user settings alone.
    for (bool chinese : {false, true}) {
        const LANGID language = chinese ? MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)
                                       : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        const wchar_t* preferred = chinese ? L"0804\0" : L"0409\0";
        Require(SetThreadUILanguage(language) != 0);
        Require(SetThreadPreferredUILanguages(MUI_LANGUAGE_ID, preferred, nullptr) != FALSE);
        search.Open(frame, *document, false);
        Require(search.hwnd != nullptr);
        Click(search.hwnd, IDC_BACKSLASHHELP, IDS_BACKSLASHHELP,
              chinese ? L"反斜线转义\n\n" : L"Backslash Transformations\n\n", language);
        Click(search.hwnd, IDC_REGEXPHELP, IDS_REGEXPHELP,
              chinese ? L"通用正则表达式 (RegExp) 语法\n\n" : L"Common RegExp Syntax\n\n", language);
        Click(search.hwnd, IDC_WILDCARDHELP, IDS_WILDCARDHELP,
              chinese ? L"通配符搜索\n\n" : L"Wildcard Search\n\n", language);
        search.Close();
        document->search = originalSearch;
    }
}

#pragma once
#include "../src/MainFrame.h"
#include "../src/CommandLine.h"
#include <functional>
#include <source_location>
#include <stdexcept>

namespace closereentrancytest {
inline thread_local HWND owner = nullptr;
inline thread_local std::function<void(HWND)> action;
inline thread_local std::exception_ptr failure;
inline thread_local bool activated = false;
inline thread_local int answer = IDCANCEL;
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(owner);
    wchar_t type[32]{}; GetClassNameW(dialog, type, _countof(type));
    if (!dialog || dialog == owner || wcscmp(type, L"#32770")) return;
    KillTimer(nullptr, timer); activated = true;
    try { action(dialog); } catch (...) { failure = std::current_exception(); }
    PostMessageW(dialog, WM_COMMAND, answer, 0);
}
inline bool CloseWithPrompt(MainFrame& frame, int index, int response, int mode, std::function<void(HWND)> callback) {
    owner = frame; answer = response; action = std::move(callback); failure = nullptr; activated = false;
    const auto timer = SetTimer(nullptr, 0, 10, Timer);
    if (!timer) throw std::runtime_error("Close reentrancy timer failed");
    const HWND child = frame.tabs.GetPageHWND(index);
    bool closed = false;
    if (mode == 0) closed = frame.Close(index);
    else {
        if (mode == 1) frame.SendMessage(WM_CLOSE_TAB, reinterpret_cast<WPARAM>(child));
        else frame.SendMessage(WM_COMMAND, ID_CLOSE);
        closed = !IsWindow(child);
    }
    KillTimer(nullptr, timer); action = {}; owner = nullptr;
    if (failure) std::rethrow_exception(failure);
    if (!activated) throw std::runtime_error("Close confirmation did not open");
    return closed;
}
}

inline void RunCloseReentrancyTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Close reentrancy assertion at line " + std::to_string(where.line()));
    };
    const int initialCount = frame.tabs.GetPageCount();
    auto* priorActive = frame.Active();
    const auto savedSettings = frame.settings;
    const auto folder = std::filesystem::temp_directory_path() / (L"MultiPad4-close-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    frame.settings = AppSettings{}; frame.settings.persistent = false;
    auto cleanDocuments = [&] {
        while (frame.tabs.GetPageCount() > initialCount) {
            auto* d = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetPageCount() - 1));
            d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false;
            if (!frame.Close(frame.tabs.GetPageCount() - 1)) break;
        }
    };
    auto restore = [&] {
        frame.runtime.pendingTabCloses.clear(); cleanDocuments();
        frame.settings = savedSettings;
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i)
            if (frame.tabs.GetPageData(i) == priorActive) frame.tabs.SetActivePage(i);
        std::error_code error; std::filesystem::remove_all(folder, error);
    };
    try {
        require(frame.runtime.pendingCommands.empty() && frame.runtime.pendingTabCloses.empty());
        for (int mode : {0, 1, 2}) for (int response : {IDCANCEL, IDNO, IDYES}) {
            frame.NewDocument(); const HWND peer = frame.tabs.GetPageHWND(frame.tabs.GetActivePage());
            auto* target = frame.NewDocument(); require(target != nullptr);
            const HWND targetChild = frame.tabs.GetPageHWND(frame.tabs.GetActivePage());
            const auto savedFile = folder / L"saved.txt", externalFile = folder / L"external.txt";
            files::WriteAtomic(externalFile, {"external buffer"});
            target->path = savedFile;
            target->Send(SCI_ADDTEXT, 7, reinterpret_cast<LPARAM>("unsaved"));
            auto* sentinel = frame.NewDocument(); require(sentinel != nullptr);
            const HWND sentinelChild = frame.tabs.GetPageHWND(frame.tabs.GetActivePage());
            frame.tabs.SetActivePage(initialCount + 1);
            bool stableDuringPrompt = false;
            const bool closed = closereentrancytest::CloseWithPrompt(frame, initialCount + 1, response, mode, [&](HWND) {
                // Native child close posts WM_CLOSE_TAB. Dispatch it in the real
                // MessageBox modal loop before answering the outer confirmation.
                ::SendMessageW(peer, WM_CLOSE, 0, 0);
                ::SendMessageW(peer, WM_CLOSE, 0, 0); // Duplicate requests close only once.
                ::SendMessageW(targetChild, WM_CLOSE, 0, 0); // Becomes stale after outer close.
                MSG message{};
                while (PeekMessageW(&message, frame, WM_CLOSE_TAB, WM_CLOSE_TAB, PM_REMOVE)) DispatchMessageW(&message);
                stableDuringPrompt = frame.tabs.GetPageCount() == initialCount + 3 && IsWindow(peer) && IsWindow(targetChild);
                fprintf(stderr, "Close reentrancy: modal pages=%d expected=%d peerAlive=%d targetAlive=%d\n",
                    frame.tabs.GetPageCount(), initialCount + 3, IsWindow(peer), IsWindow(targetChild));
                require(frame.runtime.pendingTabCloses.size() == 2);
                // Reordering is independent of closing: the outer close must find
                // its original document again instead of removing the old index.
                require(frame.GetMDITabCtrl().MoveItem(initialCount + 1, initialCount));
                // main.cpp sends the working directory followed by the full
                // command line; ParseCommandLine skips its executable argument.
                const std::wstring commandLine = L"\"multipad4.exe\" \"" + externalFile.wstring() + L"\"";
                const auto parsed = ParseCommandLine(commandLine);
                const bool validFileArgument = parsed.files.size() == 1 && parsed.files.front() == externalFile;
                if (!validFileArgument)
                    fprintf(stderr, "Close reentrancy: mode=%d response=%d IPC parsedFiles=%zu expected=1\n",
                        mode, response, parsed.files.size());
                require(validFileArgument);
                std::wstring payload = folder.wstring() + L"\n" + commandLine;
                COPYDATASTRUCT data{0x4D503434, static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t)), payload.data()};
                require(frame.SendMessage(WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data)) == TRUE);
                require(frame.runtime.pendingCommands.size() == 1);
                require(frame.runtime.pendingCommands.front().first == data.dwData && frame.runtime.pendingCommands.front().second == payload);
            });
            require(closed == (response != IDCANCEL)); require(stableDuringPrompt);
            require(IsWindow(peer) && IsWindow(sentinelChild));
            if (response == IDCANCEL) {
                require(frame.runtime.pendingTabCloses.empty());
                require(target->Text() == "unsaved" && target->Modified());
            } else {
                require(!IsWindow(targetChild));
                if (response == IDYES) require(files::Read(savedFile).utf8 == "unsaved");
            }
            // One request per dispatch; a destroyed target must be ignored, and
            // external opens must survive a canceled close and drain afterward.
            for (int dispatch = 0; dispatch < 8 && (!frame.runtime.pendingTabCloses.empty() || !frame.runtime.pendingCommands.empty()); ++dispatch)
                frame.SendMessage(WM_APP + 0x104);
            require(frame.runtime.pendingTabCloses.empty() && frame.runtime.pendingCommands.empty());
            require((IsWindow(peer) != FALSE) == (response == IDCANCEL));
            require(IsWindow(sentinelChild) && sentinel->Text().empty());
            bool opened = false;
            for (const auto& document : frame.documents)
                if (document->path == externalFile && document->Text() == "external buffer") opened = true;
            if (!opened) {
                fprintf(stderr, "Close reentrancy: mode=%d response=%d external open missing; pages=%d pendingCommands=%zu pendingTabCloses=%zu\n",
                    mode, response, frame.tabs.GetPageCount(), frame.runtime.pendingCommands.size(), frame.runtime.pendingTabCloses.size());
                for (const auto& document : frame.documents)
                    fprintf(stderr, "Close reentrancy: document pathMatches=%d textMatches=%d\n",
                        document->path == externalFile, document->Text() == "external buffer");
            }
            require(opened);
            fprintf(stderr, "Close reentrancy: mode=%d response=%d passed\n", mode, response);
            cleanDocuments();
        }
        restore();
    } catch (...) { restore(); throw; }
}

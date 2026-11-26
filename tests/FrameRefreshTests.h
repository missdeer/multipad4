#pragma once
#include "../src/MainFrame.h"
#include "../src/UpstreamUI.h"
#include "../src/FrameCommands.h"
#include "../plugins/PluginApi.h"
#include <chrono>
#include <source_location>
#include <psapi.h>
inline std::vector<std::pair<UINT, UINT_PTR>> frameRefreshNotifications;
inline void __cdecl FrameRefreshNotification(SCNotification* notification) {
    if (notification->nmhdr.code == MPN_DOCUMENTSCHANGED || notification->nmhdr.code == NPPN_BUFFERACTIVATED)
        frameRefreshNotifications.emplace_back(notification->nmhdr.code, notification->nmhdr.idFrom);
}
inline void RunFrameRefreshTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("Frame refresh regression line " + std::to_string(where.line())); };
    PluginHost::Plugin observer; observer.notify = FrameRefreshNotification; frame.plugins.plugins.push_back(std::move(observer));
    struct RemoveObserver { MainFrame& frame; ~RemoveObserver() { frame.plugins.plugins.pop_back(); } } remove{frame};
    auto* first = frame.NewDocument(); auto* second = frame.NewDocument(); require(first && second); frame.Refresh();
    auto verifyOrder = [&] {
        require(frame.publishedDocuments.size() == static_cast<size_t>(frame.tabs.GetPageCount()));
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) require(frame.publishedDocuments[i].first == static_cast<EditorDocument*>(frame.tabs.GetPageData(i))->searchIdentity);
    };
    verifyOrder(); frameRefreshNotifications.clear();
    first->Send(SCI_ADDTEXT, 4, reinterpret_cast<LPARAM>("test")); frame.Refresh();
    require(frame.tabs.GetPageTitle(0).starts_with(L"* "));
    require(std::any_of(frameRefreshNotifications.begin(), frameRefreshNotifications.end(), [](const auto& event) { return event.first == MPN_DOCUMENTSCHANGED && event.second == 0; }));
    frameRefreshNotifications.clear(); frame.Refresh(); require(frameRefreshNotifications.empty());
    require(frame.GetMDITabCtrl().MoveItem(0, 1)); frame.Refresh(); verifyOrder();
    require(frame.publishedDocuments[1].first == first->searchIdentity);
    first->untitled = L"renamed inactive"; first->Send(SCI_SETSAVEPOINT); frame.Refresh(); require(frame.tabs.GetPageTitle(1) == L"renamed inactive");
    frameRefreshNotifications.clear(); frame.tabs.SetActivePage(1); frame.Refresh();
    auto activated = std::find_if(frameRefreshNotifications.begin(), frameRefreshNotifications.end(), [](const auto& event) { return event.first == NPPN_BUFFERACTIVATED; });
    auto documents = std::find_if(frameRefreshNotifications.begin(), frameRefreshNotifications.end(), [](const auto& event) { return event.first == MPN_DOCUMENTSCHANGED; });
    require(activated != frameRefreshNotifications.end() && documents != frameRefreshNotifications.end() && activated < documents);
    HMENU menu = CreatePopupMenu(); require(menu != nullptr);
    const auto nativeRoot = frame.runtime.mainMenu ? frame.runtime.mainMenu : frame.GetMenu();
    require(AppendMenuW(menu, MF_STRING, IDM_EDIT_UNDO, L"Dynamic undo"));
    require(AppendMenuW(nativeRoot, MF_POPUP, reinterpret_cast<UINT_PTR>(menu), L"Dynamic test"));
    first->Send(SCI_EMPTYUNDOBUFFER); frame.Refresh(); require(GetMenuState(menu, IDM_EDIT_UNDO, MF_BYCOMMAND) & MF_GRAYED);
    EnableMenuItem(menu, IDM_EDIT_UNDO, MF_DISABLED); frame.Refresh(); require(GetMenuState(menu, IDM_EDIT_UNDO, MF_BYCOMMAND) & MF_GRAYED);
    first->Send(SCI_ADDTEXT, 1, reinterpret_cast<LPARAM>("a")); frame.Refresh(); require(!(GetMenuState(menu, IDM_EDIT_UNDO, MF_BYCOMMAND) & MF_GRAYED));
    EnableMenuItem(menu, IDM_EDIT_UNDO, MF_GRAYED); frame.Refresh(); require(!(GetMenuState(menu, IDM_EDIT_UNDO, MF_BYCOMMAND) & MF_GRAYED));
    require(RemoveMenu(nativeRoot, GetMenuItemCount(nativeRoot) - 1, MF_BYPOSITION)); DestroyMenu(menu);
    frame.settings.Set(IDM_VIEW_SHOWEXCERPT, 1); first->Load({"first line\nsecond"}); frame.Refresh();
    const auto excerptRevision = first->excerptRevision; const auto caption = frame.publishedCaption;
    first->Send(SCI_SETSEL, 1, 1); frame.SendMessage(WM_DOCUMENT_CHANGED, 1); require(first->excerptRevision == excerptRevision && frame.publishedCaption == caption);
    first->Send(SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>("new ")); frame.SendMessage(WM_DOCUMENT_CHANGED, 1); require(first->excerptRevision != excerptRevision && frame.publishedCaption != caption);
    const auto directory = std::filesystem::temp_directory_path() / (L"multipad-refresh-" + std::to_wstring(GetCurrentProcessId())); std::filesystem::create_directories(directory);
    first->path = directory / L"attributes.txt"; files::WriteAtomic(first->path, {"content"}); SnapshotDocument(*first); frame.Refresh();
    require(frame.State(IDM_FILE_READONLY_FILE).enabled && !frame.State(IDM_FILE_READONLY_FILE).checked);
    require(SetFileAttributesW(first->path.c_str(), FILE_ATTRIBUTE_READONLY)); SnapshotDocument(*first); frame.Refresh(); require(frame.State(IDM_FILE_READONLY_FILE).checked);
    frame.SendMessage(WM_COMMAND, IDM_FILE_READONLY_FILE); require(!(GetFileAttributesW(first->path.c_str()) & FILE_ATTRIBUTE_READONLY));
    require(SetFileAttributesW(first->path.c_str(), FILE_ATTRIBUTE_READONLY)); // Command refreshes an older cached normal state.
    require(!frame.State(IDM_FILE_READONLY_FILE).checked); // State reads the cached normal attributes until an observable refresh.
    frame.SendMessage(WM_COMMAND, IDM_FILE_READONLY_FILE); require(!(GetFileAttributesW(first->path.c_str()) & FILE_ATTRIBUTE_READONLY));
    std::string mappedText = "one\n"; for (int i = 0; i < 20000; ++i) mappedText += "later line\n";
    files::WriteAtomic(first->path, {mappedText}); first->OpenFastFile(first->path, {}, files::Encoding::Utf8);
    auto* child = frame.tabs.Child(frame.tabs.GetActivePage()); child->m_hWndClient = first->ViewWindow();
    SetParent(first->ViewWindow(), child->m_hWnd); ShowWindow(first->ViewWindow(), SW_SHOW); child->UpdateLayout(); frame.Refresh();
    const auto mappedCaption = frame.publishedCaption;
    first->fastView->Seek(first->fastView->Size(), true); frame.SendMessage(WM_DOCUMENT_CHANGED, 1);
    require(frame.publishedCaption != mappedCaption);
    frame.settings.Set(IDM_VIEW_SHOWEXCERPT, 0); first->Send(SCI_SETSAVEPOINT); second->Send(SCI_SETSAVEPOINT);
    while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1, false)); std::filesystem::remove_all(directory);
}
inline void BaselineFrameRefresh(MainFrame& frame) {
    auto Active = [&] { return frame.Active(); };
    auto& plugins = frame.plugins; auto& searchDialog = frame.searchDialog; auto& tabs = frame.tabs;
    auto& publishedActive = frame.publishedActive; auto& publishedDocuments = frame.publishedDocuments;
    auto& runtime = frame.runtime; auto& settings = frame.settings; auto m_hWnd = frame.m_hWnd;
    auto GetMenu = [&] { return frame.GetMenu(); };
    auto UpdateStatus = [&] { frame.UpdateStatus(); }; auto UpdateCommandUI = [&] { frame.UpdateCommandUI(); };
    auto* active = Active();
    plugins.Activated();
    searchDialog.SyncDocument();
    std::vector<std::pair<UINT_PTR, std::wstring>> currentDocuments;
    for (int i = 0; i < tabs.GetPageCount(); ++i) {
        auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
        auto title = d->Title();
        currentDocuments.emplace_back(d->searchIdentity, title);
        if (title != tabs.GetPageTitle(i)) tabs.SetPageTitle(i, title.c_str());
    }
    const auto currentActive = active ? active->searchIdentity : 0;
    const bool activeChanged = currentActive != publishedActive;
    publishedActive = currentActive;
    if (currentDocuments != publishedDocuments) {
        publishedDocuments = std::move(currentDocuments);
        if (plugins.ready) plugins.Notify(MPN_DOCUMENTSCHANGED);
    } else if (activeChanged && plugins.ready) {
        plugins.Notify(MPN_DOCUMENTSCHANGED, currentActive);
    }
    std::wstring title = L"MultiPad4";
    if (!runtime.titleOverride.empty()) title = runtime.titleOverride + L" — MultiPad4";
    else if (active) {
        std::wstring name = active->Title();
        int format = settings.Get(IDM_VIEW_SHOWFILENAMEONLY);
        if (!active->path.empty() && format == 1) name += L" (" + active->path.parent_path().wstring() + L")";
        else if (!active->path.empty() && format == 2) name = (active->Modified() ? L"* " : L"") + active->path.wstring();
        if (settings.Get(IDM_VIEW_SHOWEXCERPT)) {
            auto length = min<LRESULT>(124, active->Send(SCI_GETLINEENDPOSITION, 0));
            std::string text(static_cast<size_t>(length) + 1, '\0');
            active->Send(SCI_GETTEXT, length, reinterpret_cast<LPARAM>(text.data())); text.resize(length);
            auto boundary = min<size_t>(120, text.size());
            while (boundary && boundary < text.size() && (static_cast<unsigned char>(text[boundary]) & 0xC0) == 0x80) --boundary;
            try { name += L" — " + files::FromUtf8(text.substr(0, boundary)); } catch (...) {}
        }
        title = name + L" — MultiPad4";
    }
    SetWindowTextW(m_hWnd, title.c_str());
    UpdateStatus(); UpdateMenuCommands(frame, runtime.mainMenu ? runtime.mainMenu : GetMenu()); UpdateCommandUI();
}

inline size_t frameRefreshCaptionWrites = 0;
inline LRESULT CALLBACK FrameRefreshCaptionCounter(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (message == WM_SETTEXT) ++frameRefreshCaptionWrites;
    return DefSubclassProc(window, message, w, l);
}
inline void RunFrameRefreshBenchmark(MainFrame& frame, bool baseline) {
    using Clock = std::chrono::steady_clock;
    for (int i = 0; i < 500; ++i) if (!frame.NewDocument()) throw std::runtime_error("Cannot create benchmark tabs.");
    auto* document = frame.Active(); std::string corpus = "first line\n";
    for (int line = 0; line < 200; ++line) corpus += std::string(100, 'x') + "\n";
    document->Load({corpus});
    frame.ShowWindow(SW_SHOWNORMAL); frame.Refresh(); frame.UpdateWindow();
    if (!SetWindowSubclass(frame.m_hWnd, FrameRefreshCaptionCounter, 0x45, 0)) throw std::runtime_error("Cannot instrument benchmark caption.");
    struct RemoveCounter { HWND window; ~RemoveCounter() { RemoveWindowSubclass(window, FrameRefreshCaptionCounter, 0x45); } } remove{frame.m_hWnd};
    std::map<std::pair<bool, bool>, std::string> pairedStates;
    for (bool measuredBaseline : {baseline, !baseline}) for (bool excerpt : {false, true}) for (bool typing : {false, true}) {
        document->Load({corpus}); document->Send(SCI_SETFIRSTVISIBLELINE, 0);
        // The copied baseline bypasses publication caches; reset those outside measurement.
        document->publishedTitle.clear(); frame.publishedCaption.clear();
        frame.settings.Set(IDM_VIEW_SHOWEXCERPT, excerpt); frame.Refresh(); frame.UpdateWindow();
        std::vector<double> callbacks, paints;
        frameRefreshCaptionWrites = 0;
        FILETIME created{}, exited{}, kernelStart{}, userStart{}, kernelEnd{}, userEnd{};
        GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelStart, &userStart);
        for (int i = 0; i < 100; ++i) {
            const auto inputBegin = Clock::now();
            if (typing) SendMessageW(document->hwnd, WM_CHAR, 'a', 1);
            else SendMessageW(document->hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
            const auto begin = Clock::now();
            if (measuredBaseline) {
                MSG message{}; bool lightweight = true;
                while (PeekMessageW(&message, frame.m_hWnd, WM_DOCUMENT_CHANGED, WM_DOCUMENT_CHANGED, PM_REMOVE)) lightweight &= message.wParam != 0;
                if (!lightweight || excerpt) BaselineFrameRefresh(frame); else { frame.UpdateStatus(); frame.UpdateCommandUI(); }
                if (!frame.runtime.workTimerArmed && std::any_of(frame.documents.begin(), frame.documents.end(), [](const auto& d) { return d->style.occurrencePending; })) {
                    frame.SetTimer(3, 30); frame.runtime.workTimerArmed = true;
                }
            } else frame.SendMessage(WM_DOCUMENT_CHANGED, 1);
            callbacks.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());
            RedrawWindow(frame.m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
            RedrawWindow(document->ViewWindow(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            UpdateWindow(frame.GetMDITabCtrl()); UpdateWindow(frame.m_hWndStatusBar);
            paints.push_back(std::chrono::duration<double,std::milli>(Clock::now()-inputBegin).count());
        }
        GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelEnd, &userEnd);
        auto ticks = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        auto cpu = (ticks(kernelEnd) + ticks(userEnd) - ticks(kernelStart) - ticks(userStart)) / 10000.0;
        std::sort(callbacks.begin(), callbacks.end()); std::sort(paints.begin(), paints.end());
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
        std::string observed;
        auto number = [&](unsigned long long value) { observed += std::to_string(value) + ";"; };
        auto text = [&](const std::wstring& value) { const auto utf8 = files::ToUtf8(value); number(utf8.size()); observed += utf8; };
        number(document->Send(SCI_GETCURRENTPOS)); number(document->Send(SCI_GETANCHOR));
        number(document->Send(SCI_GETFIRSTVISIBLELINE)); number(document->Send(SCI_GETMODIFY));
        for (const auto& item : frame.documents) { const auto bytes = item->Text(); number(bytes.size()); observed += bytes; }
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) text(frame.tabs.GetPageTitle(i));
        for (const auto& item : frame.publishedDocuments) { number(item.first); text(item.second); }
        wchar_t caption[512]{}; GetWindowTextW(frame.m_hWnd, caption, 512); text(caption);
        for (const auto& item : frame.statusText) text(item);
        for (const auto& [id, state] : frame.toolbarState) { number(id); number(state.first); number(state.second); }
        auto menuState = [&](auto&& self, HMENU menu) -> void {
            for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                number(GetMenuItemID(menu, i)); number(GetMenuState(menu, i, MF_BYPOSITION));
                if (const auto child = GetSubMenu(menu, i)) self(self, child);
            }
        };
        menuState(menuState, frame.runtime.mainMenu ? frame.runtime.mainMenu : frame.GetMenu());
        const auto [previous, inserted] = pairedStates.emplace(std::pair{excerpt, typing}, observed);
        if (!inserted && previous->second != observed) throw std::runtime_error("Frame benchmark paired state mismatch.");
        if (typing && document->Text() != std::string(100, 'a') + corpus) throw std::runtime_error("WM_CHAR benchmark did not insert expected text.");
        if (!typing && document->Send(SCI_GETFIRSTVISIBLELINE) != 100) throw std::runtime_error("WM_VSCROLL benchmark did not scroll expected lines.");
        unsigned long long stateHash = 1469598103934665603ULL;
        for (unsigned char c : observed) stateHash = (stateHash ^ c) * 1099511628211ULL;
        printf("%s,excerpt=%d,%s,tabs=500,callback_median_ms=%.3f,callback_p95_ms=%.3f,callback_p99_ms=%.3f,callback_max_ms=%.3f,paint_median_ms=%.3f,paint_p95_ms=%.3f,paint_p99_ms=%.3f,ui_cpu_ms=%.3f,peak_private=%zu,caption_writes=%zu,state_hash=%llu\n",measuredBaseline ? "baseline" : "optimized",excerpt,typing ? "typing" : "scrolling",callbacks[49],callbacks[94],callbacks[98],callbacks[99],paints[49],paints[94],paints[98],cpu,memory.PeakPagefileUsage,frameRefreshCaptionWrites,stateHash); fflush(stdout);
    }
    document->Send(SCI_SETSAVEPOINT);
    while (frame.tabs.GetPageCount()) frame.Close(frame.tabs.GetPageCount()-1, false);
}

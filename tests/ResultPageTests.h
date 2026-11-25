#pragma once
#include "MainFrame.h"
#include <source_location>
#include <chrono>
struct ResultCalls {
    SearchResultsPane* pane;
    size_t appends = 0, readOnly = 0, styling = 0, folds = 0, notifications = 0;
    bool consistent = true;
};
inline LRESULT CALLBACK CountResultCalls(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto& calls = *reinterpret_cast<ResultCalls*>(data);
    if (message == SCI_SETREADONLY) ++calls.readOnly;
    if (message == SCI_SETSTYLING) ++calls.styling;
    if (message == SCI_SETFOLDLEVEL) ++calls.folds;
    if (message == SCI_APPENDTEXT) {
        ++calls.appends;
        const auto value = DefSubclassProc(window, message, w, l);
        if (window != calls.pane->editor) return value;
        calls.consistent &= calls.pane->lines.size() == calls.pane->targets.size() &&
            static_cast<size_t>(SendMessageW(window, SCI_GETLINECOUNT, 0, 0)) == calls.pane->lines.size() + 1;
        SCNotification request{}; request.nmhdr.hwndFrom = window; request.nmhdr.code = SCN_STYLENEEDED;
        request.position = static_cast<Sci_Position>(SendMessageW(window, SCI_GETLENGTH, 0, 0));
        SendMessageW(calls.pane->m_hWnd, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&request));
        return value;
    }
    return DefSubclassProc(window, message, w, l);
}
inline LRESULT CALLBACK CountResultNotifications(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    if (message == WM_NOTIFY) {
        const auto& n = *reinterpret_cast<SCNotification*>(l);
        auto& calls = *reinterpret_cast<ResultCalls*>(data);
        if (n.nmhdr.hwndFrom == calls.pane->editor && n.nmhdr.code == SCN_MODIFIED && (n.modificationType & SC_MOD_CHANGESTYLE)) {
            ++calls.notifications;
            calls.consistent &= calls.pane->lines.size() == calls.pane->targets.size() &&
                static_cast<size_t>(SendMessageW(calls.pane->editor, SCI_GETLINECOUNT, 0, 0)) == calls.pane->lines.size() + 1;
        }
    }
    return DefSubclassProc(window, message, w, l);
}
inline void RunResultPageTests(MainFrame& frame) {
    auto require = [](bool condition, std::source_location where = std::source_location::current()) { if (!condition) throw std::runtime_error("Result page regression at " + std::to_string(where.line())); };
    auto* document = frame.NewDocument(); require(document != nullptr);
    std::string text; for (int i = 0; i < 100; ++i) text += "hit hit\n";
    document->Load({text}); document->search = {}; document->search.findText = L"hit";
    auto matches = document->CollectMatches(); require(matches.size() == 200);
    auto& pane = frame.searchResults; pane.Clear(); pane.Begin(L"hit");
    SendMessageW(pane.editor, SCI_SETMODEVENTMASK, SC_MODEVENTMASKALL, 0);
    ResultCalls calls{&pane};
    SetWindowSubclass(pane.editor, CountResultCalls, 73, reinterpret_cast<DWORD_PTR>(&calls));
    SetWindowSubclass(pane.m_hWnd, CountResultNotifications, 74, reinterpret_cast<DWORD_PTR>(&calls));
    struct Restore { HWND editor, pane; ~Restore() { RemoveWindowSubclass(editor, CountResultCalls, 73); RemoveWindowSubclass(pane, CountResultNotifications, 74); } } restore{pane.editor, pane.m_hWnd};
    const auto start = std::chrono::steady_clock::now(); pane.Add(*document, matches);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    require(calls.consistent && calls.appends == 1 && calls.readOnly == 2);
    if (calls.folds != 101 || calls.styling != 401 || !calls.notifications)
        fprintf(stderr, "Result calls folds=%zu styles=%zu notifications=%zu\n", calls.folds, calls.styling, calls.notifications);
    require(calls.folds == 101 && calls.styling == 401 && calls.notifications > 0);
    require(pane.lines.size() == 102 && pane.lines[2].segments.size() == 2);
    require(SendMessageW(pane.editor, SCI_GETFOLDLEVEL, 101, 0) == SC_FOLDLEVELBASE + 2);
    require(SendMessageW(pane.editor, SCI_GETENDSTYLED, 0, 0) == SendMessageW(pane.editor, SCI_GETLENGTH, 0, 0));
    const auto before = calls.styling; pane.Finish(200, 1); require(calls.styling == before + 1);
    require(pane.Navigate(2, 1) && document->Send(SCI_GETSELECTIONSTART) == 4);
    pane.Begin(L"next history"); require(pane.Navigate(2, 0) && document->Send(SCI_GETSELECTIONSTART) == 0);
    auto result = std::make_shared<OrdinarySearchResult>(); result->hits = std::make_shared<large::HitArchive>();
    for (int i = 0; i < 100; ++i) {
        auto line = std::make_shared<const std::string>("hit hit");
        result->lines.push_back({i, i * 8, line});
        result->hits->Append({static_cast<large::Position>(i * 8), static_cast<large::Position>(i * 8 + 3), {}, static_cast<large::Position>(i), static_cast<large::Position>(i * 8)});
        result->hits->Append({static_cast<large::Position>(i * 8 + 4), static_cast<large::Position>(i * 8 + 7), {}, static_cast<large::Position>(i), static_cast<large::Position>(i * 8)});
    }
    calls.appends = calls.readOnly = calls.styling = calls.folds = 0;
    pane.AddOrdinary(*document, result);
    require(calls.consistent && calls.appends == 1 && calls.readOnly == 2 && calls.folds == 34 && calls.styling == 130);
    const auto marker = pane.targets.size() - 1; calls.appends = 0;
    require(pane.Navigate(marker) && calls.appends == 1 && calls.consistent);
    require(SendMessageW(pane.editor, SCI_GETENDSTYLED, 0, 0) == SendMessageW(pane.editor, SCI_GETLENGTH, 0, 0));
    if (GetEnvironmentVariableW(L"MULTIPAD4_RESULTS_BENCHMARK", nullptr, 0)) {
        // Reproduce the previous loaded-page append/style path in an equivalent Scintilla.
        const auto scratch = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD, 0, 0, 800, 200, frame.m_hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(scratch != nullptr); SearchResultLexer::Configure(scratch);
        struct Scratch { HWND window; ~Scratch() { DestroyWindow(window); } } scratchGuard{scratch};
        auto cpu = [] {
            FILETIME created{}, exited{}, kernel{}, user{}; GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
            ULARGE_INTEGER a{}, b{}; a.LowPart = kernel.dwLowDateTime; a.HighPart = kernel.dwHighDateTime; b.LowPart = user.dwLowDateTime; b.HighPart = user.dwHighDateTime;
            return (a.QuadPart + b.QuadPart) / 10000.0;
        };
        double oldCpu = 0, newCpu = 0;
        std::vector<double> oldTimes, newTimes;
        for (int sample = 0; sample < 21; ++sample) {
            pane.Clear(); pane.Begin(L"page benchmark");
            calls.appends = calls.styling = calls.folds = calls.readOnly = 0;
            auto cpuStart = cpu(); auto begin = std::chrono::steady_clock::now(); pane.AddOrdinary(*document, result);
            newCpu += cpu() - cpuStart;
            newTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
            require(calls.appends == 1 && calls.styling == 130 && calls.folds == 34);
            std::vector<std::string> rowText;
            for (size_t row = 1; row < pane.lines.size(); ++row) {
                const auto length = SendMessageW(pane.editor, SCI_LINELENGTH, row, 0);
                std::string bytes(static_cast<size_t>(length) + 1, '\0'); SendMessageW(pane.editor, SCI_GETLINE, row, reinterpret_cast<LPARAM>(bytes.data())); bytes.resize(static_cast<size_t>(length)); rowText.push_back(std::move(bytes));
            }
            SendMessageW(scratch, SCI_SETREADONLY, FALSE, 0); SendMessageW(scratch, SCI_CLEARALL, 0, 0); SendMessageW(scratch, SCI_SETREADONLY, TRUE, 0);
            std::vector<SearchResultLexer::Line> styles;
            auto append = [&](std::string_view bytes, const SearchResultLexer::Line& style) {
                SendMessageW(scratch, SCI_SETREADONLY, FALSE, 0); SendMessageW(scratch, SCI_APPENDTEXT, bytes.size(), reinterpret_cast<LPARAM>(bytes.data())); SendMessageW(scratch, SCI_SETREADONLY, TRUE, 0);
                styles.push_back(style); SearchResultLexer::Colourise(scratch, styles, styles.size() - 1);
            };
            append("Search page benchmark\n", {SearchResultLexer::SearchHeader});
            ResultCalls previousCalls{&pane}; SetWindowSubclass(scratch, CountResultCalls, 75, reinterpret_cast<DWORD_PTR>(&previousCalls));
            cpuStart = cpu(); begin = std::chrono::steady_clock::now();
            for (size_t row = 0; row < rowText.size(); ++row) append(rowText[row], pane.lines[row + 1]);
            SearchResultLexer::Colourise(scratch, styles, 1);
            oldCpu += cpu() - cpuStart;
            RemoveWindowSubclass(scratch, CountResultCalls, 75);
            require(previousCalls.appends == 34 && previousCalls.readOnly == 68 && previousCalls.styling == 260 && previousCalls.folds == 68);
            oldTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        }
        std::sort(oldTimes.begin(), oldTimes.end()); std::sort(newTimes.begin(), newTimes.end());
        fprintf(stdout, "Results64hitpage old/new ms median %.3f/%.3f p95 %.3f/%.3f max %.3f/%.3f; appends34/1 readonly68/2 styles260/130 folds68/34.\n",
            oldTimes[10], newTimes[10], oldTimes[19], newTimes[19], oldTimes[20], newTimes[20]);
        fprintf(stdout, "Results page UIthreadCPU total21samples old/new %.3f/%.3fms;100row200hit add %.3fms.\n", oldCpu, newCpu, elapsed);
        document->Load({std::string(4096, 'x')}); document->search.findText = L"x";
        pane.Clear(); pane.Begin(L"dense"); pane.Add(*document, document->CollectMatches());
        require(pane.targets[2].matches.size() == 4096 && pane.Navigate(2, 4095) && document->Send(SCI_GETSELECTIONSTART) == 4095);
        std::vector<double> fullCopies, selectedCopies;
        LRESULT sink = 0;
        for (int i = 0; i < 21; ++i) {
            auto begin = std::chrono::steady_clock::now();
            { auto target = pane.targets[2]; target.match = target.matches[4095]; sink += target.match.start; }
            fullCopies.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
            begin = std::chrono::steady_clock::now();
            { SearchResultsPane::Target target; target.identity = pane.targets[2].identity; target.path = pane.targets[2].path; target.match = pane.targets[2].matches[4095]; sink += target.match.start; }
            selectedCopies.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
        }
        require(sink == 2 * 21 * 4095);
        std::sort(fullCopies.begin(), fullCopies.end()); std::sort(selectedCopies.begin(), selectedCopies.end());
        fprintf(stdout, "Results4096hitrow navigation targetcopy old/new median %.3f/%.3fus max %.3f/%.3fus; copied SearchMatch objects4097/1.\n", fullCopies[10], selectedCopies[10], fullCopies[20], selectedCopies[20]);
    }
    pane.Clear(); document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
    require(frame.Close(frame.tabs.GetActivePage()));
}

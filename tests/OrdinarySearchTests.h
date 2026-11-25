#pragma once
#include "SearchDialogFeatureTests.h"
#include "ResultPageTests.h"
#include <chrono>
#include <fstream>
#include <psapi.h>
#include <thread>
#include "../src/ScintillaBatch.h"
struct IndicatorNotifications { int count = 0; LRESULT start = 0, length = 0; };
inline LRESULT CALLBACK CountIndicatorNotifications(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    if (message == WM_NOTIFY) {
        const auto& n = *reinterpret_cast<SCNotification*>(l);
        if (n.nmhdr.code == SCN_MODIFIED && (n.modificationType & SC_MOD_CHANGEINDICATOR)) {
            auto& calls = *reinterpret_cast<IndicatorNotifications*>(data);
            ++calls.count; calls.start = n.position; calls.length = n.length;
        }
    }
    return DefSubclassProc(window, message, w, l);
}
inline void RunOrdinarySearchTests(MainFrame& frame) {
    RunResultPageTests(frame);
    auto require = [](bool value, std::source_location where = std::source_location::current()) {
        if (!value) throw std::runtime_error("Ordinary search regression at " + std::to_string(where.line()));
    };
    auto* d = frame.NewDocument(); d->Load({"中文😀 aa aa\nsecond aa\nuntouched\n"});
    IndicatorNotifications notifications;
    const auto parent = GetParent(d->hwnd);
    SetWindowSubclass(parent, CountIndicatorNotifications, 1, reinterpret_cast<DWORD_PTR>(&notifications));
    struct SubclassGuard { HWND parent; ~SubclassGuard() { RemoveWindowSubclass(parent, CountIndicatorNotifications, 1); } } guard{parent};
    d->Send(SCI_SETINDICATORCURRENT, 12);
    d->Send(BeginIndicatorBatch); d->Send(BeginIndicatorBatch);
    d->Send(SCI_INDICATORFILLRANGE, 2, 3); d->Send(SCI_INDICATORFILLRANGE, 10, 2);
    require(notifications.count == 0 && d->Send(SCI_INDICATORVALUEAT, 12, 2) != 0);
    d->Send(EndIndicatorBatch); require(notifications.count == 0);
    d->Send(EndIndicatorBatch); require(notifications.count == 1 && notifications.start == 2 && notifications.length == 10);
    d->Send(SCI_INDICATORFILLRANGE, 20, 1); require(notifications.count == 2);
    RemoveWindowSubclass(parent, CountIndicatorNotifications, 1);
    d->search = {}; d->search.findText = L"aa";
    // Both regex engines, empty matches, multibyte advancement and target restoration.
    for (bool boost : {false, true}) for (const auto* pattern : {L"aa", L"^", L"a*", L"中文", L"missing"}) {
        d->search.findText = pattern; d->search.regex = true; d->search.useBoostRegex = boost;
        const auto expected = d->CollectMatches(); auto cursor = d->BeginMatches();
        std::vector<std::pair<LRESULT, LRESULT>> actual;
        d->Send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE); d->Send(SCI_SETTARGETRANGE, 2, 7);
        while (!d->MatchStep(cursor, [&](auto start, auto end) { actual.emplace_back(start, end); })) {}
        require(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) require(actual[i] == std::pair(expected[i].start, expected[i].end));
        require(d->Send(SCI_GETSEARCHFLAGS) == SCFIND_MATCHCASE && d->Send(SCI_GETTARGETSTART) == 2 && d->Send(SCI_GETTARGETEND) == 7);
    }
    d->search = {}; d->search.findText = L"aa"; frame.ShowSearch(false);
    auto& dialog = frame.searchDialog; auto& results = frame.searchResults;
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"aa");
    results.Clear(); dialog.Execute(IDC_FINDALL); WaitForOrdinarySearch(dialog);
    require(results.lines.size() == 4 && results.lines[2].segments.size() == 2);
    require(results.targets[2].match.source->size() < d->Text().size());
    d->Send(SCI_APPENDTEXT, 4, reinterpret_cast<LPARAM>("tail"));
    require(results.Navigate(2, 1) && d->Range(d->Send(SCI_GETSELECTIONSTART), 2) == "aa");
    // Selection-only searches retain exact byte boundaries without a document snapshot.
    d->Send(SCI_SETSEL, 11, 13); CheckDlgButton(dialog.hwnd, IDC_SEARCH_SELECTION, BST_CHECKED);
    dialog.Execute(IDC_FINDALL); WaitForOrdinarySearch(dialog);
    require(results.targets.back().matches.size() == 1);
    CheckDlgButton(dialog.hwnd, IDC_SEARCH_SELECTION, BST_UNCHECKED);
    d->Load({"a\r\nb"}); d->search.regex = true; d->search.findText = L"a*";
    d->Send(SCI_SETSEL, 0, 2); auto boundary = d->BeginMatches(true); size_t boundaryHits = 0;
    while (!d->MatchStep(boundary, [&](auto start, auto end) { require(start <= 2 && end <= 2); ++boundaryHits; })) {}
    require(boundaryHits > 0 && boundary.done);
    d->search.findText = L"\\r\\n"; d->search.useBoostRegex = true; boundary = d->BeginMatches(true); boundaryHits = 0;
    for (int attempt = 0; !boundary.done && attempt < 8; ++attempt) d->MatchStep(boundary, [&](auto, auto) { ++boundaryHits; });
    require(boundary.done && boundaryHits == 1);
    // A large unfinished mark must not purge existing marks on edit or cancellation.
    d->Load({std::string(20000, 'a')}); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"a");
    d->Send(SCI_SETINDICATORCURRENT, 12); d->Send(SCI_INDICATORFILLRANGE, 15000, 1);
    dialog.Execute(IDC_SEARCH_MARK); require(dialog.SearchBusy());
    d->Send(SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>("x")); WaitForOrdinarySearch(dialog);
    require(d->Send(SCI_INDICATORALLONFOR, 15001) & (1 << 12));
    dialog.Execute(IDC_SEARCH_MARK); require(dialog.SearchBusy()); dialog.SetPage(0);
    require(!dialog.SearchBusy() && (d->Send(SCI_INDICATORALLONFOR, 15001) & (1 << 12)));
    // Demand paging preserves history and can navigate a later page after unrelated edits.
    results.Clear(); dialog.Execute(IDC_FINDALL); WaitForOrdinarySearch(dialog);
    require(results.targets.size() == 4 && results.targets.back().archive != static_cast<size_t>(-1));
    const auto loadLine = results.targets.size() - 1; require(results.Navigate(loadLine));
    require(results.targets.size() == 7 && results.targets[5].matches.size() == 64);
    require(results.Navigate(5, 63) && d->Send(SCI_GETSELECTIONSTART) == 128);
    dialog.Execute(IDC_SEARCH_COUNT); require(dialog.SearchBusy()); results.Clear(); WaitForOrdinarySearch(dialog);
    wchar_t status[128]{}; GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, status, 128);
    require(std::wstring_view(status).starts_with(L"Completed."));
    dialog.Execute(IDC_SEARCH_COUNT); require(dialog.SearchBusy()); DestroyWindow(dialog.hwnd); require(!dialog.SearchBusy());
    frame.ShowSearch(false); require(dialog.hwnd != nullptr);
    std::string dense(2000000, ' '); for (size_t i = 0; i < dense.size(); i += 2) dense[i] = 'a';
    d->Load({dense}); d->Send(SCI_SETSEL, 0, 0); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"a");
    const auto started = std::chrono::steady_clock::now(); double longest = 0;
    dialog.Execute(IDC_SEARCH_MARK);
    frame.ShowWindow(SW_SHOWNORMAL);
    InvalidateRect(d->hwnd, nullptr, FALSE);
    PostMessageW(d->hwnd, WM_KEYDOWN, VK_RIGHT, 0);
    const auto inputDeadline = GetTickCount64() + 1000;
    while (d->Send(SCI_GETCURRENTPOS) == 0 || GetUpdateRect(d->hwnd, nullptr, FALSE)) {
        MSG message{}; if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        require(dialog.SearchBusy() && GetTickCount64() < inputDeadline);
    }
    while (dialog.SearchBusy()) {
        const auto slice = std::chrono::steady_clock::now();
        MSG message{}; if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        longest = (std::max)(longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - slice).count());
    }
    require(d->Send(SCI_INDICATORVALUEAT, 12, 1999998) != 0 && d->Send(SCI_INDICATORVALUEAT, 12, 1999999) == 0);
    fprintf(stdout, "dense Mark sanity: elapsed %.3f ms, max callback %.3f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(), longest);
    dialog.Close(); d->Send(SCI_SETSAVEPOINT); require(frame.Close(frame.tabs.GetActivePage()));
    results.Clear(); results.Hide(); results.visible = false;
}
inline void RunOrdinarySearchBenchmark(MainFrame& frame, const wchar_t* output) {
    using Clock = std::chrono::steady_clock;
    std::ofstream out{std::filesystem::path(output)};
    out << "workload,operation,implementation,run,elapsed_ms,longest_callback_ms,private_delta_bytes,hits,rows,first_correct_paint_ms,ui_cpu_ms,sampled_peak_private_delta_bytes,process_read_bytes,process_write_bytes\n";
    frame.ShowWindow(SW_SHOWNORMAL);
    auto* d = frame.NewDocument(); d->search = {}; frame.ShowSearch(false);
    auto& dialog = frame.searchDialog; auto& results = frame.searchResults;
    for (int workload = 0; workload < 3; ++workload) {
        std::string text(workload == 2 ? 2000000 : 32000000, 'x');
        if (workload == 2) for (size_t i = 0; i < text.size(); i += 2) text[i] = 'a';
        else text.replace(100, 3, "aaa");
        d->Load({text}); text.clear(); text.shrink_to_fit();
        for (int operation : {0, 1, 2}) for (bool baseline : {true, false}) for (int run = 0; run < 5; ++run) {
            const bool finder = operation == 1, mark = operation == 2;
            d->Send(SCI_SETINDICATORCURRENT, 12); d->Send(SCI_INDICATORCLEARRANGE, 0, d->Send(SCI_GETLENGTH));
            results.Clear(); d->Send(SCI_SETSEL, workload == 0 ? 100 : 0, workload == 0 ? 103 : 0);
            CheckDlgButton(dialog.hwnd, IDC_SEARCH_SELECTION, workload == 0 ? BST_CHECKED : BST_UNCHECKED);
            const auto* pattern = workload == 1 ? L"absent" : L"a";
            SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, pattern); dialog.ReadFields();
            PROCESS_MEMORY_COUNTERS_EX before{}, after{}; before.cb = sizeof(before); after.cb = sizeof(after);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before), sizeof(before));
            std::atomic<SIZE_T> peak{before.PrivateUsage};
            std::jthread sampler([&](std::stop_token stop) {
                while (!stop.stop_requested()) {
                    PROCESS_MEMORY_COUNTERS_EX sample{}; sample.cb = sizeof(sample);
                    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&sample), sizeof(sample));
                    peak.store((std::max)(peak.load(), sample.PrivateUsage)); Sleep(1);
                }
            });
            FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
            GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelBefore, &userBefore);
            IO_COUNTERS ioBefore{}, ioAfter{}; GetProcessIoCounters(GetCurrentProcess(), &ioBefore);
            const auto started = Clock::now(); double longest = 0; size_t hits = 0;
            if (baseline) {
                auto matches = d->CollectMatches(workload == 0); hits = matches.size();
                if (finder) { results.Begin(pattern); results.Add(*d, matches); results.Finish(hits, hits ? 1 : 0); }
                if (mark) {
                    d->Send(SCI_INDICSETSTYLE, 12, INDIC_ROUNDBOX); d->Send(SCI_INDICSETFORE, 12, RGB(235, 160, 0));
                    for (const auto& match : matches) d->Send(SCI_INDICATORFILLRANGE, match.start, match.end - match.start);
                }
                GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after), sizeof(after));
                longest = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            } else {
                auto step = Clock::now(); dialog.Execute(mark ? IDC_SEARCH_MARK : finder ? IDC_FINDALL : IDC_SEARCH_COUNT);
                longest = std::chrono::duration<double, std::milli>(Clock::now() - step).count();
                while (dialog.SearchBusy()) {
                    MSG message{};
                    if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                        step = Clock::now(); TranslateMessage(&message); DispatchMessageW(&message);
                        longest = (std::max)(longest, std::chrono::duration<double, std::milli>(Clock::now() - step).count());
                    } else MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
                }
                wchar_t status[256]{}; GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, status, 256);
                if (swscanf_s(status, L"Completed. %zu matches", &hits) != 1) throw std::runtime_error("Benchmark search interrupted.");
                GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after), sizeof(after));
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            if (baseline) longest = elapsed;
            if (hits != (workload == 0 ? 3u : workload == 1 ? 0u : 1000000u)) throw std::runtime_error("Benchmark hit mismatch.");
            dialog.Status(L"Completed. " + std::to_wstring(hits) + L" matches.");
            RedrawWindow(frame.m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            const auto paint = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelAfter, &userAfter);
            GetProcessIoCounters(GetCurrentProcess(), &ioAfter);
            sampler.request_stop(); sampler.join();
            auto ticks = [](FILETIME value) { return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
            const auto cpu = (ticks(kernelAfter) - ticks(kernelBefore) + ticks(userAfter) - ticks(userBefore)) / 10000.0;
            out << workload << ',' << (mark ? "mark" : finder ? "finder" : "count") << ',' << (baseline ? "before" : "after") << ',' << run << ','
                << elapsed << ',' << longest << ','
                << static_cast<long long>(after.PrivateUsage) - static_cast<long long>(before.PrivateUsage) << ',' << hits << ',' << results.lines.size() << ',' << paint << ',' << cpu << ',' << peak.load() - before.PrivateUsage << ','
                << ioAfter.ReadTransferCount - ioBefore.ReadTransferCount << ',' << ioAfter.WriteTransferCount - ioBefore.WriteTransferCount << '\n';
            out.flush();
        }
    }
    dialog.Close(); d->Send(SCI_SETSAVEPOINT); frame.Close(frame.tabs.GetActivePage());
}

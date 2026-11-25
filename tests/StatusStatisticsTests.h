#pragma once
#include "../src/MainFrame.h"
#include "../src/SimdText.h"
#include "StatusStatisticsBaseline.h"
#include <fstream>
#include <chrono>
#include <source_location>
#include <psapi.h>
#include <thread>
inline void RunStatusStatisticsTests(MainFrame& frame, const wchar_t* benchmark = nullptr) {
    auto check = [](bool value, std::source_location where = std::source_location::current()) {
        if (!value) throw std::runtime_error("Status statistics regression at " + std::to_string(where.line()));
    };
    auto* d = frame.NewDocument();
    auto verify = [&] {
        const auto deadline = GetTickCount64() + 10000;
        while (d->statusStatistics.Update(*d)) check(GetTickCount64() < deadline);
        auto position = d->Send(SCI_GETCURRENTPOS), line = d->Send(SCI_LINEFROMPOSITION, position);
        auto first = d->Send(SCI_POSITIONFROMLINE, line), last = d->Send(SCI_GETLINEENDPOSITION, line);
        auto& stats = d->statusStatistics;
        check(stats.Characters(0) == d->Send(SCI_COUNTCHARACTERS, first, position));
        check(stats.Column(0) == d->Send(SCI_GETCOLUMN, position));
        check(stats.Characters(1) == d->Send(SCI_COUNTCHARACTERS, first, last));
        check(stats.Column(1) == d->Send(SCI_GETCOLUMN, last));
        LRESULT characters = 0, bytes = 0, lines = 0;
        for (LRESULT i = 0; i < d->Send(SCI_GETSELECTIONS); ++i) {
            auto start = d->Send(SCI_GETSELECTIONNSTART, i), end = d->Send(SCI_GETSELECTIONNEND, i);
            characters += d->Send(SCI_COUNTCHARACTERS, start, end); bytes += end - start;
            if (end > start) lines += d->Send(SCI_LINEFROMPOSITION, d->Send(SCI_POSITIONBEFORE, end)) - d->Send(SCI_LINEFROMPOSITION, start) + 1;
        }
        check(stats.SelectionReady() && stats.SelectedCharacters() == characters && stats.selectedBytes == bytes && stats.selectedLines == lines);
    };
    d->Load({std::string(16382, 'a') + "😀\t中\tend\r\nnext\tline\n"});
    for (int tab : {4, 7}) {
        d->Send(SCI_SETTABWIDTH, tab);
        for (LRESULT caret : {LRESULT(1), LRESULT(16386), LRESULT(16390), LRESULT(16395)}) { d->Send(SCI_SETSEL, 0, caret); verify(); }
    }
    d->Send(SCI_SETMULTIPLESELECTION, TRUE); d->Send(SCI_CLEARSELECTIONS); d->Send(SCI_SETSEL, 1, 7); d->Send(SCI_ADDSELECTION, 9, 3); verify();
    d->Send(SCI_SETSELECTIONNANCHOR, 1, 12); verify();
    d->Send(SCI_SETSELECTION, 4, 2); verify();
    d->Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE); d->Send(SCI_SETRECTANGULARSELECTIONANCHOR, 1); d->Send(SCI_SETRECTANGULARSELECTIONCARET, d->Send(SCI_GETLENGTH) - 2); verify();
    d->Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM); d->Send(SCI_CLEARSELECTIONS);
    d->Load({std::string(8 * 1024 * 1024, 'x')});
    // Moving the caret repeatedly must not discard full-line progress.
    const auto deadline = GetTickCount64() + 10000;
    for (size_t i = 0; !d->statusStatistics.Ready(1) || i == 0; ++i) {
        d->Send(SCI_SETSEL, i % 1000, i % 1000); d->statusStatistics.Update(*d); check(GetTickCount64() < deadline);
    }
    d->Send(SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>("中\t")); verify();
    if (benchmark) {
        using Clock = std::chrono::steady_clock;
        std::ofstream out{std::filesystem::path(benchmark)};
        out << "workload,variant,run,completion_ms,callback_p50_ms,callback_p95_ms,callback_p99_ms,callback_max_ms,callbacks,characters,column,selected_characters,ui_cpu_ms,sampled_peak_private_delta,first_correct_paint_ms,process_read_bytes,process_write_bytes\n";
        frame.ShowWindow(SW_SHOWNORMAL);
        for (int workload : {0, 1, 2}) {
            std::string text(workload == 1 ? 100000 : 100 * 1024 * 1024, 'x');
            for (size_t i = 31; i < text.size(); i += 32) text[i] = '\t';
            d->Load({text}); d->Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM); d->Send(SCI_CLEARSELECTIONS);
            if (workload == 1) {
                d->Send(SCI_SETSEL, 0, 0);
                for (size_t i = 1; i < 20000; ++i) d->Send(SCI_ADDSELECTION, i * 4 + 1, i * 4);
            } else d->Send(SCI_SETSEL, text.size(), text.size());
            for (bool baseline : {true, false}) for (int run = 0; run < 3; ++run) {
                auto measure = [&](auto& stats) {
                    FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
                    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelBefore, &userBefore);
                    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
                    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
                    std::atomic<SIZE_T> peak{memory.PrivateUsage};
                    std::jthread sampler([&](std::stop_token stop) {
                        while (!stop.stop_requested()) {
                            PROCESS_MEMORY_COUNTERS_EX sample{}; sample.cb = sizeof(sample);
                            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&sample), sizeof(sample));
                            peak.store((std::max)(peak.load(), sample.PrivateUsage)); Sleep(1);
                        }
                    });
                    IO_COUNTERS beforeIo{}, afterIo{}; GetProcessIoCounters(GetCurrentProcess(), &beforeIo);
                    std::vector<double> calls; const auto start = Clock::now(); bool pending;
                    do {
                        if (workload == 2) d->Send(SCI_SETSEL, calls.size() % 1000, calls.size() % 1000);
                        const auto slice = Clock::now(); pending = stats.Update(*d);
                        calls.push_back(std::chrono::duration<double, std::milli>(Clock::now() - slice).count());
                        // Baseline continuous movement has no convergence; record its failure after 200 slices.
                        if (workload == 2 && baseline && calls.size() == 200) break;
                        check(Clock::now() - start < std::chrono::seconds(30));
                    } while (pending);
                    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                    const auto label = L"Exact characters: " + std::to_wstring(stats.Characters(1)) + L", column: " + std::to_wstring(stats.Column(1));
                    SendMessageW(frame.m_hWndStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(label.c_str()));
                    RedrawWindow(frame.m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                    const auto painted = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelAfter, &userAfter);
                    GetProcessIoCounters(GetCurrentProcess(), &afterIo); sampler.request_stop(); sampler.join();
                    auto ticks = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
                    const auto cpu = (ticks(kernelAfter) - ticks(kernelBefore) + ticks(userAfter) - ticks(userBefore)) / 10000.0;
                    std::sort(calls.begin(), calls.end());
                    auto percentile = [&](size_t p) { return calls[(calls.size() * p + 99) / 100 - 1]; };
                    if (!pending) {
                        check(stats.Characters(1) == static_cast<LRESULT>(text.size()));
                        check(stats.Column(1) == static_cast<LRESULT>(simd::MeasureUtf8(text, 0, d->Send(SCI_GETTABWIDTH)).column));
                        check(stats.SelectedCharacters() == (workload == 1 ? 19999 : 0));
                    }
                    out << workload << ',' << (baseline ? "before" : "after") << ',' << run << ',' << (pending ? -1 : elapsed) << ','
                        << percentile(50) << ',' << percentile(95) << ',' << percentile(99) << ',' << calls.back() << ',' << calls.size() << ','
                        << stats.Characters(1) << ',' << stats.Column(1) << ',' << stats.SelectedCharacters() << ',' << cpu << ',' << peak.load() - memory.PrivateUsage << ','
                        << (pending ? -1 : painted) << ',' << afterIo.ReadTransferCount - beforeIo.ReadTransferCount << ',' << afterIo.WriteTransferCount - beforeIo.WriteTransferCount << '\n'; out.flush();
                };
                if (baseline) { BaselineStatusStatistics stats; measure(stats); }
                else { StatusStatistics stats; measure(stats); }
            }
        }
    }
    d->Send(SCI_SETSAVEPOINT); check(frame.Close(frame.tabs.GetActivePage()));
}

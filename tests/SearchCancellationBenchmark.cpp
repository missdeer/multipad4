#include "SearchWork.h"
#include <psapi.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    try {
        std::ofstream out(argc > 1 ? argv[1] : "search-cancellation.csv");
        out << "variant,run,cancel_ms,private_bytes_during_block,handles_during_block,payload_alive_after_cancel,sampled_peak_private_bytes,sampled_peak_handles\n";
        for (bool baseline : {true, false}) for (int run = 0; run < 21; ++run) {
            HANDLE started = CreateEventW(nullptr, TRUE, FALSE, nullptr), release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            auto payload = std::make_shared<std::vector<char>>(1024 * 1024, 'x'); auto weak = std::weak_ptr(payload);
            std::function<void()> action = [payload, started, release] { SetEvent(started); WaitForSingleObject(release, INFINITE); if (payload->back() != 'x') throw std::runtime_error("Retired payload corruption."); };
            work::Task<void> oldTask; work::SearchTask<void> newTask;
            if (baseline) oldTask = work::Submit(action);
            else newTask = work::SearchTask<void>(work::TrySearchPermit(), [action](const auto&) { action(); });
            action = {}; payload.reset(); if (WaitForSingleObject(started, 10000) != WAIT_OBJECT_0) throw std::runtime_error("Worker failed to enter.");
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
            std::atomic<SIZE_T> peakPrivate{memory.PrivateUsage}; std::atomic<DWORD> peakHandles{handles};
            std::jthread sampler([&](std::stop_token stop) {
                while (!stop.stop_requested()) {
                    PROCESS_MEMORY_COUNTERS_EX sample{}; sample.cb = sizeof(sample); DWORD count = 0;
                    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&sample), sizeof(sample)); GetProcessHandleCount(GetCurrentProcess(), &count);
                    peakPrivate.store((std::max)(peakPrivate.load(), sample.PrivateUsage)); peakHandles.store((std::max)(peakHandles.load(), count)); Sleep(1);
                }
            });
            std::jthread unblock([release] { Sleep(50); SetEvent(release); });
            const auto begin = std::chrono::steady_clock::now();
            if (baseline) oldTask.Reset(); else newTask.Reset();
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            const bool alive = !weak.expired();
            unblock.join(); const auto deadline = GetTickCount64() + 10000;
            while (!weak.expired()) { if (GetTickCount64() > deadline) throw std::runtime_error("Retired payload was not released."); Sleep(1); }
            sampler.request_stop(); sampler.join();
            out << (baseline ? "before" : "after") << ',' << run << ',' << elapsed << ',' << memory.PrivateUsage << ',' << handles << ',' << alive << ',' << peakPrivate.load() << ',' << peakHandles.load() << '\n'; out.flush();
            CloseHandle(started); CloseHandle(release);
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

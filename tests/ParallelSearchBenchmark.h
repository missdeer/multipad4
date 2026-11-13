#pragma once
#include "MainFrame.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <psapi.h>
#include <thread>
// Experimental ASCII literal scans only. This does not change application search.
// Independent models stand in for independent read sessions over identical bytes;
// production sessions must retain the original file version and edited piece tree.
inline void RunParallelSearchBenchmark(MainFrame&, const std::filesystem::path& path) {
    auto check = [](bool value) { if (!value) throw std::runtime_error("Parallel search experiment failed."); };
    using Clock = std::chrono::steady_clock;
    const auto sourceSize = std::filesystem::file_size(path);
    const std::string marker = "\nMultiPad4_Unique_Search_731A9C42\n";
    const std::string nativeNeedle = "MultiPad4_Unique_Search_731A9C42";
    std::vector<std::unique_ptr<large::Document>> documents;
    for (int i = 0; i < 9; ++i) {
        auto source = std::make_shared<files::MappedFile>(path, nullptr, true);
        auto document = std::make_unique<large::Document>(source, files::Encoding::Utf8);
        document->Replace(sourceSize, 0, marker); documents.push_back(std::move(document));
    }
    const auto size = documents[0]->Size();
    auto cpu = [] {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) throw std::runtime_error("Cannot measure process CPU time.");
        auto value = [](FILETIME time) { return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        return (value(kernel) + value(user)) / 10000.0;
    };
    auto reads = [&] { large::Position total = 0; for (const auto& document : documents) total += document->Statistics().sourceBytesRead; return total; };
    auto trial = [&](int run, unsigned workers, bool independent, bool insensitive) {
        struct Result { size_t count = 0; large::Position first = UINT64_MAX; std::exception_ptr error; };
        std::array<Result, 8> results{};
        std::vector<std::thread> threads;
        const auto beforeRead = reads(); const auto beforeCpu = cpu(); const auto start = Clock::now();
        for (unsigned worker = 0; worker < workers; ++worker) threads.emplace_back([&, worker] {
            try {
                const auto first = size * worker / workers, last = size * (worker + 1) / workers;
                auto snapshot = documents[independent ? worker + 1 : 0]->Capture();
                large::Search search(snapshot, files::Encoding::Utf8,
                    {insensitive ? L"multipad4_unique_search_731a9c42" : L"MultiPad4_Unique_Search_731A9C42", false, !insensitive});
                const auto limit = (std::min)(size, last + nativeNeedle.size() - 1); // Include matches crossing a partition.
                auto position = first;
                while (position < last) {
                    auto hit = search.Next(position, limit); if (!hit || hit->start >= last) break;
                    ++results[worker].count; results[worker].first = (std::min)(results[worker].first, hit->start); position = hit->end;
                }
            } catch (...) { results[worker].error = std::current_exception(); }
        });
        for (auto& thread : threads) thread.join();
        const auto wall = std::chrono::duration<double, std::milli>(Clock::now() - start).count(), usedCpu = cpu() - beforeCpu;
        size_t count = 0; large::Position first = UINT64_MAX;
        for (unsigned worker = 0; worker < workers; ++worker) {
            if (results[worker].error) std::rethrow_exception(results[worker].error);
            count += results[worker].count; first = (std::min)(first, results[worker].first);
        }
        check(count == 1 && first == sourceSize + 1);
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE);
        printf("parallel-search,%d,%s,%u,%s,%.3f,%.3f,%llu,%zu,%llu,%zu,%zu\n", run, independent ? "independent-views" : "shared-view", workers, insensitive ? "ignore-case" : "exact", wall, usedCpu, reads() - beforeRead, count, first, memory.PrivateUsage / 1024, memory.WorkingSetSize / 1024); fflush(stdout);
    };
    printf("sourceBytes=%llu; ASCII literal, unique tail marker; one full warmup per configuration; three measured trials; columns=run,reader,workers,case,wallMs,cpuMs,sourceReadBytes,count,first,privateKiB,workingKiB\n", sourceSize); fflush(stdout);
    for (int run = -1; run < 3; ++run) {
        // Reverse configuration order on alternate rounds to reduce order bias.
        for (unsigned item = 0; item < 7; ++item) {
            const auto config = run % 2 == 0 ? 6 - item : item;
            const bool independent = config >= 4;
            const unsigned workers = config < 4 ? 1u << config : 1u << (config - 3);
            trial(run, workers, independent, false); trial(run, workers, independent, true);
        }
    }
    printf("parallel-search verification passed; original disk file unchanged.\n"); fflush(stdout);
}


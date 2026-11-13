#pragma once
#include "FileIO.h"
#include "SimdText.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>
#include <chrono>
#include <psapi.h>
// Reference implementation of the former forced-UTF8 path: each mapped block
// is copied into a fresh string and validated serially before delivery.
inline files::ReadStats ReferenceCopiedRead(const std::filesystem::path& path, const files::ChunkConsumer& consume = {}) {
    files::ReadStats stats;
    PROCESS_MEMORY_COUNTERS before{sizeof(before)}, after{sizeof(after)};
    GetProcessMemoryInfo(GetCurrentProcess(), &before, sizeof(before));
    auto start = std::chrono::steady_clock::now();
    {
        files::MappedFile file(path, &stats);
        auto sample = file.Read(0, 1024 * 1024 + 8);
        for (unsigned long long offset = 0; offset < file.Size();) {
            auto bytes = file.Read(offset, 16 * 1024 * 1024 + 8);
            size_t count = bytes.size();
            if (offset + count < file.Size()) {
                count -= 8;
                while (count && (static_cast<unsigned char>(bytes[count]) & 0xC0) == 0x80) --count;
            }
            bytes.resize(count);
            std::string_view text(bytes);
            if (!offset && text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
            auto check = std::chrono::steady_clock::now();
            if (!simd::IsValidUtf8(text)) throw std::runtime_error("Benchmark requires UTF-8 input.");
            stats.validationMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - check).count();
            stats.validationBytes += text.size(); stats.deliveredBytes += text.size();
            if (consume) {
                auto callback = std::chrono::steady_clock::now(); consume(text);
                stats.callbackMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - callback).count();
            }
            offset += count;
        }
    }
    stats.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    GetProcessMemoryInfo(GetCurrentProcess(), &after, sizeof(after)); stats.pageFaults = after.PageFaultCount - before.PageFaultCount;
    return stats;
}
inline int RunPairedFileReadBenchmark(const std::filesystem::path& path) {
    std::vector<files::ReadStats> runs[2];
    for (int run = -1; run < 5; ++run) for (int item = 0; item < 2; ++item) {
        int variant = run >= 0 && run % 2 ? 1 - item : item;
        files::ReadStats stats;
        if (!variant) stats = ReferenceCopiedRead(path);
        else files::ReadChunks(path, [](std::string_view) {}, {}, files::Encoding::Utf8, &stats);
        if (stats.deliveredBytes != std::filesystem::file_size(path)) throw std::runtime_error("Paired benchmark requires no-BOM UTF-8 input.");
        if (run >= 0) {
            runs[variant].push_back(stats);
            printf("paired,%s,%d,total=%.3f,map=%.3f,read/copy=%.3f,validate=%.3f,decode=%.3f,callback=%.3f,faults=%lu,source=%llu,validated=%llu,delivered=%llu\n", variant ? "optimized" : "reference", run, stats.totalMs, stats.mappingMs, stats.copyOrReadMs, stats.validationMs, stats.decodeMs, stats.callbackMs, stats.pageFaults, stats.sourceBytes, stats.validationBytes, stats.deliveredBytes);
            fflush(stdout);
        }
    }
    for (int variant = 0; variant < 2; ++variant) {
        auto values = runs[variant]; std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.totalMs < b.totalMs; });
        const auto& median = values[2];
        printf("paired summary,%s,median=%.3f,min=%.3f,max=%.3f,map=%.3f,read/copy=%.3f,validate=%.3f,faults=%lu\n", variant ? "optimized" : "reference", median.totalMs, values.front().totalMs, values.back().totalMs, median.mappingMs, median.copyOrReadMs, median.validationMs, median.pageFaults);
    }
    return 0;
}
inline int RunFileReadBenchmark(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    for (auto backend : {files::ReadBackend::Mapped, files::ReadBackend::Buffered}) for (unsigned workers : {1u, 2u, 4u, 8u}) for (bool automatic : {false}) {
        printf("workers=%u backend=%s\n", workers, backend == files::ReadBackend::Mapped ? "mapped" : "buffered ReadFile"); fflush(stdout);
        auto options = files::ReadOptions{}; options.backend = backend; options.validationWorkers = workers;
        std::vector<files::ReadStats> runs;
        // One unreported warm-up, then five independently opened streams.
        for (int run = -1; run < 5; ++run) {
            files::ReadStats stats;
            unsigned long long delivered = 0;
            files::ReadChunks(path, [&](std::string_view bytes) { delivered += bytes.size(); }, options,
                automatic ? std::optional<files::Encoding>{} : files::Encoding::Utf8, &stats);
            if (delivered != size) throw std::runtime_error("Benchmark delivered an incorrect byte count.");
            if (run >= 0) {
                runs.push_back(stats);
                double other = stats.totalMs - stats.mappingMs - stats.copyOrReadMs - stats.probeMs - stats.validationMs - stats.decodeMs - stats.callbackMs;
                printf("raw,%s,%d,total=%.3f,map=%.3f,copy/read=%.3f,probe=%.3f,validate=%.3f,decode=%.3f,callback=%.3f,other=%.3f\n", automatic ? "auto" : "forced", run, stats.totalMs, stats.mappingMs, stats.copyOrReadMs, stats.probeMs, stats.validationMs, stats.decodeMs, stats.callbackMs, other);
                printf("raw,pageFaults=%lu,sourceBytes=%llu,validateBytes=%llu\n", stats.pageFaults, stats.sourceBytes, stats.validationBytes);
                fflush(stdout);
            }
        }
        auto median = [&](auto member) {
            std::vector<double> values; for (const auto& run : runs) values.push_back(run.*member);
            std::sort(values.begin(), values.end()); return values[values.size() / 2];
        };
        auto limits = std::minmax_element(runs.begin(), runs.end(), [](const auto& a, const auto& b) { return a.totalMs < b.totalMs; });
        printf("%s: file=%llu, runs=5, total median=%.3f min=%.3f max=%.3f ms; map=%.3f copy/read=%.3f probe=%.3f validate=%.3f decode=%.3f callback=%.3f ms; source=%llu validateBytes=%llu decodeBytes=%llu delivered=%llu maps=%zu\n",
            automatic ? "auto" : "forced UTF8", size, median(&files::ReadStats::totalMs), limits.first->totalMs, limits.second->totalMs,
            median(&files::ReadStats::mappingMs), median(&files::ReadStats::copyOrReadMs), median(&files::ReadStats::probeMs),
            median(&files::ReadStats::validationMs), median(&files::ReadStats::decodeMs), median(&files::ReadStats::callbackMs),
            runs[0].sourceBytes, runs[0].validationBytes, runs[0].decodedBytes, runs[0].deliveredBytes, runs[0].mappings);
    }
    // Keep one representative 64 MiB block in memory to separate validation
    // compute from file paging, allocation, and repeated input copies.
    files::MappedFile file(path); auto sample = file.Read(0, 64 * 1024 * 1024 + 4);
    if (sample.size() > 64 * 1024 * 1024) {
        size_t count = 64 * 1024 * 1024;
        while ((static_cast<unsigned char>(sample[count]) & 0xC0) == 0x80) --count;
        sample.resize(count);
    }
    for (unsigned workers : {1u, 2u, 4u, 8u}) {
        std::vector<double> timings;
        for (int run = -1; run < 5; ++run) {
            auto start = std::chrono::steady_clock::now();
            std::vector<std::thread> threads; std::vector<unsigned char> valid(workers, 1);
            auto work = [&](unsigned index) {
                size_t begin = sample.size() * index / workers, end = sample.size() * (index + 1) / workers;
                while (begin && begin < sample.size() && (static_cast<unsigned char>(sample[begin]) & 0xC0) == 0x80) ++begin;
                while (end < sample.size() && (static_cast<unsigned char>(sample[end]) & 0xC0) == 0x80) ++end;
                for (int repeat = 0; repeat < 32; ++repeat) if (!simd::IsValidUtf8(std::string_view(sample).substr(begin, end - begin))) valid[index] = 0;
            };
            if (workers == 1) work(0);
            else { for (unsigned i = 0; i < workers; ++i) threads.emplace_back(work, i); for (auto& thread : threads) thread.join(); }
            for (auto value : valid) if (!value) throw std::runtime_error("Invalid UTF-8 benchmark sample.");
            double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (run >= 0) timings.push_back(elapsed);
        }
        std::sort(timings.begin(), timings.end());
        printf("resident SIMD validation, workers=%u, bytes=%zu: median=%.3f min=%.3f max=%.3f ms\n", workers, sample.size() * 32, timings[2], timings.front(), timings.back());
    }
    return 0;
}

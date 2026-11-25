#pragma once
#include "FileIO.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
inline int RunSaveEncodingBenchmark(const wchar_t* mode) {
    const bool before = wcscmp(mode, L"before") == 0;
    if (!before && wcscmp(mode, L"after") != 0) throw std::runtime_error("Expected before or after");
    const auto path = std::filesystem::temp_directory_path() / (L"multipad-save-bench-" + std::to_wstring(GetCurrentProcessId()));
    std::string editorText;
    while (editorText.size() < 16 * 1024 * 1024) editorText += "café ordinary editor save benchmark\r\n";
    using Clock = std::chrono::steady_clock;
    auto cpu = [] {
        FILETIME create{}, exit{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
        auto ticks = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.;
    };
    try {
        for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LE, files::FromCodePage(1252)}) {
            std::vector<double> elapsed, processor;
            for (unsigned i = 0; i < 12; ++i) {
                const auto start = Clock::now(); const auto cpuStart = cpu();
                // Reproduce the old/new synchronous Save preparation and atomic commit,
                // excluding lexer refresh, plugins, and first paint from both sides.
                files::Text text{editorText, encoding};
                if (before) { files::Encode(text); files::WriteAtomic(path, text); }
                else {
                    std::string bytes;
                    if (encoding == files::Encoding::Utf8) {
                        files::EncodeChunks(text, [](std::string_view) {});
                        bytes = std::move(text.utf8);
                    } else bytes = files::Encode(text);
                    files::WriteAtomicChunks(path, [&](const files::ChunkConsumer& consume) { consume(bytes); });
                }
                const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                const auto cpuMs = cpu() - cpuStart;
                if (i) { elapsed.push_back(ms); processor.push_back(cpuMs); }
                if (files::ReadWithEncoding(path, encoding).utf8 != editorText) throw std::runtime_error("Saved bytes changed");
            }
            std::sort(elapsed.begin(), elapsed.end()); std::sort(processor.begin(), processor.end());
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            std::cout << (before ? "before" : "after") << " encoding=" << static_cast<unsigned>(encoding)
                << " input=" << editorText.size() << " median_ms=" << elapsed[5] << " p95_ms=" << elapsed[10]
                << " p99_ms=" << elapsed[10] << " max_ms=" << elapsed.back() << " median_cpu_ms=" << processor[5]
                << " peak_private_bytes=" << memory.PeakPagefileUsage << " encode_calls=" << (before ? 2 : 1)
                << " output_bytes=" << std::filesystem::file_size(path) << '\n';
        }
        std::filesystem::remove(path); return 0;
    } catch (...) { std::error_code ec; std::filesystem::remove(path, ec); throw; }
}

#include "../src/FileIO.cpp"
#include "ReadChunksBaseline.h"
#include <iostream>
#include <chrono>
int main(int argc, char** argv) {
    const auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-probe-" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(directory);
        std::ofstream metrics(argc > 1 ? argv[1] : "read-chunks-probe.csv");
        metrics << "workload,variant,run,elapsed_ms,probe_ms,source_bytes,validation_bytes,decoded_bytes,delivered_bytes\n";
        for (int workload : {0, 1, 2, 3}) {
            const bool small = workload < 2, forced = workload % 2 == 0;
            const size_t count = small ? 100 : 1, length = small ? 1500 : 32 * 1024 * 1024;
            std::vector<std::filesystem::path> paths;
            for (size_t i = 0; i < count; ++i) {
                paths.push_back(directory / (std::to_wstring(i) + L".txt")); files::WriteAtomic(paths.back(), {std::string(length, 'a')});
            }
            for (bool baseline : {true, false}) for (int run = 0; run < 5; ++run) {
                files::ReadStats totals; const auto start = std::chrono::steady_clock::now();
                for (const auto& path : paths) {
                    files::ReadStats stats; size_t delivered = 0;
                    const auto consume = [&](std::string_view text) { delivered += text.size(); if (!text.empty() && (text.front() != 'a' || text.back() != 'a')) throw std::runtime_error("Probe benchmark changed bytes."); };
                    const auto codec = forced ? std::optional{files::Encoding::Utf8} : std::nullopt;
                    const auto encoding = baseline ? files::BaselineReadChunks(path, consume, {}, codec, &stats) : files::ReadChunks(path, consume, {}, codec, &stats);
                    if (encoding != files::Encoding::Utf8 || delivered != length) throw std::runtime_error("Probe benchmark mismatch.");
                    totals.probeMs += stats.probeMs; totals.sourceBytes += stats.sourceBytes; totals.validationBytes += stats.validationBytes; totals.decodedBytes += stats.decodedBytes; totals.deliveredBytes += stats.deliveredBytes;
                }
                metrics << workload << ',' << (baseline ? "before" : "after") << ',' << run << ',' << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << ','
                    << totals.probeMs << ',' << totals.sourceBytes << ',' << totals.validationBytes << ',' << totals.decodedBytes << ',' << totals.deliveredBytes << '\n'; metrics.flush();
            }
        }
        std::filesystem::remove_all(directory); return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; std::error_code ignored; std::filesystem::remove_all(directory, ignored); return 1; }
}

#define MULTIPAD4_FILE_STREAM_TEST
#include "../src/FileSearch.cpp"
#include <chrono>
#include <cstdio>
#include <psapi.h>
int main(int argc, char**) {
    const auto root = std::filesystem::temp_directory_path() / (L"MultiPad4-stream-bench-" + std::to_wstring(GetCurrentProcessId()));
    try {
        const std::string sample = "a\r\nneedle\rneedle\nbanana 中\r\nend";
        std::atomic<bool> stop{false};
        for (const auto& needle : {std::string("needle"), std::string("\r"), std::string("\n"), std::string("\r\n"), std::string("ana"), std::string("中")}) {
            const auto snapshot = large::Document::FromUtf8(std::make_shared<const std::string>(sample))->Capture();
            const auto expected = files::SearchSnapshot(snapshot, {files::FromUtf8(needle), false, true}, stop).archive;
            for (size_t chunk = 1; chunk <= sample.size(); ++chunk) {
                files::LiteralStream stream(needle, stop);
                for (size_t at = 0; at < sample.size(); at += chunk) stream.Feed(std::string_view(sample).substr(at, chunk));
                if (stream.hits->Count() != expected->Count()) throw std::runtime_error("Stream overlap count mismatch");
                const auto a = stream.hits->Page(0, stream.hits->Count()), b = expected->Page(0, expected->Count());
                for (size_t i = 0; i < a.size(); ++i) if (a[i].start != b[i].start || a[i].end != b[i].end || a[i].line != b[i].line || a[i].lineStart != b[i].lineStart)
                    throw std::runtime_error("Stream overlap line/offset mismatch");
            }
        }
        files::LiteralStream interrupted("needle", stop); interrupted.Feed("needle"); stop = true;
        bool cancelled = false; try { interrupted.Feed("needle"); } catch (const std::runtime_error&) { cancelled = true; }
        if (!cancelled) throw std::runtime_error("Stream cancellation ignored"); stop = false;
        if (argc > 1) { puts("Stream literal chunk parity passed."); return 0; }
        std::filesystem::create_directories(root);
        for (bool dense : {false, true}) {
            std::string text(2 * 1024 * 1024, 'x');
            if (dense) for (size_t i = 0; i < text.size(); i += 128) text.replace(i, 6, "needle");
            for (int i = 0; i < 8; ++i) files::WriteAtomic(root / (std::to_wstring(i) + L".txt"), {text});
            std::vector<double> times; unsigned long long temporary = 0, decoded = 0;
            for (int i = 0; i < 7; ++i) {
                files::tempSnapshotBytes = 0; files::decodedBytes = 0; size_t hits = 0;
                const auto start = std::chrono::steady_clock::now();
                files::DirectorySearchOptions options{root}; options.workers = 1;
                files::FileSearch search(options, {L"needle", false, true});
                while (!search.Done()) {
                    files::FileSearchResult result;
                    while (search.Poll(result)) { hits += result.archive->Count(); if (large::Document::Read(*result.snapshot, 0, 6) != "needle" && dense) throw std::runtime_error("Invalid retained version"); }
                    SwitchToThread();
                }
                if (search.Errors() || hits != (dense ? 8 * 16384 : 0)) throw std::runtime_error("Incorrect stream benchmark result");
                times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                temporary += files::tempSnapshotBytes; decoded += files::decodedBytes;
            }
            std::sort(times.begin(), times.end()); PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            printf("dense=%d files=8 bytes/file=2097152 samples7 median=%.3f max=%.3f tempSnapshotBytes=%llu decodedBytes=%llu privateBytes=%zu\n", dense, times[3], times[6], temporary / 7, decoded / 7, memory.PrivateUsage);
        }
        std::filesystem::remove_all(root); return 0;
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); std::error_code ignored; std::filesystem::remove_all(root, ignored); return 1; }
}

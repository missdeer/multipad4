#include "FileSearch.h"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <map>
#include <source_location>
#include <stdexcept>
#include <thread>
#include <psapi.h>
#include <shlwapi.h>
#include <atomic>

void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("File search regression at line " + std::to_string(where.line()));
}
using Results = std::map<std::filesystem::path, files::FileSearchResult>;
Results Collect(files::DirectorySearchOptions options, large::SearchOptions pattern,
    std::vector<std::filesystem::path> open = {}) {
    files::FileSearch search(options, pattern, std::move(open));
    Results results;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!search.Done()) {
        files::FileSearchResult result;
        while (search.Poll(result)) { Check(!results.contains(result.path)); results.emplace(result.path, std::move(result)); }
        Check(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    Check(search.Errors() == 0); return results;
}
int main(int argc, char** argv) {
    const auto root = std::filesystem::temp_directory_path() / (L"MultiPad4-file-search-" + std::to_wstring(GetCurrentProcessId()));
    try {
        if (argc > 1) {
            std::filesystem::create_directories(root);
            std::vector<std::filesystem::path> paths, open;
            for (int i = 0; i < 2000; ++i) { auto file = root / (std::to_wstring(i) + L".txt"); files::WriteAtomic(file, {"needle"}); paths.push_back(file); if (i < 128) open.push_back(file); }
            const std::string mode = argv[1];
            if (mode == "walk-baseline" || mode == "walk-optimized") {
                const auto begin = std::chrono::steady_clock::now(); size_t count = 0, attributes = 0;
                if (mode == "walk-baseline") {
                    for (const auto& entry : std::filesystem::directory_iterator(root)) {
                        const auto data = GetFileAttributesW(entry.path().c_str()); ++attributes;
                        if (data != INVALID_FILE_ATTRIBUTES && !(data & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && entry.is_regular_file() && PathMatchSpecW(entry.path().filename().c_str(), L"*.txt")) ++count;
                    }
                } else {
                    files::SearchWalker walker({root, L"*.txt"}); std::atomic<bool> cancel{false}; size_t errors = 0;
                    while (walker.Next(cancel, errors)) ++count; Check(errors == 0);
                }
                std::cout << mode << ",files=" << count << ",elapsed_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count() << ",extra_attribute_queries=" << attributes << "\n";
                Check(count == 2000); std::filesystem::remove_all(root); return 0;
            }
            if (mode == "walker") {
                files::SearchWalker walker({root, L"*.txt"}); std::atomic<bool> cancel{false};
                std::mutex mutex; std::atomic<size_t> files{0}, failures{0}; std::atomic<long long> waitNs{0}, holdNs{0};
                const auto begin = std::chrono::steady_clock::now(); std::vector<std::thread> workers;
                for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
                    for (;;) {
                        const auto before = std::chrono::steady_clock::now(); std::unique_lock lock(mutex);
                        const auto acquired = std::chrono::steady_clock::now(); size_t errors = 0;
                        auto path = walker.Next(cancel, errors); const auto after = std::chrono::steady_clock::now(); lock.unlock();
                        waitNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(acquired-before).count());
                        holdNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(after-acquired).count()); failures += errors;
                        if (!path) break;
                        Check(files::Read(*path).utf8 == "needle"); ++files;
                    }
                });
                for (auto& worker : workers) worker.join();
                std::cout << "walker,files=" << files << ",workers=8,elapsed_ms=" << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()
                    << ",aggregate_lock_wait_ms=" << waitNs.load()/1e6 << ",aggregate_lock_hold_ms=" << holdNs.load()/1e6 << ",errors=" << failures << "\n";
                Check(files == 2000 && failures == 0); std::filesystem::remove_all(root); return 0;
            }
            const bool baseline = mode == "baseline";
            FILETIME created{}, exited{}, kernelStart{}, userStart{}, kernelEnd{}, userEnd{};
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelStart, &userStart);
            auto begin = std::chrono::steady_clock::now(); size_t matches = 0, calls = 0, comparisons = 0;
            if (baseline) {
                for (const auto& path : paths) for (const auto& item : open) { ++calls; ++comparisons; std::error_code error; if (std::filesystem::equivalent(item, path, error)) { ++matches; break; } }
            } else {
                files::OpenFileIndex index(open); calls += open.size();
                for (const auto& path : paths) { ++calls; if (index.Find(path)) { ++matches; ++comparisons; } }
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelEnd, &userEnd);
            auto ticks = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
            const auto cpu = (ticks(kernelEnd) + ticks(userEnd) - ticks(kernelStart) - ticks(userStart)) / 10000.0;
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory); K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            std::cout << argv[1] << ",cpu_ms=" << cpu << ",peak_private=" << memory.PeakPagefileUsage << ",files=2000,tabs=128,elapsed_ms=" << elapsed << ",identity_operations=" << calls << ",equivalent_checks=" << comparisons << ",matches=" << matches << "\n";
            Check(matches == 128); std::filesystem::remove_all(root); return 0;
        }
        std::filesystem::create_directories(root / L"sub");
        std::filesystem::create_directories(root / L"hidden");
        Check(SetFileAttributesW((root / L"hidden").c_str(), FILE_ATTRIBUTE_HIDDEN));
        files::WriteAtomic(root / L"hidden" / L"secret.txt", {"needle"});
        files::WriteAtomic(root / L"sub" / L"unicode.txt", {"中文 needle\r\nneedle\rneedle\n", files::Encoding::Utf16LE});
        files::WriteAtomic(root / L"binary.txt", {std::string("needle\0needle", 13)});
        files::WriteAtomic(root / L"ignored.bin", {"needle"});
        files::WriteAtomic(root / L"noextension", {"needle"});
        for (int i = 0; i < 30; ++i) files::WriteAtomic(root / (std::to_wstring(i) + L".txt"), {"needle NEEDLE needles\n"});
        files::DirectorySearchOptions options{root, L"*.txt"}; options.workers = 1;
        large::SearchOptions pattern{L"needle", false, true, true};
        auto serial = Collect(options, pattern); Check(serial.size() == 31);
        options.workers = 4;
        auto parallel = Collect(options, pattern); Check(parallel.size() == serial.size());
        for (const auto& [path, result] : serial) {
            const auto& other = parallel.at(path);
            Check(*result.text == *other.text && result.matches.size() == other.matches.size());
            for (size_t i = 0; i < result.matches.size(); ++i) {
                Check(result.matches[i].start == other.matches[i].start && result.matches[i].line == other.matches[i].line);
                Check(result.text->substr(result.matches[i].start, result.matches[i].end - result.matches[i].start) == "needle");
            }
        }
        const auto& unicode = parallel.at(root / L"sub" / L"unicode.txt");
        Check(unicode.matches.size() == 3 && unicode.matches[1].line == 1 && unicode.matches[2].line == 2);
        {
            auto alias = root / L"alias.link"; Check(CreateHardLinkW(alias.c_str(), (root / L"0.txt").c_str(), nullptr));
            files::OpenFileIndex index({root / L"0.txt", root / L"missing.txt"});
            Check(index.Find(alias) == 0); Check(index.Find(root / L"0.TXT") == 0); Check(!index.Find(root / L"1.txt"));
            Check(!index.Find(root / L"missing.txt"));
            // Native traversal excludes both file symlinks and directory reparse points.
            const auto symlink = root / L"link.txt";
            const bool links = CreateSymbolicLinkW(symlink.c_str(), (root / L"0.txt").c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != FALSE;
            if (links) { Check(Collect(options, pattern).size() == serial.size()); DeleteFileW(symlink.c_str()); }
            DeleteFileW(alias.c_str());
            const auto atomic = root / L"atomic.test", retainedAlias = root / L"retained.test";
            files::WriteAtomic(atomic, {"disk miss"}); Check(CreateHardLinkW(retainedAlias.c_str(), atomic.c_str(), nullptr));
            files::OpenFileIndex beforeTraversal({atomic, retainedAlias});
            auto replacement = atomic; replacement += L".new";
            files::WriteAtomic(replacement, {"another disk miss"});
            Check(MoveFileExW(replacement.c_str(), atomic.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
            std::atomic<bool> traversing{false}; size_t traversalErrors = 0;
            files::SearchWalker traversal({root, L"atomic.test"}); auto replacedCandidate = traversal.Next(traversing, traversalErrors);
            Check(replacedCandidate && traversalErrors == 0 && beforeTraversal.Find(*replacedCandidate) == 0);
            // The first tab's path changed identity; retain the second tab's duplicate-ID entry.
            Check(beforeTraversal.Find(retainedAlias) == 1);
            Check(files::Read(atomic).utf8.find("needle") == std::string::npos);
            DeleteFileW(atomic.c_str()); DeleteFileW(retainedAlias.c_str());
            std::atomic<bool> stop{false}; size_t errors = 0;
            files::SearchWalker missing({root / L"missing-directory", L"*"}); Check(!missing.Next(stop, errors) && errors == 1);
        }
        options.recursive = false; Check(Collect(options, pattern).size() == 30);
        options.recursive = true; options.hidden = true; Check(Collect(options, pattern).size() == 32);
        options.filter = L"*.*"; Check(Collect(options, pattern).size() == 34);
        options.filter = L"*.txt *.bin"; Check(Collect(options, pattern).size() == 33);
        pattern.regex = true; pattern.wholeWord = false; pattern.pattern = L"(?=needle)";
        auto zero = Collect(options, pattern); Check(zero.at(root / L"0.txt").matches.size() == 2);
        pattern.pattern = L"needle\\r\\nneedle";
        Check(Collect(options, pattern).at(root / L"sub" / L"unicode.txt").matches.size() == 1);
        pattern.pattern = L"\\n";
        auto newline = Collect(options, pattern).at(root / L"sub" / L"unicode.txt");
        Check(newline.matches[0].line == 0 && newline.matches[1].line == 2);
        pattern = {L"absent"};
        auto open = Collect(options, pattern, {root / L"0.txt"});
        Check(open.size() == 1 && open.begin()->second.openDocument);
        {
            struct Locked { HANDLE file = INVALID_HANDLE_VALUE; ~Locked() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); } } locked;
            locked.file = CreateFileW((root / L"0.txt").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr); Check(locked.file != INVALID_HANDLE_VALUE);
            Check(!files::QueryIdentity(root / L"missing.txt"));
            auto blockedOptions = options; blockedOptions.filter = L"0.txt";
            files::FileSearch blocked(blockedOptions, {L"needle"});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!blocked.Done()) { files::FileSearchResult result; Check(!blocked.Poll(result)); Check(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
            Check(blocked.Errors() == 1);
        }
        bool invalid = false;
        try { files::FileSearch search(options, {L"[", true}); } catch (const std::exception&) { invalid = true; }
        Check(invalid);
        std::atomic<bool> cancelled{true};
        files::ReadOptions read; read.cancel = &cancelled;
        bool readCancelled = false;
        try { files::ReadChunks(root / L"0.txt", [](std::string_view) { Check(false); }, read); }
        catch (const std::runtime_error& error) { readCancelled = std::string_view(error.what()).find("cancelled") != std::string_view::npos; }
        Check(readCancelled);
        // Stop while workers are blocked publishing into the bounded queue.
        files::FileSearch stopped(options, {L"needle"});
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); stopped.Stop();
        files::FileSearchResult result; while (stopped.Poll(result)) {}
        Check(stopped.Done());
        constexpr size_t blockBytes = 64 * 1024;
        const auto boundaryText = std::make_shared<const std::string>(std::string(blockBytes - 1, 'x') + "\r\nneedle\rneedle\nneedle");
        const auto boundaryPath = root / L"boundary.test";
        files::WriteAtomic(boundaryPath, {*boundaryText});
        options.filter = L"*.test"; pattern = {L"\\n|needle", true, true};
        const auto boundary = Collect(options, pattern).at(boundaryPath);
        const auto snapshot = large::Document::FromUtf8(boundaryText)->Capture();
        std::atomic<bool> active{false};
        const auto hits = files::SearchSnapshot(snapshot, pattern, active).archive->Page(0, 6);
        const files::FileSearchMatch expected[] = {
            {blockBytes, blockBytes + 1, 0, 0},
            {blockBytes + 1, blockBytes + 7, 1, blockBytes + 1},
            {blockBytes + 8, blockBytes + 14, 2, blockBytes + 8},
            {blockBytes + 14, blockBytes + 15, 2, blockBytes + 8},
            {blockBytes + 15, blockBytes + 21, 3, blockBytes + 15},
        };
        Check(boundary.matches.size() == std::size(expected) && hits.size() == std::size(expected));
        for (size_t index = 0; index < std::size(expected); ++index) {
            const auto& match = expected[index];
            const auto& disk = boundary.matches[index];
            const auto& openHit = hits[index];
            Check(disk.start == match.start && disk.end == match.end && disk.line == match.line && disk.lineStart == match.lineStart);
            Check(openHit.start == match.start && openHit.end == match.end && openHit.line == match.line && openHit.lineStart == match.lineStart);
        }
        // Large files and dense hits retain disk archives, not one allocation per hit or full file.
        files::WriteAtomic(root / L"large.archive", {std::string(2 * 1024 * 1024, 'x') + "\r\nneedle\rneedle\n"});
        options.filter = L"*.archive"; pattern = {L"needle", false, true};
        auto archived = Collect(options, pattern);
        const auto& large = archived.at(root / L"large.archive");
        Check(!large.text && large.matches.empty() && large.archive && large.archive->Count() == 2);
        Check(large.archive->Page(0, 2)[0].line == 1 && large.archive->Page(1, 1)[0].line == 2);
        Check(large::Document::Read(*large.snapshot, 2 * 1024 * 1024 + 2, 6) == "needle");
        std::string dense; for (int i = 0; i < 5000; ++i) dense += "needle\n";
        files::WriteAtomic(root / L"dense.archive", {dense});
        auto denseResults = Collect(options, pattern);
        const auto& many = denseResults.at(root / L"dense.archive");
        Check(!many.text && many.matches.empty() && many.archive->Count() == 5000);
        Check(many.archive->Page(4999, 1)[0].line == 4999 && many.archive->Page(4999, 1)[0].start == 4999 * 7);
        // Published snapshots are stable after the original path is replaced.
        files::WriteAtomic(root / L"large.archive", {"changed"});
        Check(large::Document::Read(*large.snapshot, 2 * 1024 * 1024 + 2, 6) == "needle");
        std::filesystem::remove_all(root); std::cout << "File search tests passed.\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; std::filesystem::remove_all(root); return 1;
    }
}

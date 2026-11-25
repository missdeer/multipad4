#include "FileSearch.h"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <map>
#include <source_location>
#include <stdexcept>
#include <thread>
#include <fstream>

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
    if (search.Errors()) std::cerr << "Search errors: " << search.Errors() << " pattern=" << files::ToUtf8(pattern.pattern) << " filter=" << files::ToUtf8(options.filter) << "\n"; Check(search.Errors() == 0); return results;
}
std::vector<large::SearchHit> Hits(const files::FileSearchResult& result) { return result.archive->Page(0, result.archive->Count()); }
std::string Text(const files::FileSearchResult& result) { return large::Document::Read(*result.snapshot, 0, static_cast<size_t>(large::Document::Size(*result.snapshot))); }
int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"MultiPad4-file-search-" + std::to_wstring(GetCurrentProcessId()));
    try {
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
            Check(Text(result) == Text(other) && result.archive->Count() == other.archive->Count());
            for (size_t i = 0; i < Hits(result).size(); ++i) {
                Check(Hits(result)[i].start == Hits(other)[i].start && Hits(result)[i].line == Hits(other)[i].line);
                Check(Text(result).substr(Hits(result)[i].start, Hits(result)[i].end - Hits(result)[i].start) == "needle");
            }
        }
        const auto& unicode = parallel.at(root / L"sub" / L"unicode.txt");
        Check(Hits(unicode).size() == 3 && Hits(unicode)[1].line == 1 && Hits(unicode)[2].line == 2);
        options.recursive = false; Check(Collect(options, pattern).size() == 30);
        options.recursive = true; options.hidden = true; Check(Collect(options, pattern).size() == 32);
        options.filter = L"*.*"; Check(Collect(options, pattern).size() == 34);
        options.filter = L"*.txt *.bin"; Check(Collect(options, pattern).size() == 33);
        pattern.regex = true; pattern.wholeWord = false; pattern.pattern = L"(?=needle)";
        auto zero = Collect(options, pattern); Check(zero.at(root / L"0.txt").archive->Count() == 2);
        pattern.pattern = L"needle\\r\\nneedle";
        Check(Collect(options, pattern).at(root / L"sub" / L"unicode.txt").archive->Count() == 1);
        pattern.pattern = L"\\n";
        auto newline = Collect(options, pattern).at(root / L"sub" / L"unicode.txt");
        Check(Hits(newline)[0].line == 0 && Hits(newline)[1].line == 2);
        pattern = {L"absent"};
        auto open = Collect(options, pattern, {root / L"0.txt"});
        Check(open.size() == 1 && open.begin()->second.openDocument);
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
        Check(Hits(boundary).size() == std::size(expected) && hits.size() == std::size(expected));
        for (size_t index = 0; index < std::size(expected); ++index) {
            const auto& match = expected[index];
            const auto disk = Hits(boundary)[index];
            const auto& openHit = hits[index];
            Check(disk.start == match.start && disk.end == match.end && disk.line == match.line && disk.lineStart == match.lineStart);
            Check(openHit.start == match.start && openHit.end == match.end && openHit.line == match.line && openHit.lineStart == match.lineStart);
        }
        // Large files and dense hits retain disk archives, not one allocation per hit or full file.
        files::WriteAtomic(root / L"large.archive", {std::string(2 * 1024 * 1024, 'x') + "\r\nneedle\rneedle\n"});
        options.filter = L"*.archive"; pattern = {L"needle", false, true};
        auto archived = Collect(options, pattern);
        const auto& large = archived.at(root / L"large.archive");
        Check(large.archive && large.archive->Count() == 2);
        Check(large.archive->Page(0, 2)[0].line == 1 && large.archive->Page(1, 1)[0].line == 2);
        Check(large::Document::Read(*large.snapshot, 2 * 1024 * 1024 + 2, 6) == "needle");
        std::string dense; for (int i = 0; i < 5000; ++i) dense += "needle\n";
        files::WriteAtomic(root / L"dense.archive", {dense});
        auto denseResults = Collect(options, pattern);
        const auto& many = denseResults.at(root / L"dense.archive");
        Check(many.archive->Count() == 5000);
        Check(many.archive->Page(4999, 1)[0].line == 4999 && many.archive->Page(4999, 1)[0].start == 4999 * 7);
        // Published snapshots are stable after the original path is replaced.
        files::WriteAtomic(root / L"large.archive", {"changed"});
        Check(large::Document::Read(*large.snapshot, 2 * 1024 * 1024 + 2, 6) == "needle");
        // Exact literal streams retain overlap and line coordinates across decoder chunks.
        const auto streamedPath = root / L"streamed.literal";
        for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LE, files::Encoding::Utf16BE}) {
            const std::string content = std::string(1024 * 1024 - 3, 'x') + "needle\r\nneedle\rneedle\n";
            files::WriteAtomic(streamedPath, {content, encoding});
            options.filter = L"*.literal"; pattern = {L"needle", false, true};
            auto streamed = Collect(options, pattern); const auto& result = streamed.at(streamedPath);
            Check(result.archive->Count() == 3 && Text(result) == content);
            auto direct = files::SearchSnapshot(large::Document::FromUtf8(std::make_shared<const std::string>(content))->Capture(), pattern, active);
            const auto expectedHits = Hits(direct), actualHits = Hits(result);
            for (size_t i = 0; i < expectedHits.size(); ++i) Check(actualHits[i].start == expectedHits[i].start && actualHits[i].line == expectedHits[i].line && actualHits[i].lineStart == expectedHits[i].lineStart);
            files::WriteAtomic(streamedPath, {"replacement"}); std::filesystem::remove(streamedPath); Check(Text(result) == content);
        }
        files::WriteAtomic(streamedPath, {"needle" + std::string(2 * 1024 * 1024, 'x') + std::string(1, '\0')});
        Check(Collect(options, pattern).empty());
        // A malformed UTF-8 suffix after provisional hits must publish nothing.
        {
            std::ofstream output(streamedPath, std::ios::binary | std::ios::trunc);
            output << "\xEF\xBB\xBF" << "needle" << std::string(2 * 1024 * 1024, 'x') << "\xFF";
        }
        {
            files::FileSearch invalidSuffix(options, pattern);
            while (!invalidSuffix.Done()) {
                files::FileSearchResult provisional; Check(!invalidSuffix.Poll(provisional)); std::this_thread::yield();
            }
            Check(invalidSuffix.Errors() == 1);
        }
        const auto legacy = files::FromCodePage(1252);
        files::WriteAtomic(streamedPath, {"# coding: windows-1252\nneedle" + std::string(2 * 1024 * 1024, 'x') + "é", legacy});
        Check(Collect(options, pattern).at(streamedPath).archive->Count() == 1);
        std::filesystem::remove_all(root); std::cout << "File search tests passed.\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; std::filesystem::remove_all(root); return 1;
    }
}

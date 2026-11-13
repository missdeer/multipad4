#include "FileSearch.h"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <map>
#include <source_location>
#include <stdexcept>

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
            Check(*result.text == *other.text && result.matches.size() == other.matches.size());
            for (size_t i = 0; i < result.matches.size(); ++i) {
                Check(result.matches[i].start == other.matches[i].start && result.matches[i].line == other.matches[i].line);
                Check(result.text->substr(result.matches[i].start, result.matches[i].end - result.matches[i].start) == "needle");
            }
        }
        const auto& unicode = parallel.at(root / L"sub" / L"unicode.txt");
        Check(unicode.matches.size() == 3 && unicode.matches[1].line == 1 && unicode.matches[2].line == 2);
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
        std::filesystem::remove_all(root); std::cout << "File search tests passed.\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; std::filesystem::remove_all(root); return 1;
    }
}

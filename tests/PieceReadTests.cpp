#define MULTIPAD4_PIECE_READ_TEST
#include "../src/LazyDocument.cpp"
#include <source_location>
#include <chrono>
#include <future>
#include <fstream>
#include <cstdio>

void Require(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Piece read regression line " + std::to_string(where.line()));
}
void Verify(large::Document& document, const std::string& expected) {
    auto snapshot = document.Capture(); large::Document::ReadSession session(snapshot);
    for (size_t at : {size_t(0), size_t(1), expected.size() / 2, expected.size()}) {
        for (size_t count : {size_t(0), size_t(1), size_t(127), expected.size(), SIZE_MAX}) {
            auto bytes = expected.substr(at, count);
            Require(large::Document::Read(snapshot, at, count) == bytes);
            Require(session.Read(at, count) == bytes);
        }
    }
    bool rejected = false; try { large::Document::Read(snapshot, expected.size() + 1, 0); } catch (const std::runtime_error&) { rejected = true; } Require(rejected);
    rejected = false; try { session.Read(expected.size() + 1, 0); } catch (const std::runtime_error&) { rejected = true; } Require(rejected);
    std::vector<std::future<void>> readers;
    for (int i = 0; i < 4; ++i) readers.push_back(std::async(std::launch::async, [snapshot, expected] {
        large::Document::ReadSession independent(snapshot);
        for (int run = 0; run < 25; ++run) {
            Require(independent.Read(0, expected.size()) == expected);
            Require(large::Document::Read(snapshot, 0, expected.size()) == expected);
        }
    }));
    document.Replace(0, 0, "later");
    for (auto& reader : readers) reader.get();
    Require(session.Read(0, expected.size()) == expected);
}
int main(int argc, char**) {
    auto path = std::filesystem::temp_directory_path() / (L"MultiPad4-piece-read-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    try {
        auto empty = large::Document::FromUtf8(std::make_shared<const std::string>(""));
        Require(empty->Read(0, SIZE_MAX).empty());
        large::Document::ReadSession emptySession(empty->Capture()); Require(emptySession.Read(0, SIZE_MAX).empty());
        const std::string text = std::string(128 * 1024, 'a') + "中😀\r\ntail";
        files::WriteAtomic(path, {text});
        auto file = std::make_shared<files::MappedFile>(path, nullptr, true);
        large::Document mapped(file, files::Encoding::Utf8);
        auto before = mapped.Statistics().sourceBytesRead;
        Require(mapped.Read(0, 17) == text.substr(0, 17)); Require(mapped.Statistics().sourceBytesRead - before == 17);
        auto original = mapped.Capture(); Verify(mapped, text);
        mapped.Save(path, false); Require(large::Document::Read(original, 0, text.size()) == text);
        auto fragmented = large::Document::FromUtf8(std::make_shared<const std::string>(""));
        std::string expected;
        for (int i = 0; i < 4096; ++i) { const auto part = std::string(16, static_cast<char>('a' + i % 26)); fragmented->Replace(fragmented->Size(), 0, part); expected += part; }
        Verify(*fragmented, expected);
        auto mixed = large::Document::FromUtf8(std::make_shared<const std::string>(expected));
        for (int i = 0; i < 1024; ++i) { mixed->Replace(i * 32, 8, "CHANGED!"); expected.replace(i * 32, 8, "CHANGED!"); }
        Verify(*mixed, expected);
#ifndef MULTIPAD4_PIECE_READ_BASELINE
        large::backingReads = large::copiedBytes = 0;
        Require(fragmented->Read(0, fragmented->Size()).size() == fragmented->Size());
        Require(large::backingReads == 2 && large::copiedBytes == fragmented->Size());
        // Large contiguous mapped runs stay within the native mapping/I/O window.
        const auto largePath = path.wstring() + L".large";
        const std::string largeText(80 * 1024 * 1024, 'z'); files::WriteAtomic(largePath, {largeText});
        {
            auto backing = std::make_shared<files::MappedFile>(largePath, nullptr, true);
            large::Document largeDocument(backing, files::Encoding::Utf8);
            largeDocument.Replace(40 * 1024 * 1024, 0, "temporary"); largeDocument.Replace(40 * 1024 * 1024, 9, "");
            for (bool independent : {false, true}) {
                large::largestMappedVisit = large::copiedBytes = 0;
                large::Document::ReadSession session(largeDocument.Capture());
                const auto bytes = independent ? session.Read(0, largeText.size()) : largeDocument.Read(0, largeText.size());
                Require(bytes == largeText && large::largestMappedVisit == 16 * 1024 * 1024);
                Require(large::copiedBytes == largeText.size());
            }
        }
        std::filesystem::remove(largePath);
#endif
        if (argc > 1) {
            auto run = [&](const char* name, large::Document& document, const std::string& expected) {
                const auto snapshot = document.Capture(); large::Document::ReadSession independent(snapshot);
                for (bool session : {false, true}) {
                    std::vector<double> times; large::Position copied = 0, reads = 0;
                    for (int i = 0; i < 21; ++i) {
                        large::copiedBytes = 0; large::backingReads = 0;
                        const auto start = std::chrono::steady_clock::now();
                        const auto result = session ? independent.Read(0, expected.size()) : large::Document::Read(snapshot, 0, expected.size());
                        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                        Require(result == expected); copied += large::copiedBytes; reads += large::backingReads;
                    }
                    std::sort(times.begin(), times.end());
                    printf("%s session=%d bytes=%zu median=%.4f p95=%.4f max=%.4f copied/sample=%llu backingreads/sample=%llu\n", name, session, expected.size(), times[10], times[19], times[20], copied / 21, reads / 21);
                }
            };
            auto contiguous = large::Document::FromUtf8(std::make_shared<const std::string>(std::string(4 * 1024 * 1024, 'x')));
            run("contiguous-memory", *contiguous, std::string(4 * 1024 * 1024, 'x'));
            // Verify adds a prefix; retain its exact current serialized state for each case.
            run("adjacent-journal", *fragmented, large::Document::Read(fragmented->Capture(), 0, static_cast<size_t>(fragmented->Size())));
            run("mixed-fragments", *mixed, "later" + expected);
            auto current = mapped.Read(0, static_cast<size_t>(mapped.Size())); run("mapped", mapped, current);
        }
        std::filesystem::remove(path); puts("Piece read shared/independent, ranges, fragments, old snapshots and concurrent readers passed."); return 0;
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); std::error_code ignored; std::filesystem::remove(path, ignored); return 1; }
}

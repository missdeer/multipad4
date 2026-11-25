#define MULTIPAD4_REGEX_ITERATOR_TEST
#include "../src/LazySearch.cpp"
#include <source_location>
#include <chrono>
#include <cstdio>

void Require(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Regex iterator regression line " + std::to_string(where.line()));
}
std::shared_ptr<large::Document> Native(std::string text, files::Encoding encoding) {
    auto document = large::Document::FromUtf8(std::make_shared<const std::string>(""));
    document->Replace(0, 0, files::Encode({text, encoding})); return document;
}
void Verify(files::Encoding encoding, const std::string& text) {
    auto document = Native(text, encoding);
    std::atomic<bool> cancel{false}; large::Reader reader(document->Capture(), encoding, &cancel);
    large::Iterator first{&reader, 0}, cursor = first;
    const auto wide = files::FromUtf8(text);
    for (size_t i = 0; i < wide.size(); ++i) {
        Require(*cursor == wide[i]); auto copy = cursor; ++cursor; Require(*copy == wide[i]);
    }
    Require(cursor == large::Iterator{&reader, document->Size()});
    for (size_t i = wide.size(); i; --i) { --cursor; Require(*cursor == wide[i - 1]); }
    Require(cursor == first);
    // Value-owned cached characters survive all byte cache slots being evicted.
    auto copy = first; Require(*copy == wide[0]);
    auto largeText = std::string(6 * 65536, 'x');
    auto big = Native(largeText, encoding); large::Reader bigReader(big->Capture(), encoding, &cancel);
    large::Iterator retained{&bigReader, 0}; Require(*retained == L'x');
    for (large::Position i = 0; i < big->Size(); i += 65536) bigReader.At(i);
    Require(*retained == L'x'); ++retained; Require(*retained == L'x'); --retained; Require(*retained == L'x');
    cancel = true; bool stopped = false;
    try { for (int i = 0; i < 4096; ++i) (void)*copy; } catch (const std::runtime_error&) { stopped = true; }
    Require(stopped);
    large::SearchOptions options{L"(word)([0-9]+)", true, true};
    auto captures = Native("prefix word123 suffix", encoding);
    large::Search search(captures->Capture(), encoding, options);
    auto hit = search.Next(0, captures->Size()); Require(hit && hit->captures.size() == 3);
    Require(search.Utf8(hit->captures[1].start, hit->captures[1].end - hit->captures[1].start) == "word");
    Require(search.Utf8(hit->captures[2].start, hit->captures[2].end - hit->captures[2].start) == "123");
    Require(search.Next(captures->Size(), 0, true)->start == hit->start);
    large::Search backtracking(captures->Capture(), encoding, {L"(word|wor)([0-9]+)(?= suffix)", true, true});
    auto alternate = backtracking.Next(0, captures->Size()); Require(alternate && alternate->start == hit->start && alternate->end == hit->end);
    auto spanning = Native(std::string(5 * 65536, 'x') + "word123 suffix", encoding);
    large::Search acrossBlocks(spanning->Capture(), encoding, {L"(word)([0-9]+)(?= suffix)", true, true});
    auto tail = acrossBlocks.Next(0, spanning->Size()); Require(tail && acrossBlocks.Utf8(tail->start, tail->end - tail->start) == "word123");
}
int main(int argc, char**) {
    try {
        for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain}) Verify(encoding, "中😀 word123\r\nline");
        for (auto page : {932u, 54936u}) if (IsValidCodePage(page)) Verify(files::FromCodePage(page), page == 932 ? "ソ word123\r\nline" : "中😀 word123\r\nline");
        // Invalid bytes are deferred until reached, even after a successful early match.
        for (auto page : {65001u, 932u, 54936u}) {
            if (!IsValidCodePage(page)) continue;
            const auto encoding = files::FromCodePage(page);
            auto document = Native("hit ", encoding); document->Replace(document->Size(), 0, page == 65001 ? "\xFF" : "\x81");
            large::Search search(document->Capture(), encoding, {L"(hit)", true, true});
            Require(search.Next(0, document->Size()).has_value());
            bool rejected = false; try { search.Next(4, document->Size()); } catch (const std::runtime_error&) { rejected = true; }
            Require(rejected);
        }
        if (argc > 1) {
            std::vector<files::Encoding> benchmarkEncodings{files::Encoding::Utf8, files::Encoding::Utf16LEPlain};
            if (IsValidCodePage(932)) benchmarkEncodings.push_back(files::FromCodePage(932));
            for (auto encoding : benchmarkEncodings) {
                std::string text;
                const std::string prefix = files::CodePage(encoding) == 932 ? "ソ " : "中😀 ";
                for (int i = 0; i < 4096; ++i) text += prefix + "word123 alpha beta\n";
                auto document = Native(text, encoding);
                large::Search search(document->Capture(), encoding, {L"(word)([0-9]+)|never_match", true, true, false, false, false, 1});
                std::vector<double> samples; unsigned long long decoded = 0;
                for (int run = 0; run < 11; ++run) {
                    large::decodedCharacters = 0; size_t hits = 0; large::Position at = 0;
                    const auto start = std::chrono::steady_clock::now();
                    while (auto hit = search.Next(at, document->Size())) { Require(hit->end > at); at = hit->end; ++hits; }
                    samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                    Require(hits == 4096); decoded += large::decodedCharacters;
                }
                std::sort(samples.begin(), samples.end());
                printf("encoding=%u regex hits=4096 bytes=%llu samples=11 median=%.3f p95/max=%.3f decoded/sample=%llu\n", static_cast<unsigned>(encoding), document->Size(), samples[5], samples[10], decoded / 11);
            }
        }
        puts("Regex iterator captures, reverse traversal, eviction, deferred errors and cancellation passed."); return 0;
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
}

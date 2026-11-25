#include "LazySearch.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <source_location>
static void Require(bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("Reverse search regression line " + std::to_string(where.line())); }
// Independent original algorithm: literals advance one character to preserve overlaps.
static std::optional<large::SearchHit> Reference(large::Search& search, const large::SearchOptions& options, large::Position start, large::Position lower) {
    std::optional<large::SearchHit> last;
    auto position = lower;
    while (position <= start) {
        auto match = search.Next(position, start); if (!match) break; last = match;
        auto next = options.regex ? match->end : match->start;
        if (next == match->start) { if (next == start) break; next = search.Advance(next); }
        if (next <= position) break; position = next;
    }
    return last;
}
static void Equal(const std::optional<large::SearchHit>& a, const std::optional<large::SearchHit>& b) {
    Require(bool(a) == bool(b)); if (!a) return;
    Require(a->start == b->start && a->end == b->end && a->captures.size() == b->captures.size());
    for (size_t i = 0; i < a->captures.size(); ++i) Require(a->captures[i].start == b->captures[i].start && a->captures[i].end == b->captures[i].end && a->captures[i].matched == b->captures[i].matched);
}
int main(int argc, char** argv) {
    const auto dir = std::filesystem::temp_directory_path() / (L"multipad-reverse-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir); const auto path = dir / L"text.txt";
    try {
        if (argc > 1) {
            const bool baseline = std::string(argv[1]) == "baseline";
            const std::string kind = argc > 2 ? argv[2] : "literal";
            const bool regex = kind != "literal";
            std::string text; while (text.size() < 1024 * 1024) text += "Needle aa word\n";
            files::WriteAtomic(path, {text}); auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, files::Encoding::Utf8);
            large::SearchOptions options{kind == "regex-complex" ? L"(Needle)" : regex ? L"Needle" : L"needle", regex, regex}; options.workers = 1;
            large::Search retained(document.Capture(), files::Encoding::Utf8, options);
            FILETIME created{}, exited{}, kernelStart{}, userStart{}, kernelEnd{}, userEnd{};
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelStart, &userStart);
            std::vector<double> clicks;
            auto begin = std::chrono::steady_clock::now(); auto position = document.Size(); unsigned long long hash = 0;
            for (int i = 0; i < 100; ++i) {
                auto click = std::chrono::steady_clock::now();
                std::optional<large::SearchHit> hit;
                if (baseline) { large::Search fresh(document.Capture(), files::Encoding::Utf8, options); hit = Reference(fresh, options, position, 0); }
                else hit = retained.Next(position, 0, true);
                clicks.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-click).count());
                Require(hit.has_value()); position = hit->start; hash = hash * 131 + hit->start;
            }
            const auto elapsed = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelEnd, &userEnd);
            auto ticks = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
            const auto cpu = (ticks(kernelEnd) + ticks(userEnd) - ticks(kernelStart) - ticks(userStart)) / 10000.0;
            std::sort(clicks.begin(), clicks.end());
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            printf("%s,%s,100_previous_ms=%.3f,click_median_ms=%.3f,click_p95_ms=%.3f,click_p99_ms=%.3f,click_max_ms=%.3f,cpu_ms=%.3f,source_bytes_read=%llu,peak_private=%zu,hash=%llu\n",argv[1],kind.c_str(),elapsed,clicks[49],clicks[94],clicks[98],clicks[99],cpu,document.Statistics().sourceBytesRead,memory.PeakPagefileUsage,hash);
        } else {
            for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain, files::FromCodePage(936), files::Latin1}) {
                std::string text; for (int i = 0; i < 180; ++i) text += "aaa Alpha 12\r\nalpha 34 word_word z\n";
                if (encoding != files::Latin1) text += "中文中文";
                files::WriteAtomic(path, {text, encoding});
                auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, encoding);
                for (auto options : {large::SearchOptions{L"aa", false, false}, {L"alpha", false, false, true}, {L"word", false, false, false, true}, {L"missing", false, false}, {L"aa", true, true}, {L"Alpha", true, true}, {L"alpha", true, false}, {L"(alpha) ([0-9]+)", true, false}, {L"^alpha|z$", true, false}, {L"(?=a)|$", true, true}, {L"a.*a", true, false}, {L"a(?=lpha)|(?<!a)z", true, false}}) {
                    options.workers = 1; large::Search retained(document.Capture(), encoding, options);
                    auto position = document.Size(); const auto lower = retained.Advance(retained.Advance(0));
                    for (int i = 0; i < 100; ++i) {
                        large::Search fresh(document.Capture(), encoding, options);
                        auto expected = Reference(fresh, options, position, i < 80 ? 0 : lower);
                        auto actual = retained.Next(position, i < 80 ? 0 : lower, true); Equal(actual, expected);
                        if (actual) position = actual->start == actual->end && actual->start ? retained.Advance(actual->start, true) : actual->start;
                        else position = document.Size();
                    }
                    std::atomic<bool> stop{true}; bool cancelled = false;
                    try { large::Search interrupted(document.Capture(), encoding, options, &stop); interrupted.Next(document.Size(), 0, true); } catch (...) { cancelled = true; } Require(cancelled);
                }
                file.reset();
            }
            // Encoded characters and overlapping literals cross reverse block boundaries.
            for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain, files::FromCodePage(936)}) {
                auto encoded = [&](const char* value) { return files::Encode({value, encoding}); };
                const auto width = encoding == files::Encoding::Utf16LEPlain || encoding == files::Encoding::Utf16BEPlain ? 2u : 1u;
                files::WriteAtomic(path, {std::string(2 * 1024 * 1024 / width, 'x'), encoding});
                auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, encoding);
                document.Replace(1024*1024 - width, 10*width, encoded("中文Alpha aaa"));
                for (auto pattern : {L"中文", L"alpha", L"aa", L"missing"}) {
                    large::SearchOptions options{pattern, false, false}; options.workers = 1;
                    large::Search retained(document.Capture(), encoding, options), fresh(document.Capture(), encoding, options);
                    Equal(retained.Next(document.Size(), 0, true), Reference(fresh, options, document.Size(), 0));
                }
            }
            // Reusing a block near its beginning must reload before stepping into the prior block.
            for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain, files::FromCodePage(54936)}) {
                std::string filler = files::Encode({"😀中", encoding}), bytes;
                while (bytes.size() < 2*1024*1024) bytes += filler;
                files::WriteAtomicChunks(path, [&](const files::ChunkConsumer& consume) { consume(bytes); });
                auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, encoding);
                auto text = files::Encode({"ab", encoding});
                large::Search align(document.Capture(), encoding, {L"missing"});
                auto nearOffset = document.Size() - 1024*1024;
                while (nearOffset && align.Advance(align.Advance(nearOffset, true)) != nearOffset) nearOffset -= encoding == files::Encoding::Utf16LEPlain || encoding == files::Encoding::Utf16BEPlain ? 2 : 1;
                nearOffset = align.Advance(align.Advance(nearOffset));
                auto before = align.Advance(align.Advance(nearOffset, true), true);
                document.Replace(before, 0, text); nearOffset += text.size();
                document.Replace(nearOffset, 0, text);
                large::SearchOptions options{L"ab", false, false}; options.workers = 1;
                large::Search retained(document.Capture(), encoding, options);
                auto position = document.Size();
                for (int i = 0; i < 3; ++i) {
                    large::Search fresh(document.Capture(), encoding, options);
                    auto expected = Reference(fresh, options, position, 0), actual = retained.Next(position, 0, true); Equal(actual, expected);
                    if (!actual) break; position = actual->start;
                }
                large::Search mixed(document.Capture(), encoding, options), fresh(document.Capture(), encoding, options);
                auto forward = mixed.Next(nearOffset, document.Size()); Require(forward.has_value());
                Equal(mixed.Next(forward->start, 0, true), Reference(fresh, options, forward->start, 0));
            }
            // Every overlap endpoint must remain outside encoded characters on a complete miss.
            for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain, files::FromCodePage(936), files::FromCodePage(932), files::FromCodePage(54936)}) {
                const auto character = files::Encode({encoding == files::Encoding::Utf8 || encoding == files::Encoding::Utf16LEPlain || encoding == files::Encoding::Utf16BEPlain || encoding == files::FromCodePage(54936) ? "😀中" : "中", encoding});
                std::string encoded; while (encoded.size() < 2*1024*1024) encoded += character;
                files::WriteAtomicChunks(path, [&](const files::ChunkConsumer& consume) { consume(encoded); });
                auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, encoding);
                large::SearchOptions options{L"ab", false, false}; options.workers = 1;
                large::Search retained(document.Capture(), encoding, options), fresh(document.Capture(), encoding, options);
                Equal(retained.Next(document.Size(), 0, true), Reference(fresh, options, document.Size(), 0));
                for (bool matchCase : {false, true}) {
                    large::SearchOptions huge{std::wstring(262141, L'a'), false, matchCase}; huge.workers = 1;
                    large::Search search(document.Capture(), encoding, huge);
                    Require(!search.Next(document.Size(), 0, true));
                }
            }
            {
                const std::string text = "x" + std::string(300*1024, 'a') + "Y";
                auto source = large::Document::FromUtf8(std::make_shared<const std::string>(text));
                large::SearchOptions options{std::wstring(300*1024, L'a'), false, true}; options.workers = 1;
                large::Search retained(source->Capture(), files::Encoding::Utf8, options), fresh(source->Capture(), files::Encoding::Utf8, options);
                Equal(retained.Next(source->Size(), 0, true), Reference(fresh, options, source->Size(), 0));
            }
            puts("100 Previous operations, overlaps, bounds, encodings and captures passed.");
        }
        std::filesystem::remove_all(dir); return 0;
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); std::error_code ignored; std::filesystem::remove_all(dir, ignored); return 1; }
}

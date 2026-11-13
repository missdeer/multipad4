#include "LazyDocument.h"
#include "LazySearch.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <random>
#include <algorithm>
#include <source_location>
#include <stdexcept>
#include <vector>
void Check(bool value, std::source_location where = std::source_location::current()) { if (!value) throw std::runtime_error("Lazy document regression at line " + std::to_string(where.line())); }
std::vector<size_t> Lines(std::string_view text) {
    std::vector<size_t> lines{0};
    for (size_t i = 0; i < text.size(); ++i) { if (text[i] == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') ++i; lines.push_back(i + 1); } else if (text[i] == '\n') lines.push_back(i + 1); }
    return lines;
}
void Verify(large::Document& document, const std::string& text) {
    Check(document.Size() == text.size() && document.Read(0, text.size()) == text);
    const auto lines = Lines(text); auto snapshot = document.Capture();
    Check(large::Document::LineCount(snapshot) == lines.size());
    for (size_t line = 0; line < lines.size(); ++line) Check(large::Document::PositionFromLine(snapshot, line) == lines[line]);
    for (size_t offset = 0; offset <= text.size(); ++offset) {
        auto expected = static_cast<size_t>(std::upper_bound(lines.begin(), lines.end(), offset) - lines.begin() - 1);
        Check(large::Document::LineFromPosition(snapshot, offset) == expected);
    }
}
int main() {
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-lazy-" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(directory); auto path = directory / L"input.txt";
        files::WriteAtomic(path, {"first\r\nsecond\rlast\n"});
        {
            auto file = std::make_shared<files::MappedFile>(path, nullptr, true);
            large::Document document(file, files::Encoding::Utf8);
            Check(document.Statistics().sourceBytesRead == 0 && document.Statistics().indexedBlocks == 0);
            std::string text = "first\r\nsecond\rlast\n"; Verify(document, text);
            auto original = document.Capture();
            std::mt19937 random(1234567);
            std::vector<std::string> history{text};
            for (int edit = 0; edit < 250; ++edit) {
                auto offset = random() % (text.size() + 1), count = random() % ((std::min)(size_t(8), text.size() - offset) + 1);
                std::string addition; for (unsigned i = 0, n = random() % 7; i < n; ++i) addition += "abc\r\n"[random() % 5];
                if (!count && addition.empty()) continue;
                document.Replace(offset, count, addition); text.replace(offset, count, addition); history.push_back(text); Verify(document, text);
            }
            for (size_t i = history.size() - 1; i > 0; --i) { Check(document.Undo().has_value()); Verify(document, history[i - 1]); }
            Check(!document.Modified() && !document.CanUndo());
            for (size_t i = 1; i < history.size(); ++i) { Check(document.Redo().has_value()); Verify(document, history[i]); }
            Check(large::Document::Read(original, 0, 64) == history.front());
            document.Save(path, false); Check(!document.Modified() && files::Read(path).utf8 == text);
            Check(document.Undo().has_value() && document.Modified()); document.Save(directory / L"undo.txt", true);
            Check(files::Read(directory / L"undo.txt").utf8 == history[history.size() - 2]);
            Check(files::Read(path).utf8 == text);
        }
        for (auto encoding : {files::Encoding::Utf8Bom, files::Encoding::Utf16LE, files::Encoding::Utf16BE}) {
            const std::string text = "one\r\ntwo\nlast"; files::WriteAtomic(path, {text, encoding});
            auto file = std::make_shared<files::MappedFile>(path, nullptr, true);
            auto signature = encoding == files::Encoding::Utf8Bom ? std::string("\xEF\xBB\xBF") : encoding == files::Encoding::Utf16LE ? std::string("\xFF\xFE") : std::string("\xFE\xFF");
            large::Document document(file, encoding, signature); auto snapshot = document.Capture();
            Check(large::Document::LineCount(snapshot) == 3);
            Check(large::Document::PositionFromLine(snapshot, 1) == (encoding == files::Encoding::Utf8Bom ? 5 : 10));
            Check(large::Document::PositionFromLine(snapshot, 2) == (encoding == files::Encoding::Utf8Bom ? 9 : 18));
            document.Save(path, false); Check(files::Read(path).utf8 == text);
        }
        for (auto encoding : {files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain}) {
            const auto text = files::Encode({"a\r\nb\nc", encoding}); files::WriteAtomic(path, {"unused"});
            { std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(text.data(), text.size()); }
            auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, encoding);
            auto inserted = files::Encode({"\r", encoding}); document.Replace(0, 0, inserted);
            Check(large::Document::LineCount(document.Capture()) == 4);
            Check(large::Document::PositionFromLine(document.Capture(), 1) == 2);
            Check(large::Document::LineFromPosition(document.Capture(), 2) == 1);
        }
        files::WriteAtomic(path, {std::string(65535, 'x') + "\r\nlast"});
        {
            auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, files::Encoding::Utf8);
            auto snapshot = document.Capture(); Check(large::Document::LineCount(snapshot) == 2 && large::Document::PositionFromLine(snapshot, 1) == 65537);
            const auto blocks = document.Statistics().indexedBlocks; document.Replace(0, 0, "a");
            Check(large::Document::LineCount(document.Capture()) == 2 && large::Document::PositionFromLine(document.Capture(), 1) == 65538);
            Check(document.Statistics().indexedBlocks <= blocks + 1);
            Check(document.Statistics().addedBytes == 1);
        }
        {
            const std::string content = std::string(65535, 'q') + "\r\n" + std::string(65536, 'a') + "\nlast";
            files::WriteAtomic(path, {content}); auto file = std::make_shared<files::MappedFile>(path, nullptr, true);
            large::Document document(file, files::Encoding::Utf8); std::string text = content; std::mt19937 random(987654);
            for (int edit = 0; edit < 60; ++edit) {
                auto offset = random() % (text.size() + 1), count = (std::min)(size_t(random() % 20), text.size() - offset);
                auto addition = edit % 3 == 0 ? std::string("\r\nXYZ") : std::string("a\n");
                document.Replace(offset, count, addition); text.replace(offset, count, addition);
                auto lines = Lines(text); auto snapshot = document.Capture();
                Check(large::Document::LineCount(snapshot) == lines.size());
                for (size_t line = 0; line < lines.size(); ++line) Check(large::Document::PositionFromLine(snapshot, line) == lines[line]);
            }
            std::atomic<bool> cancel{true}; bool cancelled = false;
            try { large::Document::LineCount(document.Capture(), &cancel); } catch (const std::runtime_error&) { cancelled = true; }
            Check(cancelled); Check(document.Read(0, text.size()) == text);
            auto saved = document.Capture();
            document.Begin({5, 3}); document.Replace(1, 2, "first"); document.Replace(4, 1, "second"); document.End({7, 7});
            Check(document.Undo()->caret == 5 && document.Capture().root == saved.root);
            document.Begin({0, 0}); document.Replace(0, 5, "failed"); document.Abort(); Check(document.Capture().root == saved.root);
            const auto beforeSave = document.Capture(); bool failed = false;
            try { document.Save(directory / L"missing-folder" / L"no.txt", false); } catch (const std::runtime_error&) { failed = true; }
            Check(failed && document.Modified() && document.Capture().root == beforeSave.root);
            auto found = large::Document::Find(document.Capture(), "XYZ", 0, document.Size(), false);
            auto expected = text.find("XYZ"); Check(found.has_value() == (expected != std::string::npos));
            if (found) Check(found->position == expected);
            found = large::Document::Find(document.Capture(), "XYZ", document.Size(), 0, true); expected = text.rfind("XYZ");
            Check(found.has_value() == (expected != std::string::npos)); if (found) Check(found->position == expected);
        }
        {
            const std::string text = "Alpha 12\r\nalpha 34\nend"; files::WriteAtomic(path, {text});
            auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, files::Encoding::Utf8);
            large::Search search(document.Capture(), files::Encoding::Utf8, {L"(alpha) ([0-9]+)", true, false});
            auto match = search.Next(0, document.Size()); Check(match && match->start == 0 && match->end == 8);
            Check(search.Substitute(*match, "$2-\\1", true) == "12-Alpha");
            match = search.Next(match->end, document.Size()); Check(match && match->start == 10 && match->end == 18);
            auto reverse = search.Next(document.Size(), 0, true); Check(reverse && reverse->start == 10);
            large::Search multiline(document.Capture(), files::Encoding::Utf8, {L"^alpha", true, false});
            match = multiline.Next(1, document.Size()); Check(match && match->start == 10);
            large::Search cross(document.Capture(), files::Encoding::Utf8, {L"12\\r?\\nalpha", true, true});
            match = cross.Next(0, document.Size()); Check(match && match->start == 6 && match->end == 15);
            auto branch = document.Branch(document.Capture()); branch->Begin({}); branch->Replace(0, 5, "new"); branch->End({});
            Check(document.Read(0, 5) == "Alpha"); auto before = document.Capture(); document.Commit(before, branch->Capture(), {}, {});
            Check(document.Read(0, 3) == "new" && document.Modified()); Check(document.Undo().has_value() && document.Capture().root == before.root);
            bool invalid = false; try { large::Search bad(document.Capture(), files::Encoding::Utf8, {L"[", true}); } catch (...) { invalid = true; } Check(invalid);
        }
        // Real parallel ranges, boundary ownership, greedy non-overlap and
        // read sessions over an edited snapshot whose original path is replaced.
        for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain}) {
            const auto width = encoding == files::Encoding::Utf8 ? 1u : 2u;
            const size_t boundary = 64 * 1024 * 1024;
            files::WriteAtomic(path, {std::string((boundary + 2 * 1024 * 1024) / width, 'x'), encoding});
            auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, encoding);
            auto raw = [&](const char* text) { return files::Encode({text, encoding}); };
            document.Replace(boundary - 5 * width, 40 * width, raw("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
            document.Replace(boundary + 256 * 1024 - 4 * width, 12 * width, raw("NeedleMarker"));
            document.Replace(2 * 1024 * 1024, 12 * width, raw("NeedleMarker"));
            document.Replace(boundary + 1024 * 1024, 12 * width, raw("NeedleMarker"));
            const auto original = document.Capture();
            for (bool matchCase : {false, true}) for (auto pattern : {L"aaa", L"NeedleMarker", L"missing"}) {
                large::SearchOptions options{pattern, false, matchCase}; options.workers = 1;
                large::Search serial(original, encoding, options); options.workers = 8;
                large::Search parallel(original, encoding, options);
                auto expected = serial.Next(0, document.Size()), actual = parallel.Next(0, document.Size());
                Check(bool(expected) == bool(actual)); if (expected) Check(expected->start == actual->start && expected->end == actual->end);
                expected = serial.Next(document.Size(), 0, true); actual = parallel.Next(document.Size(), 0, true);
                Check(bool(expected) == bool(actual)); if (expected) Check(expected->start == actual->start && expected->end == actual->end);
                large::HitArchive baseline, results, count(false);
                serial.Enumerate(0, document.Size(), baseline); parallel.Enumerate(0, document.Size(), results); parallel.Enumerate(0, document.Size(), count);
                Check(baseline.Count() == results.Count() && baseline.Count() == count.Count());
                auto left = baseline.Page(0, baseline.Count()), right = results.Page(0, results.Count());
                for (size_t i = 0; i < left.size(); ++i) Check(left[i].start == right[i].start && left[i].end == right[i].end);
            }
            // Multi-unit characters cross both the 64 MiB boundary and the
            // find-next probe-shifted boundary. Never decode a partial halo.
            const auto unicode = raw("中文😀");
            document.Replace(boundary - width, width, unicode);
            document.Replace(boundary + 256 * 1024 - width, width, unicode);
            {
                large::SearchOptions options{L"中文😀", false, false}; options.workers = 1;
                large::Search serial(document.Capture(), encoding, options); options.workers = 8;
                large::Search parallel(document.Capture(), encoding, options);
                for (bool reverse : {false, true}) {
                    const auto from = reverse ? document.Size() : 0, to = reverse ? 0 : document.Size();
                    auto expected = serial.Next(from, to, reverse), actual = parallel.Next(from, to, reverse);
                    Check(expected && actual && expected->start == actual->start && expected->end == actual->end);
                }
                large::HitArchive baseline, results;
                serial.Enumerate(0, document.Size(), baseline); parallel.Enumerate(0, document.Size(), results);
                Check(baseline.Count() == 2 && results.Count() == 2);
                auto a = baseline.Page(0, 2), b = results.Page(0, 2);
                Check(a[0].start == b[0].start && a[1].start == b[1].start);
            }
            large::Document::ReadSession reader(original);
            const auto previous = reader.Read(0, 32);
            document.Replace(0, width, raw("Y")); document.Save(path, false);
            large::Document::ReadSession retained(original);
            Check(retained.Read(0, 32) == previous && reader.Read(0, 32) == previous);
            large::Search preserved(original, encoding, {L"NeedleMarker", false, true});
            auto preservedHit = preserved.Next(0, large::Document::Size(original));
            Check(preservedHit && preservedHit->start == 2 * 1024 * 1024);
            std::atomic<bool> stop{true}; bool cancelled = false;
            try { large::Search interrupted(original, encoding, {L"missing", false, true}, &stop); interrupted.Next(0, large::Document::Size(original)); } catch (const std::runtime_error&) { cancelled = true; }
            Check(cancelled);
        }
        std::filesystem::remove_all(directory); std::cout << "Lazy piece tree, persistent undo, incremental line blocks, encodings and atomic save passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; std::error_code ignored; std::filesystem::remove_all(directory, ignored); return 1; }
}

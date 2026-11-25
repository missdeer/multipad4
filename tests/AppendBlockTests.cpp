#define MULTIPAD4_APPEND_TEST
#include "../src/LazyDocument.cpp"
#include "LazySearch.h"
#include <future>
#include <chrono>
#include <source_location>
#include <cstdio>
#include <psapi.h>
void Require(bool value, std::source_location where = std::source_location::current()) { if (!value) throw std::runtime_error("Append regression line " + std::to_string(where.line())); }
SIZE_T PrivateBytes() { PROCESS_MEMORY_COUNTERS_EX data{}; data.cb = sizeof(data); GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&data), sizeof(data)); return data.PrivateUsage; }
int main(int argc, char**) {
    const auto path = std::filesystem::temp_directory_path() / (L"MultiPad4-append-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    try {
        auto document = large::Document::FromUtf8(std::make_shared<const std::string>(""));
        std::vector<large::Document::Snapshot> retained;
        const auto memory = PrivateBytes(); const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 100000; ++i) {
            document->Replace(document->Size(), 0, "x");
            if (i % 10000 == 0) retained.push_back(document->Capture());
        }
        const auto editMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        const auto undoMemory = PrivateBytes() - memory;
        const std::string expected(100000, 'x'); Require(document->Read(0, 100000) == expected);
        const auto nodes = document->Statistics().allocatedNodes;
        const auto writes = large::journalWrites;
#ifndef MULTIPAD4_APPEND_BASELINE
        // Failed partial flush retains published pending bytes and can be retried.
        {
            auto failing = large::Document::FromUtf8(std::make_shared<const std::string>(""));
            failing->Replace(0, 0, std::string(65535, 'a'));
            const auto old = failing->Capture(); large::failJournalAfter = 13;
            bool rejected = false; try { failing->Replace(failing->Size(), 0, "zz"); } catch (const std::runtime_error&) { rejected = true; }
            large::failJournalAfter = UINT64_MAX;
            Require(rejected && failing->Read(0, 65535) == std::string(65535, 'a'));
            Require(large::Document::Read(old, 0, 65535) == std::string(65535, 'a'));
            failing->Replace(failing->Size(), 0, "zz"); Require(failing->Read(65534, 3) == "azz");
        }
#endif
        auto original = document->Capture();
        auto concurrent = std::async(std::launch::async, [original] {
            large::Document::ReadSession session(original);
            large::Search search(original, files::Encoding::Utf8, {L"x{10}", true, true});
            for (int i = 0; i < 20; ++i) { Require(session.Read(0, 100000) == std::string(100000, 'x')); Require(search.Next(0, 100000).has_value()); }
        });
        document->Begin({0, 0}); document->Replace(0, 0, "aborted"); const auto aborted = document->Capture(); document->Abort();
        Require(document->Read(0, 100000) == expected); Require(large::Document::Read(aborted, 0, 7) == "aborted");
        for (int i = 0; i < 1000; ++i) Require(document->Undo().has_value());
        Require(document->Size() == 99000);
        for (int i = 0; i < 1000; ++i) Require(document->Redo().has_value());
        concurrent.get(); Require(document->Read(0, 100000) == expected);
        for (size_t i = 0; i < retained.size(); ++i) Require(large::Document::Read(retained[i], 0, 100000) == std::string(i * 10000 + 1, 'x'));
        files::WriteAtomic(path, {"original destination"});
        bool failed = false;
        try { files::WriteAtomicChunks(path, [&](const files::ChunkConsumer& consume) {
            document->Write([&](std::string_view bytes) { if (!bytes.empty()) { consume(bytes); throw std::runtime_error("Injected producer failure after partial output"); } });
        }); } catch (const std::runtime_error&) { failed = true; }
        Require(failed && files::Read(path).utf8 == "original destination" && document->Modified());
        const auto saveStart = std::chrono::steady_clock::now(); document->Save(path, true);
        const auto saveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - saveStart).count();
        Require(files::Read(path).utf8 == expected);
        size_t chunks = 0; std::string output;
        document->Write([&](std::string_view bytes) { if (!bytes.empty()) { ++chunks; output += bytes; } }); Require(output == expected);
        if (argc > 1) printf("100000 edits ms=%.3f journalWrites=%llu allocatedNodes=%llu undoPrivateDelta=%zu saveMs=%.3f outputChunks=%zu\n", editMs, writes, nodes, undoMemory, saveMs, chunks);
        std::filesystem::remove(path); puts("Append 100k edits, old roots, undo/redo, abort, concurrent search and partial-save retention passed."); return 0;
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); std::error_code ignored; std::filesystem::remove(path, ignored); return 1; }
}

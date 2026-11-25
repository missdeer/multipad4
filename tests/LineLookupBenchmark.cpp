#define MULTIPAD4_LINE_BENCHMARK
namespace large { bool lineLookupBaseline = false; }
#include "../src/LazyDocument.cpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <random>
int main(int argc, char** argv) {
    try {
        const auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-lines-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory); const auto path = directory / L"lines.txt";
        std::string text; for (int i = 0; i < 400000; ++i) text += "some line contents\r\n";
        files::WriteAtomic(path, {text});
        std::ofstream out(argc > 1 ? argv[1] : "line-lookup.csv"); out << "variant,state,run,elapsed_ms,source_bytes_read,indexed_blocks,checksum\n";
        for (bool baseline : {true, false}) for (bool warm : {false, true}) for (int run = 0; run < 5; ++run) {
            auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document document(file, files::Encoding::Utf8);
            auto snapshot = document.Capture(); large::lineLookupBaseline = baseline;
            if (warm) large::Document::LineCount(snapshot);
            auto before = document.Statistics(); const auto begin = std::chrono::steady_clock::now(); uint64_t checksum = 0;
            std::mt19937 random(12345);
            for (int i = 0; i < 10000; ++i) {
                const auto line = i == 0 ? 1u : random() % 400001;
                const auto position = large::Document::PositionFromLine(snapshot, line);
                if (position != line * 20ull) throw std::runtime_error("Line lookup benchmark mismatch.");
                checksum += position;
                if (!i && !warm && document.Statistics().sourceBytesRead > 65536 + (baseline ? 1024 : 4)) throw std::runtime_error("Cold near-start read exceeded one block.");
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count(); auto after = document.Statistics();
            out << (baseline ? "before" : "after") << ',' << (warm ? "warm" : "cold") << ',' << run << ',' << elapsed << ',' << after.sourceBytesRead - before.sourceBytesRead << ',' << after.indexedBlocks - before.indexedBlocks << ',' << checksum << '\n'; out.flush();
        }
        std::filesystem::remove_all(directory); return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

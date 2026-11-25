#define MULTIPAD4_ARCHIVE_BENCHMARK
#include <cstddef>
#include <cstdlib>
#include <new>
thread_local bool countAllocations = false;
thread_local size_t allocationCount = 0, archiveReadCalls = 0, archiveReadBytes = 0;
void* operator new(size_t size) { if (countAllocations) ++allocationCount; if (auto* pointer = std::malloc(size ? size : 1)) return pointer; throw std::bad_alloc(); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }
#include "../src/LazySearch.cpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <random>
size_t BaselineLookup(const large::HitArchive& archive, large::Position position) {
    size_t low = 0, high = archive.Count();
    while (low < high) { auto middle = low + (high - low) / 2; if (archive.Page(middle, 1)[0].end < position) low = middle + 1; else high = middle; }
    return low;
}
int main(int argc, char** argv) {
    try {
        std::ofstream out(argc > 1 ? argv[1] : "hit-archive.csv"); out << "workload,variant,run,elapsed_ms,read_calls,read_bytes,allocations,checksum\n";
        for (int workload : {0, 1, 2, 3}) for (bool baseline : {true, false}) for (int run = 0; run < 5; ++run) {
            large::HitArchive archive; for (size_t i = 0; i < 200000; ++i) archive.Append({i * 2, i * 2 + 1, {}, i / 10, i / 10 * 20});
            large::HitArchive merged; std::mt19937 random(123);
            archiveReadCalls = archiveReadBytes = allocationCount = 0; countAllocations = true;
            const auto begin = std::chrono::steady_clock::now(); uint64_t checksum = 0;
            if (workload != 2) {
                for (size_t i = 0; i < 10000; ++i) {
                    const auto position = workload == 0 ? i * 2 : workload == 1 || i % 4 == 0 ? random() % 400000 : (i * 2) % 400000;
                    const auto found = baseline ? BaselineLookup(archive, position) : archive.FirstEndingAtOrAfter(position);
                    if (found != (position / 2)) throw std::runtime_error("Archive lookup benchmark mismatch."); checksum += found;
                }
            } else {
                if (baseline) for (size_t at = 0; at < archive.Count();) { auto hits = archive.Page(at, 4096); for (const auto& hit : hits) merged.Append(hit); at += hits.size(); }
                else merged.AppendArchive(archive);
                if (merged.Count() != archive.Count()) throw std::runtime_error("Archive merge count mismatch."); checksum = merged.Count();
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count(); countAllocations = false;
            out << workload << ',' << (baseline ? "before" : "after") << ',' << run << ',' << elapsed << ',' << archiveReadCalls << ',' << archiveReadBytes << ',' << allocationCount << ',' << checksum << '\n'; out.flush();
        }
        return 0;
    } catch (const std::exception& error) { countAllocations = false; std::cerr << error.what() << '\n'; return 1; }
}

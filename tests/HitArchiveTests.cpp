#include "LazySearch.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <source_location>
void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Hit archive regression at " + std::to_string(where.line()));
}
int main() {
    try {
        large::HitArchive archive; std::vector<large::SearchHit> expected;
        Check(archive.FirstEndingAtOrAfter(0) == 0 && archive.Page(0, 10).empty());
        auto append = [&](size_t begin, size_t count) {
            for (size_t i = begin; i < begin + count; ++i) {
                large::SearchHit hit{i / 5, i / 5, {}, i / 20, i / 20 * 4};
                expected.push_back(hit); archive.Append(hit);
            }
        };
        append(0, 4096); append(4096, 13);
        archive.Page(4095, 2); // Forces a partial flush after spilling.
        append(4109, 17); archive.Page(4108, 2); append(4126, 9000);
        std::mt19937 random(123);
        for (size_t i = 0; i < 10000; ++i) {
            const auto position = random() % 3000;
            const auto found = std::lower_bound(expected.begin(), expected.end(), position, [](const auto& hit, auto value) { return hit.end < value; });
            Check(archive.FirstEndingAtOrAfter(position) == static_cast<size_t>(found - expected.begin()));
            if (i % 17 == 0) archive.Page(random() % expected.size(), 1);
        }
        large::HitArchive merged;
        merged.Append({0, 0, {}, 0, 0}); merged.AppendArchive(archive);
        auto actual = merged.Page(0, merged.Count()); Check(actual.size() == expected.size() + 1);
        for (size_t i = 0; i < expected.size(); ++i) {
            Check(actual[i + 1].start == expected[i].start && actual[i + 1].end == expected[i].end && actual[i + 1].line == expected[i].line && actual[i + 1].lineStart == expected[i].lineStart);
        }
        large::HitArchive tail; tail.Append({10000, 10000, {}, 2, 7}); merged.AppendArchive(tail);
        Check(merged.FirstEndingAtOrAfter(10000) == expected.size() + 1);
        std::atomic<bool> cancel{true}; const auto count = merged.Count(); bool cancelled = false;
        try { merged.AppendArchive(archive, &cancel); } catch (const std::runtime_error&) { cancelled = true; }
        Check(cancelled && merged.Count() == count);
        std::cout << "Hit archive sparse pages, duplicates, partial flushes, tails and bulk transfer passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

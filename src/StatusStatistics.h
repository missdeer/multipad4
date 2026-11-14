#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
struct EditorDocument;
// UI-thread incremental statistics. No query scans an unbounded Scintilla range.
class StatusStatistics {
    struct Scan {
        intptr_t first, last, cursor, characters = 0, column = 0;
    };
    uint64_t revision = UINT64_MAX;
    intptr_t position = -1, line = -1, tab = 0;
    std::vector<intptr_t> selections;
    std::vector<Scan> scans;
public:
    intptr_t selectedBytes = 0, selectedLines = 0;
    bool Update(const EditorDocument& document);
    bool Ready(size_t i) const { return i < scans.size() && scans[i].cursor == scans[i].last; }
    intptr_t Characters(size_t i) const { return scans[i].characters; }
    intptr_t Column(size_t i) const { return scans[i].column; }
    intptr_t SelectedCharacters() const {
        intptr_t count = 0;
        for (size_t i = 2; i < scans.size(); ++i) count += scans[i].characters;
        return count;
    }
};

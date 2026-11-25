// Benchmark-only pre-optimization implementation from e876e48.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
struct EditorDocument;
// UI-thread incremental statistics. No query scans an unbounded Scintilla range.
class BaselineStatusStatistics {
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



#include <chrono>
inline bool BaselineStatusStatistics::Update(const EditorDocument& d) {
    auto pos = d.Send(SCI_GETCURRENTPOS), currentLine = d.Send(SCI_LINEFROMPOSITION, pos);
    auto currentTab = (std::max)(LRESULT(1), d.Send(SCI_GETTABWIDTH));
    std::vector<intptr_t> ranges;
    auto count = d.Send(SCI_GETSELECTIONS);
    ranges.reserve(static_cast<size_t>(count) * 2);
    for (LRESULT i = 0; i < count; ++i) {
        ranges.push_back(d.Send(SCI_GETSELECTIONNSTART, i));
        ranges.push_back(d.Send(SCI_GETSELECTIONNEND, i));
    }
    if (revision != d.style.revision || position != pos || line != currentLine || tab != currentTab || selections != ranges) {
        revision = d.style.revision; position = pos; line = currentLine; tab = currentTab;
        selections = std::move(ranges); scans.clear();
        auto first = d.Send(SCI_POSITIONFROMLINE, line), last = d.Send(SCI_GETLINEENDPOSITION, line);
        scans.push_back({first, pos, first}); scans.push_back({first, last, first});
        selectedBytes = selectedLines = 0;
        for (size_t i = 0; i < selections.size(); i += 2) {
            auto a = selections[i], b = selections[i + 1];
            scans.push_back({a, b, a}); selectedBytes += b - a;
            if (b > a) selectedLines += d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_POSITIONBEFORE, b)) - d.Send(SCI_LINEFROMPOSITION, a) + 1;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    bool pending = false;
    for (size_t index = 0; index < scans.size(); ++index) {
        auto& scan = scans[index];
        while (scan.cursor < scan.last && std::chrono::steady_clock::now() < deadline) {
            auto end = (std::min)(scan.last, scan.cursor + 16384);
            // Keep UTF-8 characters together, including a character at the chunk boundary.
            if (end < scan.last) end = d.Send(SCI_POSITIONBEFORE, end + 1);
            if (end <= scan.cursor) end = d.Send(SCI_POSITIONAFTER, scan.cursor);
            const auto bytes = d.Range(scan.cursor, end - scan.cursor);
            const auto measured = simd::MeasureUtf8(bytes, scan.column, index < 2 ? tab : 0);
            scan.characters += measured.characters;
            scan.column = measured.column;
            scan.cursor = end;
        }
        pending |= scan.cursor < scan.last;
    }
    return pending;
}

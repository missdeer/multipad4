#include "EditorDocument.h"
#include <chrono>
bool StatusStatistics::Update(const EditorDocument& d) {
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
    for (auto& scan : scans) {
        while (scan.cursor < scan.last && std::chrono::steady_clock::now() < deadline) {
            auto end = (std::min)(scan.last, scan.cursor + 16384);
            // Keep UTF-8 characters together, including a character at the chunk boundary.
            if (end < scan.last) end = d.Send(SCI_POSITIONBEFORE, end + 1);
            if (end <= scan.cursor) end = d.Send(SCI_POSITIONAFTER, scan.cursor);
            const auto bytes = d.Range(scan.cursor, end - scan.cursor);
            for (unsigned char c : bytes) {
                if ((c & 0xC0) == 0x80) continue;
                ++scan.characters;
                if (c == '\t') scan.column += tab - scan.column % tab;
                else if (c != '\r' && c != '\n') ++scan.column;
            }
            scan.cursor = end;
        }
        pending |= scan.cursor < scan.last;
    }
    return pending;
}

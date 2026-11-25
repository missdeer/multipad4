#include "EditorDocument.h"
#include "SimdText.h"
#include "ScintillaSelectionGeneration.h"
#include <chrono>
bool StatusStatistics::Update(const EditorDocument& d) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    const auto position = d.Send(SCI_GETCURRENTPOS), currentLine = d.Send(SCI_LINEFROMPOSITION, position);
    const auto currentTab = (std::max)(LRESULT(1), d.Send(SCI_GETTABWIDTH));
    const auto generation = static_cast<uint64_t>(d.Send(GetSelectionGeneration));
    const auto currentDocument = d.Send(SCI_GETDOCPOINTER);
    if (documentPointer != currentDocument || identity != d.searchIdentity || revision != d.style.revision || line != currentLine || tab != currentTab) {
        documentPointer = currentDocument;
        identity = d.searchIdentity; revision = d.style.revision; line = currentLine; tab = currentTab;
        lineEnd = d.Send(SCI_GETLINEENDPOSITION, line);
        checkpoints.clear(); checkpoints.push_back({d.Send(SCI_POSITIONFROMLINE, line), 0, 0});
        selectionGeneration = UINT64_MAX; prefixReady = false;
    }
    if (generation != selectionGeneration) {
        selectionGeneration = generation; selectionIndex = 0; selectionCount = d.Send(SCI_GETSELECTIONS);
        bytesPending = linesPending = charactersPending = 0; selectionActive = false; selectionsReady = false;
    }
    if (position != caret) { caret = position; prefixReady = false; }
    auto scan = [&](Checkpoint& checkpoint, intptr_t last, intptr_t tabWidth) {
        auto end = (std::min)(last, checkpoint.position + 16384);
        if (end < last) end = d.Send(SCI_POSITIONBEFORE, end + 1);
        if (end <= checkpoint.position) end = (std::min)(last, d.Send(SCI_POSITIONAFTER, checkpoint.position));
        const auto measured = simd::MeasureUtf8(d.Range(checkpoint.position, end - checkpoint.position), checkpoint.column, tabWidth);
        checkpoint = {end, checkpoint.characters + static_cast<intptr_t>(measured.characters), static_cast<intptr_t>(measured.column)};
    };
    bool lineTurn = true;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!prefixReady && (position <= checkpoints.back().position || checkpoints.back().position == lineEnd)) {
            auto point = std::upper_bound(checkpoints.begin(), checkpoints.end(), position,
                [](auto value, const auto& item) { return value < item.position; });
            prefix = *--point;
            if (prefix.position < position) scan(prefix, position, tab);
            prefixReady = true;
        }
        if (lineTurn && checkpoints.back().position < lineEnd) {
            auto next = checkpoints.back(); scan(next, lineEnd, tab); checkpoints.push_back(next);
        } else if (!selectionsReady) {
            if (!selectionActive && selectionIndex < selectionCount) {
                const auto start = d.Send(SCI_GETSELECTIONNSTART, selectionIndex);
                selectionEnd = d.Send(SCI_GETSELECTIONNEND, selectionIndex);
                selected = {start, 0, 0}; bytesPending += selectionEnd - start;
                if (selectionEnd > start) linesPending += d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_POSITIONBEFORE, selectionEnd)) - d.Send(SCI_LINEFROMPOSITION, start) + 1;
                selectionActive = true;
            }
            if (selectionActive) {
                if (selected.position < selectionEnd) scan(selected, selectionEnd, 0);
                if (selected.position == selectionEnd) { charactersPending += selected.characters; ++selectionIndex; selectionActive = false; }
            }
            if (selectionIndex == selectionCount) {
                selectedBytes = bytesPending; selectedLines = linesPending; charactersSelected = charactersPending; selectionsReady = true;
            }
        } else if (checkpoints.back().position == lineEnd && prefixReady) break;
        lineTurn = !lineTurn;
    }
    return !prefixReady || checkpoints.back().position != lineEnd || !selectionsReady;
}

#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
struct EditorDocument;
class StatusStatistics {
    struct Checkpoint { intptr_t position, characters, column; };
    uint64_t identity = 0, revision = UINT64_MAX, selectionGeneration = UINT64_MAX;
    intptr_t documentPointer = 0;
    intptr_t line = -1, tab = 0, lineEnd = 0, caret = -1;
    std::vector<Checkpoint> checkpoints;
    Checkpoint prefix{}, selected{};
    intptr_t selectionIndex = 0, selectionCount = 0, selectionEnd = 0;
    intptr_t bytesPending = 0, linesPending = 0, charactersPending = 0, charactersSelected = 0;
    bool prefixReady = false, selectionActive = false, selectionsReady = false;
public:
    intptr_t selectedBytes = 0, selectedLines = 0;
    bool Update(const EditorDocument& document);
    bool Ready(size_t i) const { return i == 0 ? prefixReady : !checkpoints.empty() && checkpoints.back().position == lineEnd; }
    bool SelectionReady() const { return selectionsReady; }
    intptr_t Characters(size_t i) const { return i == 0 ? prefix.characters : checkpoints.back().characters; }
    intptr_t Column(size_t i) const { return i == 0 ? prefix.column : checkpoints.back().column; }
    intptr_t SelectedCharacters() const { return charactersSelected; }
};

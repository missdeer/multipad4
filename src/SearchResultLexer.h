#pragma once
#include "App.h"
// Notepad++ LexSearchResult style and fold model, adapted to SCLEX_CONTAINER.
// This avoids the upstream private @MarkingsStruct pointer ABI.
namespace SearchResultLexer {
enum Style { Default, SearchHeader, FileHeader, LineNumber, Match };
struct Line {
    int style = Default;
    std::vector<std::pair<LRESULT, LRESULT>> segments;
    LRESULT numberStart = 0, numberEnd = 0;
};
void Configure(HWND editor);
void Colourise(HWND editor, const std::vector<Line>& lines);
}

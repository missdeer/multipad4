// Adapted from Notepad++ lexilla/lexers/LexSearchResult.cxx.
// Copyright 1998-2001 Neil Hodgson; Copyright 2003 Don HO.
// GPL-2.0-or-later. See 3rdparty/notepad-plus-plus/COPYING.search-result.
#include "SearchResultLexer.h"
namespace SearchResultLexer {
void Configure(HWND editor) {
    auto send = [editor](UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(editor, m, w, l); };
    send(SCI_SETLEXER, SCLEX_CONTAINER);
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    send(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>("Consolas"));
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, 10);
    send(SCI_STYLECLEARALL);
    send(SCI_STYLESETFORE, SearchHeader, RGB(0, 110, 0));
    send(SCI_STYLESETBACK, SearchHeader, RGB(215, 245, 215));
    send(SCI_STYLESETBOLD, SearchHeader, TRUE);
    send(SCI_STYLESETFORE, FileHeader, RGB(0, 70, 170));
    send(SCI_STYLESETBOLD, FileHeader, TRUE);
    send(SCI_STYLESETFORE, LineNumber, RGB(0, 130, 120));
    send(SCI_STYLESETFORE, Match, RGB(170, 0, 0));
    send(SCI_STYLESETBACK, Match, RGB(255, 235, 140));
    send(SCI_STYLESETBOLD, Match, TRUE);
    send(SCI_SETMARGINWIDTHN, 0, 0);
    send(SCI_SETMARGINWIDTHN, 1, 0);
    send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
    send(SCI_SETMARGINWIDTHN, 2, MulDiv(16, GetDpiForWindow(editor), 96));
    send(SCI_SETMARGINSENSITIVEN, 2, TRUE);
    send(SCI_SETSCROLLWIDTHTRACKING, TRUE);
    send(SCI_SETSCROLLWIDTH, 1);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
    send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CHANGE);
    send(SCI_SETUNDOCOLLECTION, FALSE);
    send(SCI_SETREADONLY, TRUE);
}
void Colourise(HWND editor, const std::vector<Line>& lines, size_t first, size_t last) {
    auto send = [editor](UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(editor, m, w, l); };
    for (size_t i = first; i < (std::min)(last, lines.size()); ++i) {
        auto start = send(SCI_POSITIONFROMLINE, i), length = send(SCI_LINELENGTH, i);
        const auto& line = lines[i];
        send(SCI_STARTSTYLING, start);
        send(SCI_SETSTYLING, length, line.style);
        int level = SC_FOLDLEVELBASE + 2;
        if (line.style == SearchHeader) level = SC_FOLDLEVELBASE | SC_FOLDLEVELHEADERFLAG;
        else if (line.style == FileHeader) level = (SC_FOLDLEVELBASE + 1) | SC_FOLDLEVELHEADERFLAG;
        else {
            if (line.numberEnd > line.numberStart) {
                send(SCI_STARTSTYLING, start + line.numberStart);
                send(SCI_SETSTYLING, line.numberEnd - line.numberStart, LineNumber);
            }
            for (auto [a, b] : line.segments) {
                a = std::clamp<LRESULT>(a, 0, length); b = std::clamp<LRESULT>(b, a, length);
                send(SCI_STARTSTYLING, start + a); send(SCI_SETSTYLING, b - a, Match);
            }
        }
        send(SCI_SETFOLDLEVEL, i, level);
    }
}
}

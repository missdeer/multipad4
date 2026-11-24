#pragma once
#include <windows.h>
// MultiPad4 extensions to the Unicode Notepad++ plugin protocol.
// Optional export BOOL multipadCommandEnabled(UINT commandId) controls menu and dispatch.
// Sent synchronously after tab titles, order, selection or membership change.
// idFrom is the active buffer id for selection-only updates, otherwise zero.
constexpr UINT MPN_DOCUMENTSCHANGED = 0x4D500001;
constexpr UINT MPM_GETBUFFERTITLE = WM_USER + 1000 + 950; // NPPMSG + 950
// wParam is the buffer id. Returns the native files::Encoding value or -1.
constexpr UINT MPM_GETBUFFERFILEENCODING = WM_USER + 1000 + 951;
// lParam receives the active buffer's formatting styles. Returns FALSE if unavailable.
constexpr UINT MPM_GETCODEFORMATSTYLES = WM_USER + 1000 + 952;
struct MultiPadCodeFormatStyles {
    int iLexer;
    unsigned commentStyleMarker, escapeCharacterStyle, operatorStyle, operatorStyle2;
    unsigned stringStyleFirst, stringStyleLast;
};
// Apply the callback to each active selection using Scintilla's case-mapping edit.
// The callback returns a process-heap buffer and sets length, or returns nullptr.
constexpr UINT MPM_MAPSELECTION = WM_USER + 1000 + 953;
using MultiPadSelectionMapper = char* (*)(const char* text, size_t& length, UINT codePage) noexcept;
struct MultiPadBufferTitle {
    wchar_t* text;
    size_t capacity; // Includes the terminator; query with nullptr for required length.
};

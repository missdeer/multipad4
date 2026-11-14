#pragma once
#include <windows.h>
// MultiPad4 extensions to the Unicode Notepad++ plugin protocol.
// Sent synchronously after tab titles, order, selection or membership change.
constexpr UINT MPN_DOCUMENTSCHANGED = 0x4D500001;
constexpr UINT MPM_GETBUFFERTITLE = WM_USER + 1000 + 950; // NPPMSG + 950
struct MultiPadBufferTitle {
    wchar_t* text;
    size_t capacity; // Includes the terminator; query with nullptr for required length.
};

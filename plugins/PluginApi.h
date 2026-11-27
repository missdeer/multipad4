#pragma once
#include <windows.h>
// MultiPad4 extensions to the Unicode Notepad++ plugin protocol.
// Optional export BOOL multipadCommandEnabled(UINT commandId) controls menu and dispatch.
// Optional export void multipadPopulateEditorContextMenu(HMENU) appends commands
// using the IDs assigned to the plugin's FuncItem entries.
// Optional export const wchar_t* multipadGetMainMenuLabel() runs after getFuncsArray.
// Return a UTF-16 label in static storage to place the plugin's FuncItem menu on the
// main menu bar. Return nullptr or an empty label to use the Plugins menu.

// Sent synchronously after tab titles, order, selection or membership change.
// idFrom is the active buffer id for selection-only updates, otherwise zero.
constexpr UINT MPN_DOCUMENTSCHANGED = 0x4D500001;
constexpr UINT MPM_GETBUFFERTITLE = WM_USER + 1000 + 950; // NPPMSG + 950
// wParam is the buffer id. Returns the native files::Encoding value or -1.
constexpr UINT MPM_GETBUFFERFILEENCODING = WM_USER + 1000 + 951;
// Apply the callback to each active selection using Scintilla's case-mapping edit.
// The callback returns a process-heap buffer and sets length, or returns nullptr.
constexpr UINT MPM_MAPSELECTION = WM_USER + 1000 + 953;
using MultiPadSelectionMapper = char* (*)(const char* text, size_t& length, UINT codePage) noexcept;
// lParam points to a request for a string from the host profile.
constexpr UINT MPM_GETPROFILESTRING = WM_USER + 1000 + 954;
struct MultiPadProfileString {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* fallback;
    wchar_t* text;
    size_t capacity; // Includes the terminator; query with nullptr for required length.
};
// Open a folder path in lParam. wParam selects the path in its containing folder.
constexpr UINT MPM_OPENFOLDER = WM_USER + 1000 + 955;
// Run the command line in lParam with the active document's working directory.
constexpr UINT MPM_RUNCOMMAND = WM_USER + 1000 + 956;
// lParam receives metadata from the active document's lexer configuration.
constexpr UINT MPM_GETLEXERMETADATA = WM_USER + 1000 + 957;
struct MultiPadLexerMetadata {
    int iLexer;
    unsigned lexerAttr;
    unsigned commentStyleMarker, escapeCharacterStyle, operatorStyle, operatorStyle2;
    unsigned stringStyleFirst, stringStyleLast;
};
// Returns the active full editor's native HWND, or zero if unavailable.
constexpr UINT MPM_GETEDITORWINDOW = WM_USER + 1000 + 958;
// Like NPPM_GETFULLPATHFROMBUFFERID, with an empty path for untitled buffers.
// wParam is the buffer ID. Query length with lParam zero, or pass length + 1 wchar_t slots.
constexpr UINT MPM_GETBUFFERFILEPATH = WM_USER + 1000 + 959;
// Close the buffer in wParam using the host's normal save/discard/cancel flow.
constexpr UINT MPM_CLOSEBUFFER = WM_USER + 1000 + 960;
struct MultiPadBufferTitle {
    wchar_t* text;
    size_t capacity; // Includes the terminator; query with nullptr for required length.
};

// Optional export BOOL multipadHandleEditorNotification(const SCNotification*).
// UI-thread callback with the real editor HWND, before built-in style handling.
// TRUE skips built-in handling for this event; legacy beNotified still runs.
// Never consume SCN_MODIFIED: the host uses it for its revision bookkeeping.
constexpr UINT MPM_GETBUFFEREDITORWINDOW = WM_USER + 1000 + 961;
// wParam = buffer ID; returns its full editor HWND, or zero for closed/fast views.
// HWND is borrowed and must be revalidated after document lifecycle callbacks.

// wParam = buffer ID, lParam = BOOL; suspend built-in completion for that buffer.
// Reset on plugin stop; does not suppress indentation or paired-character handling.
constexpr UINT MPM_SETEXTERNALCOMPLETION = WM_USER + 1000 + 962;

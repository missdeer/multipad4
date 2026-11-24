#pragma once
#include <windows.h>
#include "FileIO.h"
#include "Scintilla.h"
#include "resource.h"
inline bool RejectRectangularEdit(HWND editor) {
    if (!SendMessageW(editor, SCI_SELECTIONISRECTANGLE, 0, 0))
        return false;
    wchar_t message[512]{};
    if (!LoadStringW(GetModuleHandleW(nullptr), IDS_SELRECT, message, _countof(message)))
        wcscpy_s(message, L"This command does not support rectangular selections.");
    auto text = files::ToUtf8(message);
    SendMessageW(editor, SCI_SHOWNOTIFICATION, SC_NOTIFICATIONPOSITION_DEFAULT, reinterpret_cast<LPARAM>(text.c_str()));
    return true;
}

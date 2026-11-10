#pragma once
#include "EditorDocument.h"
#include "resource.h"
inline bool RejectRectangularEdit(EditorDocument &document) {
    if (!document.Send(SCI_SELECTIONISRECTANGLE))
        return false;
    wchar_t message[512]{};
    if (!LoadStringW(_Module.GetModuleInstance(), IDS_SELRECT, message, 512))
        wcscpy_s(message, L"This command does not support rectangular selections.");
    auto text = files::ToUtf8(message);
    document.Send(SCI_SHOWNOTIFICATION, SC_NOTIFICATIONPOSITION_DEFAULT, reinterpret_cast<LPARAM>(text.c_str()));
    return true;
}

#pragma once
#include <windows.h>
#include <cwchar>

// The upstream templates leave ResizeDlg's sizing grip at a placeholder
// position. Keep it at the client corner, as ResizeDlg_InitEx does.
inline void PositionDialogResizeGrip(HWND dialog, int id) {
    HWND grip = GetDlgItem(dialog, id);
    wchar_t className[32]{};
    GetClassNameW(grip, className, _countof(className));
    if (_wcsicmp(className, L"ScrollBar") != 0 ||
        (GetWindowLongPtrW(grip, GWL_STYLE) & SBS_SIZEGRIP) == 0) return;
    RECT client{};
    GetClientRect(dialog, &client);
    int size = GetSystemMetricsForDpi(SM_CXHTHUMB, GetDpiForWindow(dialog));
    SetWindowPos(grip, nullptr, client.right - size, client.bottom - size, size, size,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

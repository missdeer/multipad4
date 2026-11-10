#pragma once
#include <windows.h>
// Clipboard viewers can transiently hold the shared clipboard after a copy.
inline bool OpenEditClipboard(HWND owner) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (attempt)
            Sleep(1U << (attempt - 1));
        if (OpenClipboard(owner))
            return true;
    }
    return false;
}

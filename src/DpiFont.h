#pragma once
#include "WinWindow.h"

class DpiGuiFont {
    UniqueFont font;
    UINT dpi = 0;
public:
    bool Update(UINT value) {
        if (!value) value = USER_DEFAULT_SCREEN_DPI;
        if (font && dpi == value) return true;
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, value)) return false;
        UniqueFont replacement(CreateFontIndirectW(&metrics.lfMessageFont));
        if (!replacement) return false;
        font.swap(replacement); dpi = value; return true;
    }
    HFONT Get() const { return font.get(); }
    void Apply(HWND control) const {
        if (control && font) ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font.get()), TRUE);
    }
};

#pragma once
#include "../src/Frameworks.h"
#include <stdexcept>

// The WTL API returns a borrowed wrapper around a newly allocated brush.
// Repeated splitter previews must release it after restoring the DC's brush.
inline void RunFrameworkCompatibilityTests() {
    WTL::CDC dc;
    if (!dc.CreateCompatibleDC(nullptr)) throw std::runtime_error("Cannot create framework test DC");
    WTL::CBitmap bitmap;
    if (!bitmap.CreateBitmap(16, 16, 1, 1, nullptr)) throw std::runtime_error("Cannot create framework test bitmap");
    const auto previous = dc.SelectBitmap(bitmap);
    dockwins::CSimpleSplitterBar<> splitter(true);
    splitter.SetRect(0, 0, 16, 16);
    splitter.DrawGhostBar(dc); splitter.CleanGhostBar(dc);
    const auto before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 64; ++i) {
        splitter.DrawGhostBar(dc); splitter.CleanGhostBar(dc);
    }
    const auto after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    dc.SelectBitmap(previous);
    if (after > before + 2) throw std::runtime_error("Splitter preview leaked GDI objects");
}

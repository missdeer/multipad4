#pragma once
#include <windows.h>
#include <string>
#include <string_view>
namespace simd {
bool BitmapMergeAlpha(HBITMAP bitmap, COLORREF background) noexcept;
bool BitmapAlphaBlend(HBITMAP bitmap, COLORREF background, BYTE alpha) noexcept;
bool VerifyContrast(COLORREF first, COLORREF second) noexcept;
std::string NarrowAsciiTag(std::wstring_view text);
}

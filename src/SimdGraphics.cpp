// Adapted from Notepad4 Helpers.cpp / Edit.cpp.
// See 3rdparty/notepad4/License.txt and THIRD_PARTY.md.
#include "SimdGraphics.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "VectorISA.h"
#include "GraphicUtils.h"
namespace simd {
bool BitmapMergeAlpha(HBITMAP hbmp, COLORREF crDest) noexcept {
    BITMAP bmp{};
    if (GetObject(hbmp, sizeof(BITMAP), &bmp)) {
        if (bmp.bmBitsPixel == 32 && bmp.bmBits && bmp.bmWidth > 0 && bmp.bmHeight > 0) {
            //StopWatch watch;
            //watch.Start();
#if NP2_USE_AVX2
            #define BitmapMergeAlpha_Tag    "sse4 2x1"
            const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 2U;
            uint64_t *prgba = static_cast<uint64_t *>(bmp.bmBits);

            const __m128i i16x8Back = rgba_to_bgra_epi16x8_sse4_si32(crDest);
            for (ULONG x = 0; x < count; x++, prgba++) {
                __m128i i16x8Fore = unpack_color_epi16_sse4_ptr64(prgba);
                const __m128i i16x8Alpha = _mm_shufflehi_epi16(_mm_shufflelo_epi16(i16x8Fore, 0xff), 0xff);
                i16x8Fore = mm_alpha_blend_epi16(i16x8Fore, i16x8Back, i16x8Alpha);
                const uint64_t color = pack_color_epi16_sse2_si64(i16x8Fore);
                *prgba = color | UINT64_C(0xff000000ff000000);
            }

#elif NP2_USE_SSE2
            #define BitmapMergeAlpha_Tag    "sse2 1x1"
            const ULONG count = bmp.bmHeight * bmp.bmWidth;
            uint32_t *prgba = static_cast<uint32_t *>(bmp.bmBits);

            const __m128i i16x4Back = rgba_to_bgra_epi16_sse2_si32(crDest);
            for (ULONG x = 0; x < count; x++, prgba++) {
                __m128i i16x4Fore = unpack_color_epi16_sse2_ptr32(prgba);
                const __m128i i16x4Alpha = _mm_shufflelo_epi16(i16x4Fore, 0xff);
                i16x4Fore = mm_alpha_blend_epi16(i16x4Fore, i16x4Back, i16x4Alpha);
                const uint32_t color = pack_color_epi16_sse2_si32(i16x4Fore);
                *prgba = color | 0xff000000U;
            }

#else
            #define BitmapMergeAlpha_Tag    "scalar"
            const ULONG count = bmp.bmHeight * bmp.bmWidth;
            RGBQUAD *prgba = static_cast<RGBQUAD *>(bmp.bmBits);

            const BYTE red = GetRValue(crDest);
            const BYTE green = GetGValue(crDest);
            const BYTE blue = GetBValue(crDest);
            for (ULONG x = 0; x < count; x++) {
                const BYTE alpha = prgba[x].rgbReserved;
                prgba[x].rgbRed = ((prgba[x].rgbRed * alpha) + (red * (255 ^ alpha))) >> 8;
                prgba[x].rgbGreen = ((prgba[x].rgbGreen * alpha) + (green * (255 ^ alpha))) >> 8;
                prgba[x].rgbBlue = ((prgba[x].rgbBlue * alpha) + (blue * (255 ^ alpha))) >> 8;
                prgba[x].rgbReserved = 0xFF;
            }
#endif
#if NP2_USE_AVX2
            if ((bmp.bmHeight * bmp.bmWidth) & 1) {
                auto* pixel = static_cast<uint32_t*>(bmp.bmBits) + bmp.bmHeight * bmp.bmWidth - 1;
                auto color = unpack_color_epi16_sse2_ptr32(pixel);
                color = mm_alpha_blend_epi16(color, rgba_to_bgra_epi16_sse2_si32(crDest), _mm_shufflelo_epi16(color, 0xff));
                *pixel = pack_color_epi16_sse2_si32(color) | 0xff000000U;
            }
#endif
            //watch.Stop();
            //watch.ShowLog("BitmapMergeAlpha " BitmapMergeAlpha_Tag);
            #undef BitmapMergeAlpha_Tag
            return true;
        }
    }
    return false;
}
bool BitmapAlphaBlend(HBITMAP hbmp, COLORREF crDest, BYTE alpha) noexcept {
    BITMAP bmp{};
    if (GetObject(hbmp, sizeof(BITMAP), &bmp)) {
        if (bmp.bmBitsPixel == 32 && bmp.bmBits && bmp.bmWidth > 0 && bmp.bmHeight > 0) {
            //StopWatch watch;
            //watch.Start();
            //FILE *fp = fopen("bitmap.dat", "wb");
            //fwrite(bmp.bmBits, 1, bmp.bmHeight*bmp.bmWidth*4, fp);
            //fclose(fp);
#if 0//NP2_USE_AVX512
            #define BitmapAlphaBlend_Tag    "avx512 8x1"
            const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 8U;
            __m256i *prgba = static_cast<__m256i *>(bmp.bmBits);

            const __m512i i16x32Alpha = _mm512_broadcastw_epi16(_mm_cvtsi32_si128(alpha));
            const __m512i i16x32Back = _mm512_broadcastq_epi64(_mm_mullo_epi16(rgba_to_bgra_epi16_sse2_si32(crDest), mm_xor_alpha_epi16(_mm512_castsi512_si128(i16x32Alpha))));
            const __m512i i16x32_0x8081 = _mm512_broadcast_i32x4(_mm_set1_epi16(-0x8000 | 0x81));
            for (ULONG x = 0; x < count; x++, prgba++) {
                const __m512i origin = unpack_color_epi16_avx512_ptr256(prgba);
                __m512i i16x32Fore = _mm512_mullo_epi16(origin, i16x32Alpha);
                i16x32Fore = _mm512_add_epi16(i16x32Fore, i16x32Back);
                i16x32Fore = mm512_div_epu16_by_255(i16x32Fore, i16x32_0x8081);
                i16x32Fore = _mm512_mask_blend_epi16(0x77777777, origin, i16x32Fore);
                i16x32Fore = pack_color_epi16_avx512_si512(i16x32Fore);
                _mm256_storeu_si256(prgba, _mm512_castsi512_si256(i16x32Fore));
            }

#elif NP2_USE_AVX2
#if 1
            #define BitmapAlphaBlend_Tag    "avx2 4x1"
            const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 4U;
            __m128i *prgba = static_cast<__m128i *>(bmp.bmBits);

            const __m256i i16x16Alpha = _mm256_broadcastw_epi16(_mm_cvtsi32_si128(alpha));
            const __m256i i16x16Back = _mm256_broadcastq_epi64(_mm_mullo_epi16(rgba_to_bgra_epi16_sse4_si32(crDest), mm_xor_alpha_epi16(_mm256_castsi256_si128(i16x16Alpha))));
            const __m256i i16x16_0x8081 = _mm256_broadcastsi128_si256(_mm_set1_epi16(-0x8000 | 0x81));
            for (ULONG x = 0; x < count; x++, prgba++) {
                const __m256i origin = unpack_color_epi16_avx2_ptr128(prgba);
                __m256i i16x16Fore = _mm256_mullo_epi16(origin, i16x16Alpha);
                i16x16Fore = _mm256_add_epi16(i16x16Fore, i16x16Back);
                i16x16Fore = mm256_div_epu16_by_255(i16x16Fore, i16x16_0x8081);
                i16x16Fore = _mm256_blend_epi16(origin, i16x16Fore, 0x77);
                i16x16Fore = pack_color_epi16_avx2_si256(i16x16Fore);
                _mm_storeu_si128(prgba, _mm256_castsi256_si128(i16x16Fore));
            }
#else
            #define BitmapAlphaBlend_Tag    "sse4 2x1"
            const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 2U;
            uint64_t *prgba = static_cast<uint64_t *>(bmp.bmBits);

            const __m128i i16x8Alpha = _mm_broadcastw_epi16(_mm_cvtsi32_si128(alpha));
            const __m128i i16x8Back = _mm_mullo_epi16(rgba_to_bgra_epi16x8_sse4_si32(crDest), mm_xor_alpha_epi16(i16x8Alpha));
            for (ULONG x = 0; x < count; x++, prgba++) {
                const __m128i origin = unpack_color_epi16_sse4_ptr64(prgba);
                __m128i i16x8Fore = _mm_mullo_epi16(origin, i16x8Alpha);
                i16x8Fore = _mm_add_epi16(i16x8Fore, i16x8Back);
                i16x8Fore = mm_div_epu16_by_255(i16x8Fore);
                i16x8Fore = _mm_blend_epi16(origin, i16x8Fore, 0x77);
                i16x8Fore = pack_color_epi16_sse2_si128(i16x8Fore);
                _mm_storel_epi64(reinterpret_cast<__m128i *>(prgba), i16x8Fore);
            }
#endif // NP2_USE_AVX2
#elif NP2_USE_SSE2
            #define BitmapAlphaBlend_Tag    "sse2 1x4"
            const ULONG count = (bmp.bmHeight * bmp.bmWidth) / 4U;
            __m128i *prgba = static_cast<__m128i *>(bmp.bmBits);

            const __m128i i16x8Alpha = _mm_shuffle_epi32(mm_setlo_alpha_epi16(alpha), 0x44);
            __m128i i16x8Back = _mm_shuffle_epi32(rgba_to_bgra_epi16_sse2_si32(crDest), 0x44);
            i16x8Back = _mm_mullo_epi16(i16x8Back, mm_xor_alpha_epi16(i16x8Alpha));
            for (ULONG x = 0; x < count; x++, prgba++) {
                __m128i origin = _mm_loadu_si128(prgba);
                __m128i color42 = _mm_shuffle_epi32(origin, 0x31);

                __m128i i16x8Fore = unpacklo_color_epi16_sse2_si32(origin);
                i16x8Fore = _mm_unpacklo_epi64(i16x8Fore, unpacklo_color_epi16_sse2_si32(color42));
                i16x8Fore = _mm_mullo_epi16(i16x8Fore, i16x8Alpha);
                i16x8Fore = _mm_add_epi16(i16x8Fore, i16x8Back);
                i16x8Fore = mm_div_epu16_by_255(i16x8Fore);
                __m128i i32x4Fore = pack_color_epi16_sse2_si128(i16x8Fore);

                i16x8Fore = unpackhi_color_epi16_sse2_si128(origin);
                i16x8Fore = _mm_unpacklo_epi64(i16x8Fore, unpackhi_color_epi16_sse2_si128(color42));
                i16x8Fore = _mm_mullo_epi16(i16x8Fore, i16x8Alpha);
                i16x8Fore = _mm_add_epi16(i16x8Fore, i16x8Back);
                i16x8Fore = mm_div_epu16_by_255(i16x8Fore);
                color42 = pack_color_epi16_sse2_si128(i16x8Fore);

                i32x4Fore = _mm_unpacklo_epi64(i32x4Fore, color42);
                i32x4Fore = _mm_and_si128(_mm_set1_epi32(0x00ffffff), i32x4Fore);
                origin = _mm_andnot_si128(_mm_set1_epi32(0x00ffffff), origin);
                i32x4Fore = _mm_or_si128(origin, i32x4Fore);
                _mm_storeu_si128(prgba, i32x4Fore);
            }

#else
            #define BitmapAlphaBlend_Tag    "scalar"
            const ULONG count = bmp.bmHeight * bmp.bmWidth;
            RGBQUAD *prgba = static_cast<RGBQUAD *>(bmp.bmBits);

            const WORD red = GetRValue(crDest) * (255 ^ alpha);
            const WORD green = GetGValue(crDest) * (255 ^ alpha);
            const WORD blue = GetBValue(crDest) * (255 ^ alpha);
            for (ULONG x = 0; x < count; x++) {
                prgba[x].rgbRed = ((prgba[x].rgbRed * alpha) + red) >> 8;
                prgba[x].rgbGreen = ((prgba[x].rgbGreen * alpha) + green) >> 8;
                prgba[x].rgbBlue = ((prgba[x].rgbBlue * alpha) + blue) >> 8;
            }
#endif
            // SSE2 and AVX2 process four pixels per iteration.
            auto* pixels = static_cast<RGBQUAD*>(bmp.bmBits);
            const size_t pixelsCount = static_cast<size_t>(bmp.bmHeight) * bmp.bmWidth;
            for (size_t i = pixelsCount & ~size_t(3); i < pixelsCount; ++i) {
                pixels[i].rgbRed = static_cast<BYTE>((pixels[i].rgbRed * alpha + GetRValue(crDest) * (255 - alpha)) / 255);
                pixels[i].rgbGreen = static_cast<BYTE>((pixels[i].rgbGreen * alpha + GetGValue(crDest) * (255 - alpha)) / 255);
                pixels[i].rgbBlue = static_cast<BYTE>((pixels[i].rgbBlue * alpha + GetBValue(crDest) * (255 - alpha)) / 255);
            }
            //watch.Stop();
            //watch.ShowLog("BitmapAlphaBlend " BitmapAlphaBlend_Tag);
            #undef BitmapAlphaBlend_Tag
            return true;
        }
    }
    return false;
}
bool VerifyContrast(COLORREF cr1, COLORREF cr2) noexcept {
    // reduce code size
#if NP2_USE_AVX2
    __m128i i32x4Fore = unpack_color_epi32_sse4_si32(cr1);
    __m128i i32x4Back = unpack_color_epi32_sse4_si32(cr2);

    __m128i diff = _mm_sad_epu8(i32x4Fore, i32x4Back);
    int value = mm_hadd_epi32_si32(diff);
    if (value >= 400) {
        return true;
    }

    i32x4Fore = _mm_mullo_epi16(i32x4Fore, _mm_setr_epi32(3, 5, 1, 0));
    i32x4Back = _mm_mullo_epi16(i32x4Back, _mm_setr_epi32(3, 6, 1, 0));
    diff = _mm_sub_epi32(i32x4Fore, i32x4Back);
    value = mm_hadd_epi32_si32(diff);
    return abs(value) >= 400;

#else
    const BYTE r1 = GetRValue(cr1);
    const BYTE g1 = GetGValue(cr1);
    const BYTE b1 = GetBValue(cr1);
    const BYTE r2 = GetRValue(cr2);
    const BYTE g2 = GetGValue(cr2);
    const BYTE b2 = GetBValue(cr2);

    return  ((abs((3 * r1 + 5 * g1 + 1 * b1) - (3 * r2 + 6 * g2 + 1 * b2))) >= 400) ||
            ((abs(r1 - r2) + abs(b1 - b2) + abs(g1 - g2)) >= 400);
#endif
}
std::string NarrowAsciiTag(std::wstring_view text) {
    std::string output(text.size(), '\0');
    size_t at = 0;
    for (; text.size() - at >= 16; at += 16) {
        const auto* input = reinterpret_cast<const __m128i*>(text.data() + at);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(output.data() + at),
            _mm_packus_epi16(_mm_loadu_si128(input), _mm_loadu_si128(input + 1)));
    }
    if (at < text.size()) {
        wchar_t tail[16]{};
        char narrow[16];
        std::memcpy(tail, text.data() + at, (text.size() - at) * sizeof(wchar_t));
        const auto* input = reinterpret_cast<const __m128i*>(tail);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(narrow),
            _mm_packus_epi16(_mm_loadu_si128(input), _mm_loadu_si128(input + 1)));
        std::memcpy(output.data() + at, narrow, text.size() - at);
    }
    return output;
}
}

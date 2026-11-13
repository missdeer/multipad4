// Adapted from notepad4 Edit.cpp / EditEncoding.cpp (see THIRD_PARTY.md).
// This file contains code from Notepad4; see 3rdparty/notepad4/License.txt.
// https://github.com/zwegner/faster-utf8-validator
// faster-utf8-validator
// Copyright (c) 2019 Zach Wegner
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "SimdText.h"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "VectorISA.h"
namespace simd {
namespace {
constexpr int SC_EOL_CRLF = 0, SC_EOL_CR = 1, SC_EOL_LF = 2;
constexpr int CPI_DEFAULT = 0, CPI_UNICODE = 1;
void EditDetectEOLMode(LPCSTR lpData, size_t cbData, LineEndings &status) noexcept {
    /* '\r' and '\n' is not reused (e.g. as trailing byte in DBCS) by any known encoding,
    it's safe to check whole data byte by byte.*/

    size_t lineCountCRLF = 0;
    size_t lineCountCR = 0;
    size_t lineCountLF = 0;

    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(lpData);
    // No NULL-terminated requirement for *ptr == '\n'
#if NP2_USE_SSE2 || NP2_USE_AVX2
    const uint8_t * const end = ptr + cbData;
#else
    const uint8_t * const end = ptr + cbData - 1;
#endif

#if NP2_USE_AVX512
    uint8_t lastCR = 0;
    const __m512i vectCR = _mm512_set1_epi32('\r' * 0x01010101);
    const __m512i vectLF = _mm512_set1_epi32('\n' * 0x01010101);
    while (static_cast<size_t>(end - ptr) >= sizeof(__m512i)) {
        const __m512i chunk = _mm512_loadu_si512(ptr);
        ptr += sizeof(__m512i);
        uint64_t maskLF = _mm512_cmpeq_epi8_mask(chunk, vectLF);
        uint64_t maskCR = _mm512_cmpeq_epi8_mask(chunk, vectCR);

        if (maskCR | lastCR) {
            lastCR = _addcarry_u64(lastCR, maskCR, maskCR, &maskCR);

            // maskCR and maskLF never have some bit set, after shifting maskCR by 1 bit,
            // the bits both set in maskCR and maskLF represents CR+LF;
            // the bits only set in maskCR or maskLF represents individual CR or LF.
            const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
            lineCountCRLF += np2_popcount64(maskCRLF);
            maskLF = andn_u64(maskCR, maskLF); // LF alone
            maskCR = maskCRLF ^ maskCR; // CR alone (with one position offset)
            if (maskCR) {
                lineCountCR += np2_popcount64(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount64(maskLF);
        }
    }

    if (ptr < end) {
        uint8_t buffer[sizeof(__m512i)];
        const __m512i zero = _mm512_setzero_si512();
        _mm512_storeu_si512(buffer, zero);
        __movsb(buffer, ptr, end - ptr);

        const __m512i chunk = _mm512_loadu_si512(buffer);
        uint64_t maskLF = _mm512_cmpeq_epi8_mask(chunk, vectLF);
        uint64_t maskCR = _mm512_cmpeq_epi8_mask(chunk, vectCR);

        if (maskCR | lastCR) {
            lastCR = _addcarry_u64(lastCR, maskCR, maskCR, &maskCR);
            _addcarry_u64(lastCR, lineCountCR, 0, &lineCountCR);
            lastCR = 0;

            const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
            lineCountCRLF += np2_popcount64(maskCRLF);
            maskLF = andn_u64(maskCR, maskLF); // LF alone
            maskCR = maskCRLF ^ maskCR; // CR alone (with one position offset)
            if (maskCR) {
                lineCountCR += np2_popcount64(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount64(maskLF);
        }
    }
    lineCountCR += lastCR;
    // end NP2_USE_AVX512
#elif NP2_USE_AVX2
    uint8_t lastCR = 0;
    const __m256i vectCR = _mm256_set1_epi8('\r');
    const __m256i vectLF = _mm256_set1_epi8('\n');
    while (static_cast<size_t>(end - ptr) >= 2*sizeof(__m256i)) {
        const __m256i chunk1 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(ptr));
        const __m256i chunk2 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(ptr + sizeof(__m256i)));
        ptr += 2*sizeof(__m256i);
        uint64_t maskCR = mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectCR));
        uint64_t maskLF = mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectLF));
        maskLF |= static_cast<uint64_t>(mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m256i);
        maskCR |= static_cast<uint64_t>(mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m256i);

        if (maskCR | lastCR) {
            lastCR = _addcarry_u64(lastCR, maskCR, maskCR, &maskCR);

            // maskCR and maskLF never have some bit set, after shifting maskCR by 1 bit,
            // the bits both set in maskCR and maskLF represents CR+LF;
            // the bits only set in maskCR or maskLF represents individual CR or LF.
            const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
            lineCountCRLF += np2_popcount64(maskCRLF);
            maskLF = andn_u64(maskCR, maskLF); // LF alone
            maskCR = maskCRLF ^ maskCR; // CR alone (with one position offset)
            if (maskCR) {
                lineCountCR += np2_popcount64(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount64(maskLF);
        }
    }

    if (ptr < end) {
        uint8_t buffer[2*sizeof(__m256i)];
        const __m256i zero = _mm256_setzero_si256();
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(buffer), zero);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(buffer + sizeof(__m256i)), zero);
        __movsb(buffer, ptr, end - ptr);

        const __m256i chunk1 = _mm256_loadu_si256(reinterpret_cast<__m256i *>(buffer));
        const __m256i chunk2 = _mm256_loadu_si256(reinterpret_cast<__m256i *>(buffer + sizeof(__m256i)));
        uint64_t maskCR = mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectCR));
        uint64_t maskLF = mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk1, vectLF));
        maskLF |= static_cast<uint64_t>(mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectLF))) << sizeof(__m256i);
        maskCR |= static_cast<uint64_t>(mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk2, vectCR))) << sizeof(__m256i);

        if (maskCR | lastCR) {
            lastCR = _addcarry_u64(lastCR, maskCR, maskCR, &maskCR);
            _addcarry_u64(lastCR, lineCountCR, 0, &lineCountCR);
            lastCR = 0;

            const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
            lineCountCRLF += np2_popcount64(maskCRLF);
            maskLF = andn_u64(maskCR, maskLF); // LF alone
            maskCR = maskCRLF ^ maskCR; // CR alone (with one position offset)
            if (maskCR) {
                lineCountCR += np2_popcount64(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount64(maskLF);
        }
    }
    lineCountCR += lastCR;
    // end NP2_USE_AVX2
#elif NP2_USE_SSE2
#if defined(_WIN64)
    const __m128i vectCR = _mm_set1_epi8('\r');
    const __m128i vectLF = _mm_set1_epi8('\n');
    uint8_t lastCR = 0;
    while (static_cast<size_t>(end - ptr) >= 4*sizeof(__m128i)) {
        const __m128i chunk1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr));
        const __m128i chunk2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr + sizeof(__m128i)));
        const __m128i chunk3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr + 2*sizeof(__m128i)));
        const __m128i chunk4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr + 3*sizeof(__m128i)));
        ptr += 4*sizeof(__m128i);
        uint64_t maskCR = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
        uint64_t maskLF = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
        maskCR |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR)) << sizeof(__m128i);
        maskLF |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF)) << sizeof(__m128i);
        maskCR |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectCR))) << 2*sizeof(__m128i);
        maskLF |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectLF))) << 2*sizeof(__m128i);
        maskLF |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectLF))) << 3*sizeof(__m128i);
        maskCR |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectCR))) << 3*sizeof(__m128i);

        if (maskCR | lastCR) {
            lastCR = _addcarry_u64(lastCR, maskCR, maskCR, &maskCR);

            // maskCR and maskLF never have some bit set, after shifting maskCR by 1 bit,
            // the bits both set in maskCR and maskLF represents CR+LF;
            // the bits only set in maskCR or maskLF represents individual CR or LF.
            const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
            const uint64_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
            maskLF = maskCR_LF & maskLF; // LF alone
            maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
            if (maskCRLF) {
                lineCountCRLF += np2_popcount64(maskCRLF);
            }
            if (maskCR) {
                lineCountCR += np2_popcount64(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount64(maskLF);
        }
    }

    if (ptr < end) {
        alignas(16) uint8_t buffer[4*sizeof(__m128i)];
        const __m128 zero = _mm_setzero_ps();
        _mm_store_ps(reinterpret_cast<float *>(buffer), zero);
        _mm_store_ps(reinterpret_cast<float *>(buffer + sizeof(__m128)), zero);
        _mm_store_ps(reinterpret_cast<float *>(buffer + 2*sizeof(__m128)), zero);
        _mm_store_ps(reinterpret_cast<float *>(buffer + 3*sizeof(__m128)), zero);
        __movsb(buffer, ptr, end - ptr);

        const __m128i chunk1 = _mm_load_si128(reinterpret_cast<__m128i *>(buffer));
        const __m128i chunk2 = _mm_load_si128(reinterpret_cast<__m128i *>(buffer + sizeof(__m128i)));
        const __m128i chunk3 = _mm_load_si128(reinterpret_cast<__m128i *>(buffer + 2*sizeof(__m128i)));
        const __m128i chunk4 = _mm_load_si128(reinterpret_cast<__m128i *>(buffer + 3*sizeof(__m128i)));
        uint64_t maskCR = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
        uint64_t maskLF = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
        maskCR |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR)) << sizeof(__m128i);
        maskLF |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF)) << sizeof(__m128i);
        maskCR |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectCR))) << 2*sizeof(__m128i);
        maskLF |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, vectLF))) << 2*sizeof(__m128i);
        maskLF |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectLF))) << 3*sizeof(__m128i);
        maskCR |= static_cast<uint64_t>(mm_movemask_epi8(_mm_cmpeq_epi8(chunk4, vectCR))) << 3*sizeof(__m128i);

        if (maskCR | lastCR) {
            lastCR = _addcarry_u64(lastCR, maskCR, maskCR, &maskCR);
            _addcarry_u64(lastCR, lineCountCR, 0, &lineCountCR);
            lastCR = 0;

            const uint64_t maskCRLF = maskCR & maskLF; // CR+LF
            const uint64_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
            maskLF = maskCR_LF & maskLF; // LF alone
            maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
            if (maskCRLF) {
                lineCountCRLF += np2_popcount64(maskCRLF);
            }
            if (maskCR) {
                lineCountCR += np2_popcount64(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount64(maskLF);
        }
    }
    lineCountCR += lastCR;
    // end _WIN64 NP2_USE_SSE2
#else
    const __m128i vectCR = _mm_set1_epi8('\r');
    const __m128i vectLF = _mm_set1_epi8('\n');
    // not accumulate lastCR due to less register on x86
    while (ptr + 2*sizeof(__m128i) < end) {
        const __m128i chunk1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr));
        const __m128i chunk2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr + sizeof(__m128i)));
        ptr += 2*sizeof(__m128i);
        uint32_t maskCR = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
        uint32_t maskLF = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
        maskLF |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF)) << sizeof(__m128i);
        maskCR |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR)) << sizeof(__m128i);

        if (maskCR) {
            if (_addcarry_u32(0, maskCR, maskCR, &maskCR)) {
                if (*ptr == '\n') {
                    // CR+LF across boundary
                    ++ptr;
                    ++lineCountCRLF;
                } else {
                    ++lineCountCR;
                }
            }

            // maskCR and maskLF never have some bit set, after shifting maskCR by 1 bit,
            // the bits both set in maskCR and maskLF represents CR+LF;
            // the bits only set in maskCR or maskLF represents individual CR or LF.
            const uint32_t maskCRLF = maskCR & maskLF; // CR+LF
            const uint32_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
            maskLF = maskCR_LF & maskLF; // LF alone
            maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
            if (maskCRLF) {
                lineCountCRLF += np2_popcount(maskCRLF);
            }
            if (maskCR) {
                lineCountCR += np2_popcount(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount(maskLF);
        }
    }

    if (ptr < end) {
        uint8_t buffer[2*sizeof(__m128i)];
        const __m128 zero = _mm_setzero_ps();
        _mm_storeu_ps(reinterpret_cast<float *>(buffer), zero);
        _mm_storeu_ps(reinterpret_cast<float *>(buffer + sizeof(__m128)), zero);
        __movsb(buffer, ptr, end - ptr);

        const __m128i chunk1 = _mm_loadu_si128(reinterpret_cast<__m128i *>(buffer));
        const __m128i chunk2 = _mm_loadu_si128(reinterpret_cast<__m128i *>(buffer + sizeof(__m128i)));
        uint32_t maskCR = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectCR));
        uint32_t maskLF = mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, vectLF));
        maskLF |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectLF)) << sizeof(__m128i);
        maskCR |= mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, vectCR)) << sizeof(__m128i);

        if (maskCR) {
            const uint8_t lastCR = _addcarry_u32(0, maskCR, maskCR, &maskCR);
            _addcarry_u32(lastCR, lineCountCR, 0, &lineCountCR);

            const uint32_t maskCRLF = maskCR & maskLF; // CR+LF
            const uint32_t maskCR_LF = maskCR ^ maskLF;// CR alone or LF alone
            maskLF = maskCR_LF & maskLF; // LF alone
            maskCR = maskCR_LF ^ maskLF; // CR alone (with one position offset)
            if (maskCRLF) {
                lineCountCRLF += np2_popcount(maskCRLF);
            }
            if (maskCR) {
                lineCountCR += np2_popcount(maskCR);
            }
        }
        if (maskLF) {
            lineCountLF += np2_popcount(maskLF);
        }
    }
#endif
    // end NP2_USE_SSE2
#else

#if defined(__clang__) || defined(__GNUC__) || defined(__ICL) || !defined(_MSC_VER)
    while (ptr < end) {
        const uint8_t ch = *ptr++;
        const uint32_t mask = ((1 << '\r') - 1) ^ (1 << '\n');
        if (ch > '\r' || ((mask >> ch) & 1) != 0) {
            continue;
        }
        if (ch == '\n') {
            ++lineCountLF;
        } else {
            if (*ptr == '\n') {
                ++ptr;
                ++lineCountCRLF;
            } else {
                ++lineCountCR;
            }
        }
    }
#else
    do {
        // skip to line end
        uint8_t ch = 0;
#if 1
        const uint32_t mask = ((1 << '\r') - 1) ^ (1 << '\n');
        while (ptr < end && ((ch = *ptr++) > '\r' || ((mask >> ch) & 1) != 0)) {
            // nop
        }
#else
        while (ptr < end && ((ch = *ptr++) > '\r' || ch < '\n')) {
            // nop
        }
#endif
        switch (ch) {
        case '\n':
            ++lineCountLF;
            break;
        case '\r':
            if (*ptr == '\n') {
                ++ptr;
                ++lineCountCRLF;
            } else {
                ++lineCountCR;
            }
            break;
        }
    } while (ptr < end);
#endif
    if (ptr == end) {
        switch (*ptr) {
        case '\n':
            ++lineCountLF;
            break;
        case '\r':
            ++lineCountCR;
            break;
        }
    }
#endif

    const size_t linesMax = (std::max)((std::max)(lineCountCRLF, lineCountCR), lineCountLF);
    status.linesCount[SC_EOL_CRLF] = lineCountCRLF;
    status.linesCount[SC_EOL_CR] = lineCountCR;
    status.linesCount[SC_EOL_LF] = lineCountLF;
    int iEOLMode = status.iEOLMode;
    if (linesMax != status.linesCount[iEOLMode]) {
        if (linesMax == lineCountCRLF) {
            iEOLMode = SC_EOL_CRLF;
        } else if (linesMax == lineCountLF) {
            iEOLMode = SC_EOL_LF;
        } else {
            iEOLMode = SC_EOL_CR;
        }
    }

    status.iEOLMode = iEOLMode;
    status.bInconsistent = ((!!lineCountCRLF) + (!!lineCountCR) + (!!lineCountLF)) > 1;
    status.totalLineCount = lineCountCRLF + lineCountCR + lineCountLF + 1;
}
static int DetectUTF16LatinExt(const char *pTest, DWORD nLength) noexcept {
    const char *pt = pTest;
    const char * const end = pt + nLength;

#if NP2_USE_AVX512
    const uint64_t high = UINT64_MAX << (nLength & (sizeof(__m512i) - 1));
    __m512i test = _mm512_set1_epi16(-0x0800); // 0xF800
    uint64_t expected = 0xAAAAAAAA'AAAAAAAAULL;
    do {
        const __m512i chunk = _mm512_loadu_si512(pt);
        pt += sizeof(__m512i);
        uint64_t mask = _mm512_testn_epi8_mask(chunk, test);
        if (andn_u64(mask, expected) != 0) {
            if (pt > end) {
                mask |= high;
                if (andn_u64(mask, expected) == 0) {
                    break;
                }
            }
            if ((expected & 1) == 0) {
                expected >>= 1;
                if (andn_u64(mask, expected) == 0) {
                    pt = pTest;
                    test = _mm512_bsrli_epi128(test, 1);
                    continue;
                }
            }
            return CPI_DEFAULT;
        }
    } while (pt < end);
    // end NP2_USE_AVX512
#elif NP2_USE_AVX2
    nLength &= sizeof(__m256i) - 1;
    nLength = UINT32_MAX << nLength;
#if !NP2_USE_AVX512
    const __m256i zero = _mm256_setzero_si256();
#endif
    __m256i test = _mm256_set1_epi16(-0x0800); // 0xF800
    uint32_t expected = 0xAAAAAAAA;
    do {
        const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pt));
        pt += sizeof(__m256i);
        if (_mm256_testz_si256(chunk, test) == 0)
        //if (andn_u32(mask, expected) != 0)
        {
#if NP2_USE_AVX512
            uint32_t mask = _mm256_testn_epi8_mask(chunk, test);
#else
            uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_and_si256(chunk, test), zero));
#endif
            if (pt > end) {
                mask |= nLength;
                if (andn_u32(mask, expected) == 0) {
                    break;
                }
            }
            if ((expected & 1) == 0) {
                expected >>= 1;
                if (andn_u32(mask, expected) == 0) {
                    pt = pTest;
                    test = _mm256_srli_si256(test, 1);
                    continue;
                }
            }
            return CPI_DEFAULT;
        }
    } while (pt < end);
    // end NP2_USE_AVX2
#elif NP2_USE_SSE2
    uint32_t padding = nLength & (sizeof(__m128i) - 1);
    const uint32_t high = UINT32_MAX << padding;
    const uint32_t lower = ~high; // (1 << padding) - 1
    padding = padding ? high : 0;
    nLength &= 2*sizeof(__m128i) - 1;
    __m128i test = _mm_set1_epi16(-0x0800); // 0xF800
    const __m128i zero = _mm_setzero_si128();
    uint32_t expected = 0xAAAA;
    do {
        const __m128i chunk1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt));
        const __m128i chunk2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt + sizeof(__m128i)));
        pt += 2*sizeof(__m128i);
        uint32_t mask = ~_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(_mm_or_si128(chunk1, chunk2), test), zero));
        if ((mask & expected) != 0) {
            if (pt > end) {
                mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(chunk1, test), zero));
                uint32_t last = padding;
                if (nLength > sizeof(__m128i)) {
                    last = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(chunk2, test), zero));
                    last &= lower;
                }
                mask |= last;
                mask = ~mask;
                if ((mask & expected) == 0) {
                    break;
                }
            }
            if ((expected & 1) == 0) {
                expected >>= 1;
                if ((mask & expected) == 0) {
                    pt = pTest;
                    test = _mm_srli_si128(test, 1);
                    continue;
                }
            }
            return CPI_DEFAULT;
        }
    } while (pt < end);
    // end NP2_USE_SSE2
#elif defined(_WIN64)
    nLength &= sizeof(uint64_t) - 1;
    nLength = (sizeof(uint64_t) - nLength)*8;
    uint64_t expected = UINT64_C(0xF800F800F800F800);
    do {
        uint64_t value = *(reinterpret_cast<const uint64_t *>(pt));
        pt += sizeof(uint64_t);
        if ((value & expected) != 0) {
            if (pt > end) {
                value <<= nLength;
                if ((value & expected) == 0) {
                    break;
                }
            }
            if ((expected & 8) == 0) {
                expected >>= 8;
                if ((value & expected) == 0) {
                    pt = pTest;
                    continue;
                }
            }
            return CPI_DEFAULT;
        }
    } while (pt < end);
    expected >>= 3;
    // end _WIN64
#else
    nLength &= sizeof(uint32_t) - 1;
    nLength = (sizeof(uint32_t) - nLength)*8;
    uint32_t expected = 0xF800F800U;
    do {
        uint32_t value = *(reinterpret_cast<const uint32_t *>(pt));
        pt += sizeof(uint32_t);
        if ((value & expected) != 0) {
            if (pt > end) {
                value <<= nLength;
                if ((value & expected) == 0) {
                    break;
                }
            }
            if ((expected & 8) == 0) {
                expected >>= 8;
                if ((value & expected) == 0) {
                    pt = pTest;
                    continue;
                }
            }
            return CPI_DEFAULT;
        }
    } while (pt < end);
    expected >>= 3;
    // end _WIN32
#endif

    return CPI_UNICODE + (expected & 1);
}
#if NP2_USE_AVX2
#if defined(__GNUC__) || defined(__clang__)
__attribute__((__always_inline__)) static inline
#else
static __forceinline
#endif
bool z_validate_vec_avx2(__m256i bytes, __m256i shifted_bytes, uint32_t *last_cont) noexcept {
    // Error lookup tables for the first, second, and third nibbles
    // Simple macro to make a vector lookup table for use with vpshufb. Since
    // AVX2 is two 16-byte halves, we duplicate the input values.
#define V_TABLE_16(...)     _mm256_setr_epi8(__VA_ARGS__, __VA_ARGS__)
    const __m256i error_1 = V_TABLE_16(
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x06, 0x38
    );
    const __m256i error_2 = V_TABLE_16(
        0x0B, 0x01, 0x00, 0x00,
        0x10, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20,
        0x20, 0x24, 0x20, 0x20
    );
    const __m256i error_3 = V_TABLE_16(
        0x29, 0x29, 0x29, 0x29,
        0x29, 0x29, 0x29, 0x29,
        0x2B, 0x33, 0x35, 0x35,
        0x31, 0x31, 0x31, 0x31
    );
#undef V_TABLE_16

    // Quick skip for ascii-only input. If there are no bytes with the high bit
    // set, we don't need to do any more work. We return either valid or
    // invalid based on whether we expected any continuation bytes here.
    const uint32_t high = _mm256_movemask_epi8(bytes);
    if (!high) {
        return *last_cont == 0;
    }

    // Which bytes are required to be continuation bytes
    uint64_t req = *last_cont;

    // Compute the continuation byte mask by finding bytes that start with
    // 11x, 111x, and 1111. For each of these prefixes, we get a bitmask
    // and shift it forward by 1, 2, or 3. This loop should be unrolled by
    // the compiler, and the (n == 1) branch inside eliminated.
    uint32_t set = high;
    // set &= _mm256_movemask_epi8(_mm256_slli_epi16(bytes, 1));
    set &= _mm256_movemask_epi8(_mm256_add_epi16(bytes, bytes));
    // A bitmask of the actual continuation bytes in the input
    // Mark continuation bytes: those that have the high bit set but
    // not the next one
    const uint32_t cont = high ^ set;

    // We add the shifted mask here instead of ORing it, which would
    // be the more natural operation, so that this line can be done
    // with one lea. While adding could give a different result due
    // to carries, this will only happen for invalid UTF-8 sequences,
    // and in a way that won't cause it to pass validation. Reasoning:
    // Any bits for required continuation bytes come after the bits
    // for their leader bytes, and are all contiguous. For a carry to
    // happen, two of these bit sequences would have to overlap. If
    // this is the case, there is a leader byte before the second set
    // of required continuation bytes (and thus before the bit that
    // will be cleared by a carry). This leader byte will not be
    // in the continuation mask, despite being required. QEDish.
    req += static_cast<uint64_t>(set) << 1;
    set &= _mm256_movemask_epi8(_mm256_slli_epi16(bytes, 2));
    req += static_cast<uint64_t>(set) << 2;
    set &= _mm256_movemask_epi8(_mm256_slli_epi16(bytes, 3));
    req += static_cast<uint64_t>(set) << 3;

    // Check that continuation bytes match. We must cast req from uint64_t
    // (which holds the carry mask in the upper half) to uint32_t, which
    // zeroes out the upper bits
    if (cont != static_cast<uint32_t>(req)) {
        return false;
    }

    // Look up error masks for three consecutive nibbles.
    const __m256i nibbles = _mm256_set1_epi8(0x0F);
    const __m256i e_1 = _mm256_shuffle_epi8(error_1, _mm256_and_si256(_mm256_srli_epi16(shifted_bytes, 4), nibbles));
    const __m256i e_2 = _mm256_shuffle_epi8(error_2, _mm256_and_si256(shifted_bytes, nibbles));
    const __m256i e_3 = _mm256_shuffle_epi8(error_3, _mm256_and_si256(_mm256_srli_epi16(bytes, 4), nibbles));

    // Check if any bits are set in all three error masks
    if (!_mm256_testz_si256(_mm256_and_si256(e_1, e_2), e_3)) {
        return false;
    }

    // Save continuation bits and input bytes for the next round
    *last_cont = req >> sizeof(__m256i);
    return true;
}

bool IsUTF8(const char *data, DWORD length) noexcept {
    // Keep continuation bits from the previous iteration that carry over to
    // each input chunk vector
    uint32_t last_cont = 0;

    uint32_t offset = 0;
    // Deal with the input up until the last section of bytes
    if (length >= sizeof(__m256i)) {
        // We need a vector of the input byte stream shifted forward one byte.
        // Since we don't want to read the memory before the data pointer
        // (which might not even be mapped), for the first chunk of input just
        // use vector instructions.
        __m256i shifted_bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data));
        //__m256i shl_16 = _mm256_permute2x128_si256(shifted_bytes, _mm256_setzero_si256(), 0x03);
        __m256i shl_16 = _mm256_permute2x128_si256(shifted_bytes, shifted_bytes, 10);
        shifted_bytes = _mm256_alignr_epi8(shifted_bytes, shl_16, 15);

        // Loop over input in sizeof(__m256i)-byte chunks, as long as we can safely read
        // that far into memory
        for (; offset + sizeof(__m256i) < length; offset += sizeof(__m256i)) {
            const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + offset));
            if (!z_validate_vec_avx2(bytes, shifted_bytes, &last_cont)) {
                return false;
            }
            // Only load the shifted vector when another full iteration follows.
            if (offset + 2*sizeof(__m256i) < length) {
            shifted_bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + offset + sizeof(__m256i) - 1));
            }
        }
    }

    // Deal with any bytes remaining. Rather than making a separate scalar path,
    // just fill in a buffer, reading bytes only up to length, and load from that.
    if (offset < length) {
        uint8_t buffer[sizeof(__m256i) + 1];
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(buffer), _mm256_setzero_si256());
        buffer[sizeof(__m256i)] = 0;

        if (offset != 0) {
            buffer[0] = data[offset - 1];
        }
        __movsb(buffer + 1, reinterpret_cast<const uint8_t *>(data + offset), length - offset);

        const __m256i shifted_bytes = _mm256_loadu_si256(reinterpret_cast<__m256i *>(buffer));
        const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<__m256i *>(buffer + 1));
        if (!z_validate_vec_avx2(bytes, shifted_bytes, &last_cont)) {
            return false;
        }
    }

    // The input is valid if we don't have any more expected continuation bytes
    return last_cont == 0;
}

// end NP2_USE_AVX2
#elif NP2_USE_SSE2
#if defined(__clang__)
#include <tmmintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((__target__("ssse3"), __always_inline__)) static inline
#else
static __forceinline
#endif
bool z_validate_vec_sse4(__m128i bytes, __m128i shifted_bytes, uint32_t *last_cont) noexcept {
    // Error lookup tables for the first, second, and third nibbles
    const __m128i error_1 = _mm_setr_epi8(
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x06, 0x38
    );
    const __m128i error_2 = _mm_setr_epi8(
        0x0B, 0x01, 0x00, 0x00,
        0x10, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20,
        0x20, 0x24, 0x20, 0x20
    );
    const __m128i error_3 = _mm_setr_epi8(
        0x29, 0x29, 0x29, 0x29,
        0x29, 0x29, 0x29, 0x29,
        0x2B, 0x33, 0x35, 0x35,
        0x31, 0x31, 0x31, 0x31
    );

    // Quick skip for ascii-only input. If there are no bytes with the high bit
    // set, we don't need to do any more work. We return either valid or
    // invalid based on whether we expected any continuation bytes here.
    const uint32_t high = _mm_movemask_epi8(bytes);
    if (!high) {
        return *last_cont == 0;
    }

    // Which bytes are required to be continuation bytes
    uint32_t req = *last_cont;

    // Compute the continuation byte mask by finding bytes that start with
    // 11x, 111x, and 1111. For each of these prefixes, we get a bitmask
    // and shift it forward by 1, 2, or 3. This loop should be unrolled by
    // the compiler, and the (n == 1) branch inside eliminated.
    uint32_t set = high;
    // set &= _mm_movemask_epi8(_mm_slli_epi16(bytes, 1));
    set &= _mm_movemask_epi8(_mm_add_epi16(bytes, bytes));
    // A bitmask of the actual continuation bytes in the input
    // Mark continuation bytes: those that have the high bit set but
    // not the next one
    const uint32_t cont = high ^ set;
    // We add the shifted mask here instead of ORing it, which would
    // be the more natural operation, so that this line can be done
    // with one lea. While adding could give a different result due
    // to carries, this will only happen for invalid UTF-8 sequences,
    // and in a way that won't cause it to pass validation. Reasoning:
    // Any bits for required continuation bytes come after the bits
    // for their leader bytes, and are all contiguous. For a carry to
    // happen, two of these bit sequences would have to overlap. If
    // this is the case, there is a leader byte before the second set
    // of required continuation bytes (and thus before the bit that
    // will be cleared by a carry). This leader byte will not be
    // in the continuation mask, despite being required. QEDish.
    req += set << 1;
    set &= _mm_movemask_epi8(_mm_slli_epi16(bytes, 2));
    req += set << 2;
    set &= _mm_movemask_epi8(_mm_slli_epi16(bytes, 3));
    req += set << 3;

    // Check that continuation bytes match. We must cast req from uint32_t
    // (which holds the carry mask in the upper half) to uint16_t, which
    // zeroes out the upper bits
    if (cont != static_cast<uint16_t>(req)) {
        return false;
    }

    // Look up error masks for three consecutive nibbles.
    const __m128i nibbles = _mm_set1_epi8(0x0F);
    const __m128i e_1 = _mm_shuffle_epi8(error_1, _mm_and_si128(_mm_srli_epi16(shifted_bytes, 4), nibbles));
    const __m128i e_2 = _mm_shuffle_epi8(error_2, _mm_and_si128(shifted_bytes, nibbles));
    __m128i e_3 = _mm_shuffle_epi8(error_3, _mm_and_si128(_mm_srli_epi16(bytes, 4), nibbles));

    // Check if any bits are set in all three error masks
#if defined(__SSE4_1__)
    if (!_mm_testz_si128(_mm_and_si128(e_1, e_2), e_3)) {
        return false;
    }
#else
    e_3 = _mm_and_si128(_mm_and_si128(e_1, e_2), e_3);
    const int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(e_3, _mm_setzero_si128()));
    if (mask != 0xFFFF) {
        return false;
    }
#endif

    // Save continuation bits and input bytes for the next round
    *last_cont = req >> sizeof(__m128i);
    return true;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((__target__("ssse3")))
#endif
static inline bool z_validate_utf8_sse4(const char *data, DWORD length) noexcept {
    // Keep continuation bits from the previous iteration that carry over to
    // each input chunk vector
    uint32_t last_cont = 0;

    uint32_t offset = 0;
    // Deal with the input up until the last section of bytes
    if (length >= sizeof(__m128i)) {
        // We need a vector of the input byte stream shifted forward one byte.
        // Since we don't want to read the memory before the data pointer
        // (which might not even be mapped), for the first chunk of input just
        // use vector instructions.
        __m128i shifted_bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data));
        //shifted_bytes = _mm_alignr_epi8(shifted_bytes, _mm_setzero_si128(), 15);
        shifted_bytes = _mm_slli_si128(shifted_bytes, 1);

        // Loop over input in sizeof(__m128i)-byte chunks, as long as we can safely read
        // that far into memory
        for (; offset + sizeof(__m128i) < length; offset += sizeof(__m128i)) {
            const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + offset));
            if (!z_validate_vec_sse4(bytes, shifted_bytes, &last_cont)) {
                return false;
            }
            // Only load the shifted vector when another full iteration follows.
            if (offset + 2*sizeof(__m128i) < length) {
            shifted_bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + offset + sizeof(__m128i) - 1));
            }
        }
    }

    // Deal with any bytes remaining. Rather than making a separate scalar path,
    // just fill in a buffer, reading bytes only up to length, and load from that.
    if (offset < length) {
        uint8_t buffer[sizeof(__m128i) + 1];
        _mm_storeu_ps(reinterpret_cast<float *>(buffer), _mm_setzero_ps());
        buffer[sizeof(__m128i)] = 0;

        if (offset != 0) {
            buffer[0] = data[offset - 1];
        }
        __movsb(buffer + 1, reinterpret_cast<const uint8_t *>(data + offset), length - offset);

        const __m128i shifted_bytes = _mm_loadu_si128(reinterpret_cast<__m128i *>(buffer));
        const __m128i bytes = _mm_loadu_si128(reinterpret_cast<__m128i *>(buffer + 1));
        if (!z_validate_vec_sse4(bytes, shifted_bytes, &last_cont)) {
            return false;
        }
    }

    // The input is valid if we don't have any more expected continuation bytes
    return last_cont == 0;
}

static inline int did_cpu_supports_ssse3() noexcept {
    int info[4]{};
    __cpuid(info, 0x00000001);
    return info[2] & 0x0000200;
}
// end NP2_USE_SSE2
#endif

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See https://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#if !NP2_USE_AVX2
bool IsUTF8(const char *data, DWORD length) noexcept {
#if NP2_USE_SSE2
    if (true) {
        return z_validate_utf8_sse4(data, length);
    }
#endif // NP2_USE_SSE2

    enum {
        UTF8_ACCEPT = 0,
        UTF8_REJECT = 12,
    };

    static constexpr uint8_t utf8_dfa[] = {
        // The first part of the table maps bytes to character classes that
        // to reduce the size of the transition table and create bitmasks.
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
         1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
         7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
         8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

        // The second part is a transition table that maps a combination
        // of a state of the automaton and a character class to a state.
         0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
        12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
        12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
        12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
        12,36,12,12,12,12,12,12,12,12,12,12,
    };

    const uint8_t *pt = reinterpret_cast<const uint8_t *>(data);
    const uint8_t * const end = pt + length;
    UINT state = UTF8_ACCEPT;

#if 0 // NP2_USE_AVX2
    while (pt + sizeof(__m256i) <= end) {
        const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pt));
        const uint32_t mask = _mm256_movemask_epi8(chunk);
        if (mask) {
            // skip leading and trailing ASCII
            const DWORD trailing = (state != UTF8_ACCEPT)? 0 : np2_ctz(mask);
            DWORD leading;
            _BitScanReverse(&leading, mask);

            const uint8_t *temp = pt + trailing;
            const uint8_t * const endPtr = pt + leading + 1;
            do {
                state = utf8_dfa[256 + state + utf8_dfa[*temp++]];
            } while (temp < endPtr);
            if (state == UTF8_REJECT || (leading != 31 && state != UTF8_ACCEPT)) {
                return false;
            }
        } else if (state != UTF8_ACCEPT) {
            return false;
        }
        pt += sizeof(__m256i);
    }
    // end NP2_USE_AVX2
#elif NP2_USE_SSE2
    while (pt + 2*sizeof(__m128i) <= end) {
        const __m128i chunk1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt));
        const __m128i chunk2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt + sizeof(__m128i)));
        const uint32_t mask = mm_movemask_epi8(chunk1) | (mm_movemask_epi8(chunk2) << sizeof(__m128i));
        if (mask) {
            // skip leading and trailing ASCII
            const DWORD trailing = (state != UTF8_ACCEPT)? 0 : np2_ctz(mask);
            DWORD leading;
            _BitScanReverse(&leading, mask);

            const uint8_t *temp = pt + trailing;
            const uint8_t * const endPtr = pt + leading + 1;
            do {
                state = utf8_dfa[256 + state + utf8_dfa[*temp++]];
            } while (temp < endPtr);
            if (state == UTF8_REJECT || (leading != 31 && state != UTF8_ACCEPT)) {
                return false;
            }
        } else if (state != UTF8_ACCEPT) {
            return false;
        }
        pt += 2*sizeof(__m128i);
    }
    // end NP2_USE_SSE2
#elif defined(_WIN64)
    while (pt + sizeof(uint64_t) <= end) {
        const uint64_t val = *(reinterpret_cast<const uint64_t *>(pt));
        if (val & UINT64_C(0x8080808080808080)) {
#if 0
            state = utf8_dfa[256 + state + utf8_dfa[pt[0]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[1]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[2]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[3]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[4]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[5]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[6]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[7]]];
#else
            // same as above
            state = utf8_dfa[256 + state + utf8_dfa[val & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 8) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 16) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 24) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 32) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 40) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 48) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[val >> 56]];
#endif
            if (state == UTF8_REJECT) {
                return false;
            }
        } else if (state != UTF8_ACCEPT) {
            return false;
        }
        pt += sizeof(uint64_t);
    }
    // end _WIN64
#else
    while (pt + sizeof(uint32_t) <= end) {
        const uint32_t val = *(reinterpret_cast<const uint32_t *>(pt));
        if (val & 0x80808080U) {
#if 0
            state = utf8_dfa[256 + state + utf8_dfa[pt[0]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[1]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[2]]];
            state = utf8_dfa[256 + state + utf8_dfa[pt[3]]];
#else
            // same as above
            state = utf8_dfa[256 + state + utf8_dfa[val & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 8) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[(val >> 16) & 255]];
            state = utf8_dfa[256 + state + utf8_dfa[val >> 24]];
#endif
            if (state == UTF8_REJECT) {
                return false;
            }
        } else if (state != UTF8_ACCEPT) {
            return false;
        }
        pt += sizeof(uint32_t);
    }
    // end _WIN32
#endif

    while (pt < end) {
        state = utf8_dfa[256 + state + utf8_dfa[*pt++]];
    }

    return state == UTF8_ACCEPT;
}
#endif // !NP2_USE_AVX2

static const char *CheckUTF7(const char *pTest, DWORD nLength) noexcept {
    const char *pt = pTest;
#if NP2_USE_AVX512
    if (nLength >= sizeof(__m512i)) {
        const char * const end = pt + nLength - sizeof(__m512i);
        do {
            const __m512i chunk = _mm512_loadu_si512(pt);
            if (_mm512_movepi8_mask(chunk)) {
                return pt;
            }
            pt += sizeof(__m512i);
        } while (pt <= end);
    }

    nLength &= sizeof(__m512i) - 1;
    if (nLength != 0) {
        const __m512i chunk = _mm512_loadu_si512(pt);
        uint64_t mask = _mm512_movepi8_mask(chunk);
        mask = bit_zero_high_u64(mask, nLength);
        return mask ? pt : nullptr;
    }
    return nullptr;
#elif NP2_USE_AVX2
    if (nLength >= 2*sizeof(__m256i)) {
        const char * const end = pt + nLength - 2*sizeof(__m256i);
        do {
            const __m256i chunk1 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pt));
            const __m256i chunk2 = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pt + sizeof(__m256i)));
            const uint32_t mask = _mm256_movemask_epi8(_mm256_or_si256(chunk1, chunk2));
            if (mask) {
                return pt;
            }
            pt += 2*sizeof(__m256i);
        } while (pt <= end);
    }

    nLength &= 2*sizeof(__m256i) - 1;
    if (nLength != 0) {
        uint32_t mask = 0;
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pt));
        uint32_t last = _mm256_movemask_epi8(chunk);
        if (nLength > sizeof(__m256i)) {
            nLength &= sizeof(__m256i) - 1;
            mask = last;
            chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pt + sizeof(__m256i)));
            last = _mm256_movemask_epi8(chunk);
        }
        mask |= bit_zero_high_u32(last, nLength);
        return mask ? pt : nullptr;
    }
    return nullptr;
    // end NP2_USE_AVX2
#elif NP2_USE_SSE2
    if (nLength >= 4*sizeof(__m128i)) {
        const char * const end = pt + nLength - 4*sizeof(__m128i);
        do {
            const __m128i chunk1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt));
            const __m128i chunk2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt + sizeof(__m128i)));
            const __m128i chunk3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt + 2*sizeof(__m128i)));
            const __m128i chunk4 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt + 3*sizeof(__m128i)));
            const uint32_t mask = _mm_movemask_epi8(_mm_or_si128(_mm_or_si128(_mm_or_si128(chunk1, chunk2), chunk3), chunk4));
            if (mask) {
                return pt;
            }
            pt += 4*sizeof(__m128i);
        } while (pt <= end);
    }

    nLength &= 4*sizeof(__m128i) - 1;
    if (nLength != 0) {
        const char * const end = pt;
        __m128i chunk = _mm_setzero_si128();
#if defined(__clang__)
        #pragma clang loop unroll(disable)
#endif
        for (UINT i = 0; i < nLength / sizeof(__m128i); i++) {
            chunk = _mm_or_si128(chunk, _mm_loadu_si128(reinterpret_cast<const __m128i *>(pt)));
            pt += sizeof(__m128i);
        }

        uint32_t mask = _mm_movemask_epi8(chunk);
        nLength &= sizeof(__m128i) - 1;
        if (nLength != 0) {
            const uint32_t last = _mm_movemask_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i *>(pt)));
            mask |= bit_zero_high_u32(last, nLength);
        }
        return mask ? end : nullptr;
    }
    return nullptr;
    // end NP2_USE_SSE2
#elif defined(_WIN64)
    const char * const end = pt + nLength;
    nLength &= sizeof(uint64_t) - 1;
    nLength = (sizeof(uint64_t) - nLength)*8;
    do {
        uint64_t value = *(reinterpret_cast<const uint64_t *>(pt));
        pt += sizeof(uint64_t);
        value &= UINT64_C(0x8080808080808080);
        if (value != 0) {
            if (pt > end) {
                value <<= nLength;
                if (value == 0) {
                    return nullptr;
                }
            }
            return pt - sizeof(uint64_t);
        }
    } while (pt < end);
    return nullptr;
    // end _WIN64
#else
    const char * const end = pt + nLength;
    nLength &= sizeof(uint32_t) - 1;
    nLength = (sizeof(uint32_t) - nLength)*8;
    do {
        uint32_t value = *(reinterpret_cast<const uint32_t *>(pt));
        pt += sizeof(uint32_t);
        value &= 0x80808080U;
        if (value != 0) {
            if (pt > end) {
                value <<= nLength;
                if (value == 0) {
                    return nullptr;
                }
            }
            return pt - sizeof(uint32_t);
        }
    } while (pt < end);
    return nullptr;
    // end _WIN32
#endif
}

}
LineEndings AnalyzeLineEndings(std::string_view text, int preferred) noexcept {
    LineEndings result;
    result.iEOLMode = preferred >= 0 && preferred <= 2 ? preferred : 0;
    if (!text.empty()) EditDetectEOLMode(text.data(), text.size(), result);
    return result;
}
bool IsValidUtf8(std::string_view text) noexcept {
    constexpr size_t chunkSize = 16 * 1024 * 1024;
    while (!text.empty()) {
        size_t count = (std::min)(text.size(), chunkSize);
        if (count < text.size()) {
            while (count && (static_cast<unsigned char>(text[count]) & 0xC0) == 0x80) --count;
            if (!count) return false;
        }
        if (!IsUTF8(text.data(), static_cast<DWORD>(count))) return false;
        text.remove_prefix(count);
    }
    return true;
}
bool IsAscii(std::string_view text) noexcept {
    // CheckUTF7 expects up to 64 bytes of readable padding on the last load.
    while (text.size() >= 64) {
        const size_t count = (std::min)(text.size() & ~size_t(63), size_t(16 * 1024 * 1024));
        if (CheckUTF7(text.data(), static_cast<DWORD>(count))) return false;
        text.remove_prefix(count);
    }
    char tail[64]{};
    if (!text.empty()) std::memcpy(tail, text.data(), text.size());
    return !CheckUTF7(tail, static_cast<DWORD>(text.size()));
}
int DetectUtf16(std::string_view text) noexcept {
    if (text.size() < 2 || text.size() % 2) return 0;
    char sample[4096 + 64]{};
    size_t count = (std::min)(text.size(), size_t(4096));
    std::memcpy(sample, text.data(), count);
    size_t even = 0, odd = 0;
    const auto* ptr = reinterpret_cast<const __m128i*>(sample);
    for (size_t at = 0; at < count; at += 16) {
        unsigned mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128(ptr++), _mm_setzero_si128()));
        const size_t remaining = count - at;
        if (remaining < 16) mask &= (1u << remaining) - 1;
        even += np2_popcount(mask & 0x5555);
        odd += np2_popcount(mask & 0xaaaa);
    }
    if (odd > 2 && odd > even * 4) return 1;
    if (even > 2 && even > odd * 4) return 2;
    return DetectUTF16LatinExt(sample, static_cast<DWORD>(count));
}
int DetectIndentation(std::string_view text) noexcept {
    if (text.empty()) return 0;
    const char* lpData = text.data();
    size_t cbData = text.size();
    // code based on SciTEBase::DiscoverIndentSetting().
    cbData = (std::min<size_t>)(cbData, 1*1024*1024);
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(lpData);
    const uint8_t * const end = ptr + cbData;
    #define MAX_DETECTED_TAB_WIDTH  8
    // line count for ambiguous lines, line indented by 1 to 8 spaces, line starts with tab.
    uint32_t indentLineCount[1 + MAX_DETECTED_TAB_WIDTH + 1 + (6*NP2_USE_AVX2)]{};
    int prevIndentCount = 0;
    int prevTabWidth = 0;

#if NP2_USE_AVX2
    const __m256i vectCR = _mm256_set1_epi8('\r');
    const __m256i vectLF = _mm256_set1_epi8('\n');
labelStart:
#elif NP2_USE_SSE2
    const __m128i vectCR = _mm_set1_epi8('\r');
    const __m128i vectLF = _mm_set1_epi8('\n');
labelStart:
#endif
    while (ptr < end) {
        switch (*ptr++) {
        case '\t':
            ++indentLineCount[MAX_DETECTED_TAB_WIDTH + 1];
            break;

        case ' ': {
            int indentCount = 1;
            while (ptr < end && *ptr == ' ') {
                ++ptr;
                ++indentCount;
            }
            if ((indentCount & 1) != 0 && ptr < end && *ptr == '*') {
                // fix alignment space before star in Javadoc style comment: ` * comment content`
                --indentCount;
                if (indentCount == 0) {
                    break;
                }
            }
            if (indentCount != prevIndentCount) {
                const int delta = abs(indentCount - prevIndentCount);
                prevIndentCount = indentCount;
                // TODO: fix other (e.g. function argument) alignment spaces.
                if (delta <= MAX_DETECTED_TAB_WIDTH) {
                    prevTabWidth = (std::min)(delta, indentCount);
                } else {
                    prevTabWidth = 0;
                }
            }
            ++indentLineCount[prevTabWidth];
        } break;

        case '\r':
        case '\n':
            continue;

        default:
            prevIndentCount = 0;
            break;
        }

        // skip to line end
#if NP2_USE_AVX2
        while (static_cast<size_t>(end - ptr) >= sizeof(__m256i)) {
            const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(ptr));
            const uint32_t mask = mm256_movemask_epi8(_mm256_or_si256(_mm256_cmpeq_epi8(chunk, vectCR), _mm256_cmpeq_epi8(chunk, vectLF)));
            if (mask != 0) {
                const uint32_t trailing = np2_ctz(mask);
                ptr += trailing + 1;
                goto labelStart;
            }
            ptr += sizeof(__m256i);
        }
#elif NP2_USE_SSE2
        while (static_cast<size_t>(end - ptr) >= sizeof(__m128i)) {
            const __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i *>(ptr));
            const uint32_t mask = mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(chunk, vectCR), _mm_cmpeq_epi8(chunk, vectLF)));
            if (mask != 0) {
                const uint32_t trailing = np2_ctz(mask);
                ptr += trailing + 1;
                goto labelStart;
            }
            ptr += sizeof(__m128i);
        }
#endif
        constexpr uint32_t mask = ((1 << '\r') - 1) ^ (1 << '\n');
        uint8_t ch;
        while (ptr < end && ((ch = *ptr++) > '\r' || ((mask >> ch) & 1) != 0)) {
            // nop
        }
    }

    // reduce code size for the unrolled loop
#if NP2_USE_AVX512
    const __m512i chunk = _mm512_loadu_si512(indentLineCount);
    const __m512i maxAll = _mm512_set1_epi32(_mm512_reduce_max_epu32(chunk));
    const uint32_t mask = _mm512_cmpeq_epu32_mask(chunk, maxAll);
    prevTabWidth = np2_ctz(mask);
#elif NP2_USE_AVX2
    const __m256i chunk1 = _mm256_loadu_si256(reinterpret_cast<__m256i *>(indentLineCount));
    const __m256i chunk2 = _mm256_loadu_si256(reinterpret_cast<__m256i *>(indentLineCount + 8));
    __m128i maxAll = _mm_max_epu32(_mm256_castsi256_si128(chunk1), _mm256_castsi256_si128(chunk2));
    maxAll = _mm_max_epu32(maxAll, _mm256_extracti128_si256(chunk1, 1));
    maxAll = _mm_max_epu32(maxAll, _mm_shuffle_epi32(maxAll, _MM_SHUFFLE(0, 1, 2, 3)));
    maxAll = _mm_max_epu32(maxAll, _mm_shuffle_epi32(maxAll, _MM_SHUFFLE(1, 0, 3, 2)));
    const __m256i chunk = _mm256_broadcastd_epi32(maxAll);
    uint32_t mask = _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(chunk, chunk1)));
    mask |= static_cast<uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(chunk, chunk2)))) << 8;
    prevTabWidth = np2_ctz(mask);

#else
    prevTabWidth = 0;
    for (int i = 1; i < MAX_DETECTED_TAB_WIDTH + 2; i++) {
        if (indentLineCount[i] > indentLineCount[prevTabWidth]) {
            prevTabWidth = i;
        }
    }
#endif
    return prevTabWidth;
}

}

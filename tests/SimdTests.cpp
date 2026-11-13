#include "SimdText.h"
#include "SimdGraphics.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Check(std::string_view text) {
    size_t counts[3]{};
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') { ++counts[0]; ++i; }
            else ++counts[1];
        } else if (text[i] == '\n') ++counts[2];
    }
    for (int preferred = 0; preferred < 3; ++preferred) {
        auto actual = simd::AnalyzeLineEndings(text, preferred);
        Require(std::equal(std::begin(counts), std::end(counts), actual.linesCount), "SIMD EOL counts differ");
        auto maximum = (std::max)({counts[0], counts[1], counts[2]});
        int mode = preferred;
        if (counts[mode] != maximum) mode = counts[0] == maximum ? 0 : counts[2] == maximum ? 2 : 1;
        Require(actual.iEOLMode == mode, "EOL majority/tie selection differs");
        Require(actual.totalLineCount == counts[0] + counts[1] + counts[2] + 1, "Line allocation count differs");
        Require(actual.bInconsistent == ((counts[0] != 0) + (counts[1] != 0) + (counts[2] != 0) > 1), "Mixed EOL detection differs");
    }
    bool ascii = std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 128; });
    Require(simd::IsAscii(text) == ascii, "SIMD ASCII detection differs");
    bool utf8 = text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) != 0;
    Require(simd::IsValidUtf8(text) == utf8, "SIMD UTF-8 differs from strict Windows validation");
    simd::DetectIndentation(text);
    simd::DetectUtf16(text);
}
int main() {
    try {
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        const size_t page = system.dwPageSize;
        auto* memory = static_cast<char*>(VirtualAlloc(nullptr, page * 3, MEM_RESERVE, PAGE_NOACCESS));
        Require(memory != nullptr, "Reserve guard pages failed");
        struct Allocation { void* p; ~Allocation() { VirtualFree(p, 0, MEM_RELEASE); } } allocation{memory};
        Require(VirtualAlloc(memory + page, page, MEM_COMMIT, PAGE_READWRITE) != nullptr, "Commit page failed");
        std::mt19937 random(0x12345678);
        Check({});
        for (size_t size = 0; size <= 512; ++size) {
            for (int variant = 0; variant < 6; ++variant) {
                std::string text(size, 'a');
                for (auto& c : text) {
                    auto value = random();
                    if (variant == 0) c = value & 1 ? '\r' : '\n';
                    if (variant == 1) c = static_cast<char>(value);
                    if (variant == 2) c = " \r\n\txyz"[value % 7];
                }
                // Both ends abut inaccessible pages in separate runs.
                for (char* target : {memory + page, memory + 2 * page - size}) {
                    std::memcpy(target, text.data(), size);
                    Check(std::string_view(target, size));
                }
            }
        }
        for (size_t offset = 0; offset < 130; ++offset) {
            for (const std::string sequence : {std::string("\xc2\xa2"), std::string("\xe4\xb8\xad"), std::string("\xf0\x9f\x98\x80"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xc0\x80")}) {
                for (size_t count = 1; count <= sequence.size(); ++count) {
                    auto text = std::string(offset, 'x') + sequence.substr(0, count);
                    Check(text); text += std::string(130, 'x'); Check(text);
                }
            }
        }
        // Validate UTF-8 crossing the 16 MiB wrapper boundary, including truncation.
        auto large = std::string(16 * 1024 * 1024 - 1, 'x') + "\xf0\x9f\x98\x80";
        Require(simd::IsValidUtf8(large), "Chunk boundary rejected valid UTF-8");
        large.pop_back();
        Require(!simd::IsValidUtf8(large), "Chunk boundary accepted truncated UTF-8");
        Require(simd::DetectIndentation("a\r  b\r    c\r  d\r") == 2, "CR indentation detection failed");
        Require(simd::DetectIndentation("a\n\tb\n\tc\n") == 9, "Tab indentation detection failed");
        Require(simd::DetectUtf16(std::string("a\0b\0c\0", 6)) == 1, "UTF16 LE detection failed");
        Require(simd::DetectUtf16(std::string("\0a\0b\0c", 6)) == 2, "UTF16 BE detection failed");
        Require(simd::NarrowAsciiTag(L"input") == "input", "SIMD tag conversion failed");
        for (size_t size = 0; size < 65; ++size) Require(simd::NarrowAsciiTag(std::wstring(size, L'z')) == std::string(size, 'z'), "Tag vector boundary failed");
        for (unsigned i = 0; i < 1000; ++i) {
            COLORREF a = random() & 0xffffff, b = random() & 0xffffff;
            int r1 = GetRValue(a), g1 = GetGValue(a), b1 = GetBValue(a);
            int r2 = GetRValue(b), g2 = GetGValue(b), b2 = GetBValue(b);
            bool expected = abs(3*r1+5*g1+b1-3*r2-6*g2-b2) >= 400 || abs(r1-r2)+abs(g1-g2)+abs(b1-b2) >= 400;
            Require(simd::VerifyContrast(a,b) == expected, "Color contrast differs");
        }
        for (int width = 1; width <= 17; ++width) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = 1;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            void* pixels = nullptr;
            auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            Require(bitmap != nullptr, "Create test DIB failed");
            struct Bitmap { HBITMAP p; ~Bitmap() { DeleteObject(p); } } owner{bitmap};
            for (BYTE alpha : {BYTE(0), BYTE(1), BYTE(96), BYTE(254), BYTE(255)}) {
                auto* data = static_cast<RGBQUAD*>(pixels);
                for (int i=0; i<width; ++i) data[i] = {37, 139, 241, 77};
                Require(simd::BitmapAlphaBlend(bitmap, RGB(11,22,33), alpha), "Blend rejected DIB");
                for (int i=0; i<width; ++i) {
                    Require(data[i].rgbBlue == (37*alpha+33*(255-alpha))/255 && data[i].rgbGreen == (139*alpha+22*(255-alpha))/255 && data[i].rgbRed == (241*alpha+11*(255-alpha))/255 && data[i].rgbReserved == 77, "Bitmap blend/tail differs");
                }
                for (int i=0; i<width; ++i) data[i] = {37, 139, 241, alpha};
                Require(simd::BitmapMergeAlpha(bitmap, RGB(11,22,33)), "Merge rejected DIB");
                for (int i=0; i<width; ++i) Require(data[i].rgbBlue == (37*alpha+33*(255-alpha))/255 && data[i].rgbRed == (241*alpha+11*(255-alpha))/255 && data[i].rgbReserved == 255, "Bitmap merge/tail differs");
            }
        }
        std::cout << "SIMD guard-page, encoding, newline, indentation, graphics checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

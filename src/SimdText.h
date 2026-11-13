#pragma once
#include <cstddef>
#include <string_view>
namespace simd {
struct LineEndings {
    size_t linesCount[3]{}; // Scintilla: CRLF, CR, LF
    int iEOLMode = 0;
    bool bInconsistent = false;
    size_t totalLineCount = 1;
};
LineEndings AnalyzeLineEndings(std::string_view text, int preferred = 0) noexcept;
bool IsValidUtf8(std::string_view text) noexcept;
bool IsAscii(std::string_view text) noexcept;
// Returns 0 (unknown), 1 (little endian), or 2 (big endian).
int DetectUtf16(std::string_view text) noexcept;
// 0: no recommendation; 1..8: spaces; 9: tabs.
int DetectIndentation(std::string_view text) noexcept;
}

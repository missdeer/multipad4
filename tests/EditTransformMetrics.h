#pragma once
#include "FileIO.h"
#include <cstddef>
struct EditorDocument;
namespace transformtest {
struct Metrics {
    size_t rangeCalls = 0, rangeBytes = 0, splitBytes = 0;
    size_t eagerBytes = 0, lineCopyBytes = 0;
    size_t utf16Calls = 0, utf16Bytes = 0, widthCalls = 0;
};
inline thread_local Metrics metrics;
inline thread_local bool (*baseline)(EditorDocument&, UINT) = nullptr;
inline std::wstring Decode(const std::string& text) {
    ++metrics.utf16Calls; metrics.utf16Bytes += text.size();
    return files::FromUtf8(text);
}
}

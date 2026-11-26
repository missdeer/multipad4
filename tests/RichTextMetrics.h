#pragma once
#include "FileIO.h"
struct EditorDocument;
namespace richtexttest {
struct Metrics { size_t utf16Calls = 0, utf16Bytes = 0; };
inline thread_local Metrics metrics;
inline thread_local void (*baseline)(EditorDocument&) = nullptr;
inline std::wstring Decode(const std::string& text) {
    ++metrics.utf16Calls; metrics.utf16Bytes += text.size();
    return files::FromUtf8(text);
}
}

#pragma once
#include "FileSearch.h"
namespace files {
struct ReplacePattern {
    std::string needle, replacement;
    int flags = 0;
    bool captures = false;
};
struct ReplaceResult {
    Text text;
    FileSearchResult hits;
    size_t replacements = 0;
};
// Uses the pinned Scintilla document engine without creating any window or renderer.
Text ReadReplacement(const std::filesystem::path& path, const std::atomic<bool>& cancel);
ReplaceResult ReplaceContents(Text text, const ReplacePattern& pattern, const std::atomic<bool>& cancel);
struct ReplaceVersion {
    unsigned long volume = 0, indexHigh = 0, indexLow = 0, links = 0;
    unsigned long long size = 0, write = 0;
    bool operator==(const ReplaceVersion&) const = default;
};
ReplaceVersion ReplacementVersion(const std::filesystem::path& path);
void CommitReplacement(const std::filesystem::path& path, const Text& text, const ReplaceVersion& version,
    const std::atomic<bool>& cancel, const std::function<void()>& committed = {});
}

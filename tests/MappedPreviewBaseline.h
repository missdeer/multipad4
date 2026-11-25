// Benchmark fixture: original mapped previews at e876e48.
#pragma once
#include "../src/EditorDocument.h"
inline std::vector<SearchMatch> BaselineMappedMatches(EditorDocument& document, const large::Document::Snapshot& snapshot, const std::vector<large::SearchHit>& hits) {
    std::vector<SearchMatch> matches;
    auto shared = std::make_shared<const large::Document::Snapshot>(snapshot);
    large::Search reader(snapshot, document.encoding, {L"", false, true});
    for (const auto& hit : hits) {
        const auto line = large::Document::LineFromPosition(snapshot, hit.start);
        const auto lineStart = large::Document::PositionFromLine(snapshot, line);
        const auto unit = document.encoding == files::Encoding::Utf16LE || document.encoding == files::Encoding::Utf16LEPlain || document.encoding == files::Encoding::Utf16BE || document.encoding == files::Encoding::Utf16BEPlain ? 2u : 1u;
        auto previewStart = (std::max)(lineStart, hit.start > 256 ? hit.start - 256 : 0); previewStart -= previewStart % unit;
        const bool unicode8 = document.encoding == files::Encoding::Utf8 || document.encoding == files::Encoding::Utf8Bom;
        if (unit == 1 && !unicode8) previewStart = hit.start;
        auto rawPreview = large::Document::Read(snapshot, previewStart, 2048);
        if (unit == 1 && (document.encoding == files::Encoding::Utf8 || document.encoding == files::Encoding::Utf8Bom)) {
            size_t skip = 0; while (skip < rawPreview.size() && (static_cast<unsigned char>(rawPreview[skip]) & 0xC0) == 0x80) ++skip;
            previewStart += skip; rawPreview.erase(0, skip);
            if (previewStart + rawPreview.size() < large::Document::Size(snapshot)) { size_t count = rawPreview.size() > 4 ? rawPreview.size() - 4 : 0; while (count && (static_cast<unsigned char>(rawPreview[count]) & 0xC0) == 0x80) --count; rawPreview.resize(count); }
        } else if (unit == 2) {
            rawPreview.resize(rawPreview.size() & ~size_t(1));
            auto value = [&](size_t at) { auto a = static_cast<unsigned char>(rawPreview[at]), b = static_cast<unsigned char>(rawPreview[at + 1]); return document.encoding == files::Encoding::Utf16LE || document.encoding == files::Encoding::Utf16LEPlain ? a | (b << 8) : (a << 8) | b; };
            if (rawPreview.size() >= 2 && value(0) >= 0xDC00 && value(0) <= 0xDFFF) { previewStart += 2; rawPreview.erase(0, 2); }
            if (rawPreview.size() >= 2) { auto c = value(rawPreview.size() - 2); if (c >= 0xD800 && c <= 0xDBFF) rawPreview.resize(rawPreview.size() - 2); }
        }
        if (unit == 1 && !unicode8 && previewStart + rawPreview.size() < large::Document::Size(snapshot)) {
            const auto page = files::CodePage(document.encoding); size_t at = 0;
            while (at < rawPreview.size()) {
                auto c = static_cast<unsigned char>(rawPreview[at]); size_t width = IsDBCSLeadByteEx(page, c) ? 2 : 1;
                if (page == 54936 && c >= 0x81 && c <= 0xFE && at + 1 < rawPreview.size()) { auto n = static_cast<unsigned char>(rawPreview[at + 1]); width = n >= 0x30 && n <= 0x39 ? 4 : 2; }
                if (at + width > rawPreview.size()) { rawPreview.resize(at); break; } at += width;
            }
        }
        auto text = files::Decode(rawPreview, document.encoding, false).utf8;
        auto newline = text.find_first_of("\r\n"); if (newline != std::string::npos) text.resize(newline);
        auto prefix = reader.Utf8(previewStart, hit.start - previewStart).size();
        auto length = hit.end <= previewStart + rawPreview.size() ? reader.Utf8(hit.start, hit.end - hit.start).size() : text.size();
        SearchMatch match; match.start = static_cast<LRESULT>(prefix); match.end = static_cast<LRESULT>(prefix + length);
        match.line = static_cast<LRESULT>(line); match.lineStart = 0; match.mappedSource = shared;
        match.nativeStart = hit.start; match.nativeEnd = hit.end; match.nativeLineStart = lineStart;
        match.preview = std::move(text); matches.push_back(std::move(match));
    }
    return matches;
}
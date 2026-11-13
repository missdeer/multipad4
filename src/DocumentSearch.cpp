#include "EditClipboard.h"
#include "EditSelection.h"
#include "EditorDocument.h"
#include <stdexcept>
#include <string_view>
namespace {
std::string TransformSlashes(const std::wstring &text) {
    std::string output;
    std::wstring segment;
    auto flush = [&] {
        output += files::ToUtf8(segment);
        segment.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t character = text[i];
        if (character != L'\\' || i + 1 == text.size()) {
            segment += character;
            continue;
        }
        wchar_t escape = text[i + 1];
        auto position = std::wstring_view(L"\\0abefnrtv").find(escape);
        if (position != std::wstring_view::npos) {
            segment += L"\\\0\a\b\x1B\f\n\r\t\v"[position];
            ++i;
            continue;
        }
        if (escape == L'x' || escape == L'u') {
            const size_t count = escape == L'x' ? 2 : 4;
            unsigned value = 0;
            bool valid = i + 1 + count < text.size();
            for (size_t digit = 0; valid && digit < count; ++digit) {
                wchar_t c = text[i + 2 + digit];
                int hex = c >= L'0' && c <= L'9'   ? c - L'0'
                          : c >= L'a' && c <= L'f' ? c - L'a' + 10
                          : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                                   : -1;
                if (hex < 0)
                    valid = false;
                else
                    value = (value << 4) | static_cast<unsigned>(hex);
            }
            if (valid) {
                if (escape == L'x') {
                    flush();
                    output += static_cast<char>(value);
                } else
                    segment += static_cast<wchar_t>(value);
                i += count + 1;
                continue;
            }
        }
        // Unknown/incomplete escapes remain literal, as in EditEncoding.cpp.
        segment += L'\\';
    }
    flush();
    return output;
}
std::string Needle(EditorDocument &d) {
    auto text = d.search.transformBackslashes ? TransformSlashes(d.search.findText) : files::ToUtf8(d.search.findText);
    if (d.search.wildcard) {
        std::string result;
        for (char c : text) {
            if (c == '*')
                result += ".*";
            else if (c == '?')
                result += '.';
            else {
                if (std::string_view(".[](){}+^$|\\").find(c) != std::string_view::npos)
                    result += '\\';
                result += c;
            }
        }
        text = std::move(result);
    }
    return text;
}
int Flags(EditorDocument &d) {
    return d.search.flags | (d.search.useBoostRegex ? SCFIND_CXX11REGEX : 0) |
           ((d.search.regex || d.search.wildcard) ? SCFIND_REGEXP | SCFIND_POSIX : 0) |
           (d.search.dotMatchesNewline ? SCFIND_REGEX_DOT_ALL : 0);
}
LRESULT Search(EditorDocument &d, const std::string &needle, LRESULT from, LRESULT to) {
    d.Send(SCI_SETSTATUS, SC_STATUS_OK);
    d.Send(SCI_SETSEARCHFLAGS, Flags(d));
    d.Send(SCI_SETTARGETRANGE, from, to);
    auto result = d.Send(SCI_SEARCHINTARGET, needle.size(), reinterpret_cast<LPARAM>(needle.data()));
    if (d.Send(SCI_GETSTATUS) == SC_STATUS_WARN_REGEX)
        throw std::runtime_error("Invalid regular expression.");
    return result;
}
std::string Replacement(EditorDocument &d) {
    if (d.search.replaceText == L"^c") {
        std::wstring text;
        if (!OpenEditClipboard(d.hwnd))
            throw std::runtime_error("Cannot read clipboard text; document retained.");
        {
            auto memory = GetClipboardData(CF_UNICODETEXT);
            if (memory) {
                auto *value = static_cast<const wchar_t *>(GlobalLock(memory));
                if (value) {
                    text = value;
                    GlobalUnlock(memory);
                }
            }
            CloseClipboard();
        }
        return files::ToUtf8(text);
    }
    return d.search.transformBackslashes ? TransformSlashes(d.search.replaceText) : files::ToUtf8(d.search.replaceText);
}
UINT ReplaceMessage(const EditorDocument &d) {
    return (d.search.regex || d.search.wildcard) && d.search.replaceText != L"^c" ? SCI_REPLACETARGETRE
                                                                                  : SCI_REPLACETARGET;
}

} // namespace
large::SearchOptions EditorDocument::MappedSearchOptions() const {
    auto& self = const_cast<EditorDocument&>(*this);
    auto needle = Needle(self);
    return {files::FromUtf8(needle), search.regex || search.wildcard, (search.flags & SCFIND_MATCHCASE) != 0,
        (search.flags & SCFIND_WHOLEWORD) != 0, (search.flags & SCFIND_WORDSTART) != 0, search.dotMatchesNewline};
}
std::string EditorDocument::SearchReplacement() { return Replacement(*this); }
void EditorDocument::ValidateSearch(bool replacing) {
    if (FastView()) {
        large::Search pattern(fastView->Model().Capture(), encoding, MappedSearchOptions());
        if (replacing) pattern.Native(Replacement(*this));
    } else {
        const auto flags = Send(SCI_GETSEARCHFLAGS), start = Send(SCI_GETTARGETSTART), end = Send(SCI_GETTARGETEND);
        struct Restore { EditorDocument& d; LRESULT flags, start, end; ~Restore() { d.Send(SCI_SETSEARCHFLAGS, flags); d.Send(SCI_SETTARGETRANGE, start, end); } } restore{*this, flags, start, end};
        Search(*this, Needle(*this), 0, Send(SCI_GETLENGTH)); if (replacing) Replacement(*this);
    }
}
std::vector<SearchMatch> EditorDocument::MappedMatches(const large::Document::Snapshot& snapshot, const std::vector<large::SearchHit>& hits) {
    std::vector<SearchMatch> matches;
    auto shared = std::make_shared<const large::Document::Snapshot>(snapshot);
    large::Search reader(snapshot, encoding, {L"", false, true});
    for (const auto& hit : hits) {
        const auto line = large::Document::LineFromPosition(snapshot, hit.start);
        const auto lineStart = large::Document::PositionFromLine(snapshot, line);
        const auto unit = encoding == files::Encoding::Utf16LE || encoding == files::Encoding::Utf16LEPlain || encoding == files::Encoding::Utf16BE || encoding == files::Encoding::Utf16BEPlain ? 2u : 1u;
        auto previewStart = (std::max)(lineStart, hit.start > 256 ? hit.start - 256 : 0); previewStart -= previewStart % unit;
        const bool unicode8 = encoding == files::Encoding::Utf8 || encoding == files::Encoding::Utf8Bom;
        if (unit == 1 && !unicode8) previewStart = hit.start;
        auto rawPreview = large::Document::Read(snapshot, previewStart, 2048);
        if (unit == 1 && (encoding == files::Encoding::Utf8 || encoding == files::Encoding::Utf8Bom)) {
            size_t skip = 0; while (skip < rawPreview.size() && (static_cast<unsigned char>(rawPreview[skip]) & 0xC0) == 0x80) ++skip;
            previewStart += skip; rawPreview.erase(0, skip);
            if (previewStart + rawPreview.size() < large::Document::Size(snapshot)) { size_t count = rawPreview.size() > 4 ? rawPreview.size() - 4 : 0; while (count && (static_cast<unsigned char>(rawPreview[count]) & 0xC0) == 0x80) --count; rawPreview.resize(count); }
        } else if (unit == 2) {
            rawPreview.resize(rawPreview.size() & ~size_t(1));
            auto value = [&](size_t at) { auto a = static_cast<unsigned char>(rawPreview[at]), b = static_cast<unsigned char>(rawPreview[at + 1]); return encoding == files::Encoding::Utf16LE || encoding == files::Encoding::Utf16LEPlain ? a | (b << 8) : (a << 8) | b; };
            if (rawPreview.size() >= 2 && value(0) >= 0xDC00 && value(0) <= 0xDFFF) { previewStart += 2; rawPreview.erase(0, 2); }
            if (rawPreview.size() >= 2) { auto c = value(rawPreview.size() - 2); if (c >= 0xD800 && c <= 0xDBFF) rawPreview.resize(rawPreview.size() - 2); }
        }
        if (unit == 1 && !unicode8 && previewStart + rawPreview.size() < large::Document::Size(snapshot)) {
            const auto page = files::CodePage(encoding); size_t at = 0;
            while (at < rawPreview.size()) {
                auto c = static_cast<unsigned char>(rawPreview[at]); size_t width = IsDBCSLeadByteEx(page, c) ? 2 : 1;
                if (page == 54936 && c >= 0x81 && c <= 0xFE && at + 1 < rawPreview.size()) { auto n = static_cast<unsigned char>(rawPreview[at + 1]); width = n >= 0x30 && n <= 0x39 ? 4 : 2; }
                if (at + width > rawPreview.size()) { rawPreview.resize(at); break; } at += width;
            }
        }
        auto text = files::Decode(rawPreview, encoding, false).utf8;
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
bool EditorDocument::FindNext(bool backwards, const std::function<bool(bool)> &confirmWrap) {
    if (FastView()) {
        if (search.findText.empty()) return false;
        fastView->StartSearch(MappedSearchOptions(), backwards, search.wrap); return true;
    }
    if (search.findText.empty())
        return false;
    auto needle = Needle(*this);
    if (needle.empty())
        return false;
    const auto length = Send(SCI_GETLENGTH);
    auto start = Send(backwards ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND);
    const auto boundary = start;
    bool exhausted = false;
    if ((search.regex || search.wildcard) && search.lastMatchStart == start && search.lastMatchEnd == start) {
        if (backwards) {
            if (start == 0)
                exhausted = true;
            else
                start = Send(SCI_POSITIONBEFORE, start);
        } else {
            if (start == length)
                exhausted = true;
            else
                start = Send(SCI_POSITIONAFTER, start);
        }
    }
    auto found = exhausted ? -1 : Search(*this, needle, start, backwards ? 0 : length);
    if (found < 0 && search.wrap && (backwards ? boundary < length : boundary > 0)) {
        if (confirmWrap && !confirmWrap(backwards))
            return false;
        found = Search(*this, needle, backwards ? length : 0, boundary);
    }
    if (found < 0) {
        // Keep the exhausted zero-width position so repeated no-wrap searches
        // cannot alternate between not-found and the same end-of-document match.
        return false;
    }
    auto end = Send(SCI_GETTARGETEND);
    search.lastMatchStart = found;
    search.lastMatchEnd = end;
    Send(SCI_SETSEL, found, end);
    Send(SCI_SCROLLCARET);
    return true;
}
bool EditorDocument::ReplaceOne() {
    if (FastView()) return fastView->ReplaceMatch(MappedSearchOptions(), Replacement(*this),
        (search.regex || search.wildcard) && search.replaceText != L"^c");
    if (Send(SCI_GETREADONLY) || search.findText.empty())
        return false;
    auto needle = Needle(*this);
    if (needle.empty())
        return false;
    auto start = Send(SCI_GETSELECTIONSTART), end = Send(SCI_GETSELECTIONEND);
    if (start == end && (search.lastMatchStart != start || search.lastMatchEnd != end))
        return false;
    // Lookarounds may inspect text beyond the selected match.
    if (Search(*this, needle, start, Send(SCI_GETLENGTH)) != start || Send(SCI_GETTARGETEND) != end)
        return false;
    auto replacement = Replacement(*this);
    auto size = Send(ReplaceMessage(*this), replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    Send(SCI_SETSEL, start, start + size);
    search.lastMatchStart = end == start ? start + size : start;
    search.lastMatchEnd = start + size;
    return true;
}
size_t EditorDocument::ReplaceAll(bool selectionOnly) {
    if (FastView()) return fastView->ReplaceEvery(MappedSearchOptions(), Replacement(*this), (search.regex || search.wildcard) && search.replaceText != L"^c", selectionOnly);
    if (selectionOnly && RejectRectangularEdit(*this))
        return 0;
    if (Send(SCI_GETREADONLY) || search.findText.empty())
        return 0;
    auto needle = Needle(*this), replacement = Replacement(*this);
    if (needle.empty())
        return 0;
    auto start = selectionOnly ? Send(SCI_GETSELECTIONSTART) : 0;
    auto end = selectionOnly ? Send(SCI_GETSELECTIONEND) : Send(SCI_GETLENGTH);
    // Validate before opening undo group, including malformed regex on empty input.
    Search(*this, needle, start, end);
    size_t count = 0;
    Send(SCI_BEGINUNDOACTION);
    try {
        while (start <= end) {
            auto found = Search(*this, needle, start, end);
            if (found < 0)
                break;
            const auto oldEnd = Send(SCI_GETTARGETEND), oldLength = oldEnd - found;
            auto size = Send(ReplaceMessage(*this), replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
            end += size - oldLength;
            start = found + size;
            ++count;
            if (oldLength == 0) {
                if (start >= end)
                    break;
                start = Send(SCI_POSITIONAFTER, start);
            }
        }
    } catch (...) {
        Send(SCI_ENDUNDOACTION);
        throw;
    }
    Send(SCI_ENDUNDOACTION);
    search.lastMatchStart = search.lastMatchEnd = -1;
    return count;
}
size_t EditorDocument::FindAll(bool selectAll, bool bookmark) {
    if (FastView()) {
        if (selectAll) throw std::runtime_error("Global multiple selections require the full editor; use Find All results to navigate mapped matches.");
        auto snapshot = fastView->Model().Capture(); auto hits = fastView->ArchiveMatches(MappedSearchOptions(), false);
        fastView->SetMarks(snapshot, hits, bookmark); search.matches = hits->Count();
        return hits->Count();
    }
    struct RestoreState {
        EditorDocument &document;
        LRESULT indicator, flags, start, end;
        ~RestoreState() {
            document.Send(SCI_SETINDICATORCURRENT, indicator);
            document.Send(SCI_SETSEARCHFLAGS, flags);
            document.Send(SCI_SETTARGETRANGE, start, end);
        }
    } restore{*this, Send(SCI_GETINDICATORCURRENT), Send(SCI_GETSEARCHFLAGS), Send(SCI_GETTARGETSTART),
              Send(SCI_GETTARGETEND)};
    search.matches = 0;
    if (search.findText.empty())
        return 0;
    auto needle = Needle(*this);
    if (needle.empty())
        return 0;
    auto length = Send(SCI_GETLENGTH);
    LRESULT start = 0;
    Search(*this, needle, 0, length);
    constexpr int indicator = 12;
    Send(SCI_SETINDICATORCURRENT, indicator);
    Send(SCI_INDICATORCLEARRANGE, 0, length);
    Send(SCI_INDICSETSTYLE, indicator, INDIC_ROUNDBOX);
    Send(SCI_INDICSETFORE, indicator, RGB(235, 160, 0));
    if (bookmark)
        Send(SCI_MARKERDELETEALL, 0);
    bool first = true;
    while (start <= length) {
        auto found = Search(*this, needle, start, length);
        if (found < 0)
            break;
        auto end = Send(SCI_GETTARGETEND);
        ++search.matches;
        Send(SCI_INDICATORFILLRANGE, found, end - found);
        if (bookmark)
            Send(SCI_MARKERADD, Send(SCI_LINEFROMPOSITION, found), 0);
        if (selectAll) {
            Send(SCI_SETMULTIPLESELECTION, TRUE);
            if (first) {
                Send(SCI_CLEARSELECTIONS);
                Send(SCI_SETSEL, found, end);
                first = false;
            } else
                Send(SCI_ADDSELECTION, end, found);
        }
        if (end == found) {
            if (end >= length)
                break;
            start = Send(SCI_POSITIONAFTER, end);
        } else
            start = end;
    }
    return search.matches;
}

// Shared enumeration for Count, Mark and Finder. Positions are UTF-8 byte offsets.
std::vector<SearchMatch> EditorDocument::CollectMatches(bool selectionOnly) {
    if (FastView()) { auto snapshot = fastView->Model().Capture(); auto hits = fastView->Matches(MappedSearchOptions(), selectionOnly); return MappedMatches(snapshot, hits); }
    const auto flags = Send(SCI_GETSEARCHFLAGS), from = Send(SCI_GETTARGETSTART), to = Send(SCI_GETTARGETEND);
    struct Restore { EditorDocument& d; LRESULT flags, from, to;
        ~Restore() { d.Send(SCI_SETSEARCHFLAGS, flags); d.Send(SCI_SETTARGETRANGE, from, to); }
    } restore{*this, flags, from, to};
    std::vector<SearchMatch> matches;
    if (search.findText.empty()) return matches;
    const auto needle = Needle(*this);
    if (needle.empty()) return matches;
    auto start = selectionOnly ? Send(SCI_GETSELECTIONSTART) : 0;
    const auto limit = selectionOnly ? Send(SCI_GETSELECTIONEND) : Send(SCI_GETLENGTH);
    const auto text = std::make_shared<const std::string>(Text());
    while (start <= limit) {
        auto found = Search(*this, needle, start, limit);
        if (found < 0) break;
        auto end = Send(SCI_GETTARGETEND), line = Send(SCI_LINEFROMPOSITION, found);
        auto lineStart = Send(SCI_POSITIONFROMLINE, line);
        matches.push_back({found, end, line, lineStart, text});
        if (end == found) { if (end >= limit) break; start = Send(SCI_POSITIONAFTER, end); }
        else start = end;
    }
    return matches;
}

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
        auto position = std::wstring_view(L"\\abefnrtv").find(escape);
        if (position != std::wstring_view::npos) {
            segment += L"\\\a\b\x1B\f\n\r\t\v"[position];
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
bool EditorDocument::FindNext(bool backwards, const std::function<bool(bool)> &confirmWrap) {
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
    if (Send(SCI_GETREADONLY) || search.findText.empty())
        return false;
    auto needle = Needle(*this);
    if (needle.empty())
        return false;
    auto start = Send(SCI_GETSELECTIONSTART), end = Send(SCI_GETSELECTIONEND);
    if (Search(*this, needle, start, end) != start || Send(SCI_GETTARGETEND) != end)
        return false;
    auto replacement = Replacement(*this);
    auto size = Send(ReplaceMessage(*this), replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    Send(SCI_SETSEL, start, start + size);
    search.lastMatchStart = start;
    search.lastMatchEnd = start + size;
    return true;
}
size_t EditorDocument::ReplaceAll(bool selectionOnly) {
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

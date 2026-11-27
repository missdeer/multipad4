// Scintilla style export adapted from notepad4 Bridge.cpp / SciTE ExportRTF.
#include "EditRichText.h"
#include "EditClipboard.h"
#include <cstdio>
#include <set>
#include <array>
#include <limits>
#include <string_view>
#include <vector>
#ifdef MULTIPAD4_RICH_TEXT_TEST
#include "../tests/hooks/EditRichText.cpp.01.inc"
#else
#define RTF_UTF16(value) files::FromUtf8(value)
#endif
namespace {
void AppendRtfText(std::string& out, std::wstring_view text) {
    for (wchar_t c : text) {
        if (c == L'\\' || c == L'{' || c == L'}') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 32 && c < 127)
            out += static_cast<char>(c);
        else
            out += "\\u" + std::to_string(static_cast<short>(c)) + "?";
    }
}
bool SetData(UINT format, const void *data, size_t size) {
    auto memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory)
        return false;
    auto *pointer = GlobalLock(memory);
    if (!pointer) {
        GlobalFree(memory);
        return false;
    }
    memcpy(pointer, data, size);
    GlobalUnlock(memory);
    if (!SetClipboardData(format, memory)) {
        GlobalFree(memory);
        return false;
    }
    return true;
}
} // namespace
void CopyRichText(EditorDocument &d) {
#ifdef MULTIPAD4_RICH_TEXT_TEST
#include "../tests/hooks/EditRichText.cpp.02.inc"
#endif
    auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
    if (start == end) { start = 0; end = d.Send(SCI_GETLENGTH); }
    if (start == end) return;
    d.Send(SCI_COLOURISE, start, end);
    std::vector<char> styled;
    size_t count = 0;
    auto readBlock = [&](LRESULT first) {
        auto last = first + (std::min)(LRESULT(64 * 1024), end - first);
        if (last < end) last = d.Send(SCI_POSITIONBEFORE, last);
        if (last <= first) throw std::runtime_error("Cannot advance styled text range.");
        count = static_cast<size_t>(last - first);
        if (count > ((std::numeric_limits<size_t>::max)() - 2) / 2) throw std::length_error("Styled text range is too large.");
        // This fork returns N styles, NUL, N text bytes, NUL (not interleaved pairs).
        styled.resize(2 * count + 2);
        Sci_TextRangeFull range{{first, last}, styled.data()};
        if (d.Send(SCI_GETSTYLEDTEXTFULL, 0, reinterpret_cast<LPARAM>(&range)) != static_cast<LRESULT>(count))
            throw std::runtime_error("Cannot read styled text range.");
        return last;
    };
    auto characterBytes = [](unsigned char lead) -> size_t {
        return lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    };
    std::set<int> styles{STYLE_DEFAULT};
    bool styleSkipLF = false;
    for (auto first = start; first < end;) {
        const auto last = readBlock(first);
        const auto* text = styled.data() + count + 1;
        for (size_t i = 0; i < count; i += characterBytes(static_cast<unsigned char>(text[i]))) {
            if (styleSkipLF) { styleSkipLF = false; if (text[i] == '\n') continue; }
            styles.insert(static_cast<unsigned char>(styled[i]));
            styleSkipLF = text[i] == '\r';
        }
        first = last;
    }
    std::map<int, int> ids;
    int index = 0;
    std::string out = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl";
    for (int style : styles) {
        ids[style] = index;
        char font[256]{};
        d.Send(SCI_STYLEGETFONT, style, reinterpret_cast<LPARAM>(font));
        auto charset = d.Send(SCI_STYLEGETCHARACTERSET, style);
        out += "{\\f" + std::to_string(index++) + "\\fnil";
        if (charset != DEFAULT_CHARSET && charset != ANSI_CHARSET) out += "\\fcharset" + std::to_string(charset);
        out += " "; AppendRtfText(out, RTF_UTF16(font)); out += ";}";
    }
    out += "}{\\colortbl;";
    for (int style : styles) for (UINT message : {SCI_STYLEGETFORE, SCI_STYLEGETBACK}) {
        auto color = static_cast<COLORREF>(d.Send(message, style));
        out += "\\red" + std::to_string(GetRValue(color)) + "\\green" + std::to_string(GetGValue(color)) +
            "\\blue" + std::to_string(GetBValue(color)) + ";";
    }
    out += "}";
    struct Controls { std::array<std::string, 2> text; bool eolFilled = false, eolKnown = false; };
    std::array<Controls, 256> controls;
    for (int style : styles) {
        const int id = ids[style];
        const auto prefix = "\\f" + std::to_string(id) + "\\cf" + std::to_string(id * 2 + 1) + "\\highlight";
        auto suffix = "\\fs" + std::to_string((d.Send(SCI_STYLEGETSIZEFRACTIONAL, style) + 25) / 50);
        suffix += d.Send(SCI_STYLEGETWEIGHT, style) >= FW_SEMIBOLD ? "\\b" : "\\b0";
        suffix += d.Send(SCI_STYLEGETITALIC, style) ? "\\i" : "\\i0";
        suffix += d.Send(SCI_STYLEGETUNDERLINE, style) ? "\\ul" : "\\ulnone";
        suffix += d.Send(SCI_STYLEGETSTRIKE, style) ? "\\strike " : "\\strike0 ";
        controls[style].text = {prefix + std::to_string(id * 2 + 2) + suffix, prefix + "0" + suffix};
        controls[style].eolFilled = d.Send(SCI_STYLEGETEOLFILLED, style) != 0;
        controls[style].eolKnown = true;
    }
    const int defaultBackground = ids[STYLE_DEFAULT] * 2 + 2;
    auto lineBackground = [&](LRESULT position) {
        auto line = d.Send(SCI_LINEFROMPOSITION, position), nextLine = d.Send(SCI_POSITIONFROMLINE, line + 1);
        if (nextLine > position && nextLine < end) {
            const auto style = static_cast<unsigned>(d.Send(SCI_GETSTYLEINDEXAT, nextLine - 1));
            if (!controls[style].eolKnown) {
                controls[style].eolFilled = d.Send(SCI_STYLEGETEOLFILLED, style) != 0; controls[style].eolKnown = true;
            }
            if (controls[style].eolFilled) return ids[style] * 2 + 2;
        }
        return defaultBackground;
    };
    int background = lineBackground(start), previous = -1;
    out += "\\pard\\box\\cbpat" + std::to_string(background) + " ";
    size_t column = 0;
    bool skipLF = false;
    std::wstring plain;
    const auto tabWidth = static_cast<size_t>(max<LRESULT>(1, d.Send(SCI_GETTABWIDTH)));
    const bool useTabs = d.Send(SCI_GETUSETABS) != 0;
    for (auto first = start; first < end;) {
        const auto last = readBlock(first);
        const std::string text(styled.data() + count + 1, count);
        const auto wide = RTF_UTF16(text);
        plain += wide;
        size_t i = 0, unit = 0;
        while (i < count) {
            const char c = text[i];
            if (skipLF) { skipLF = false; if (c == '\n') { ++i; ++unit; continue; } }
            const auto style = static_cast<unsigned char>(styled[i]);
            if (style != previous) { out += controls[style].text[ids[style] * 2 + 2 == background]; previous = style; }
            if (c == '\r' || c == '\n') {
                column = 0;
                auto next = first + static_cast<LRESULT>(i) + 1;
                const auto following = i + 1 < count ? static_cast<unsigned char>(text[i + 1]) :
                    next < end ? static_cast<unsigned>(d.Send(SCI_GETCHARAT, next)) : 0;
                skipLF = c == '\r' && next < end && following == '\n';
                if (skipLF) ++next;
                const int nextBackground = next < end ? lineBackground(next) : background;
                if (nextBackground != background) {
                    background = nextBackground;
                    out += "\\par\n\\pard\\box\\cbpat" + std::to_string(background) + " "; previous = -1;
                } else out += "\\line\n";
                ++i; ++unit;
            } else if (c == '\t') {
                const auto padding = tabWidth - column % tabWidth;
                if (useTabs) out += "\\tab "; else out.append(padding, ' ');
                column += padding; ++i; ++unit;
            } else {
                const auto begin = unit;
                size_t characters = 0;
                do {
                    const auto bytes = characterBytes(static_cast<unsigned char>(text[i]));
                    i += bytes; unit += bytes == 4 ? 2 : 1; ++characters;
                } while (i < count && static_cast<unsigned char>(styled[i]) == style && text[i] != '\r' && text[i] != '\n' && text[i] != '\t');
                AppendRtfText(out, std::wstring_view(wide).substr(begin, unit - begin)); column += characters;
            }
        }
        first = last;
    }
    out += "\\par\n}";
    if (OpenEditClipboard(d.hwnd)) {
        EmptyClipboard();
        SetData(CF_UNICODETEXT, plain.c_str(), (plain.size() + 1) * sizeof(wchar_t));
        SetData(RegisterClipboardFormatW(L"Rich Text Format"), out.c_str(), out.size() + 1);
        CloseClipboard();
    }
}
void ShowCharacterInfo(EditorDocument &d) {
    auto position = d.Send(SCI_GETSELECTIONSTART);
    if (position >= d.Send(SCI_GETLENGTH))
        return;
    auto next = d.Send(SCI_POSITIONAFTER, position);
    auto utf8 = d.Range(position, next - position);
    auto utf16 = RTF_UTF16(utf8);
    unsigned character = utf16[0];
    if (utf16.size() == 2)
        character = 0x10000 + ((utf16[0] - 0xD800) << 10) + (utf16[1] - 0xDC00);
    char buffer[256]{};
    sprintf_s(buffer, "Unicode: U+%04X\nUTF-16:", character);
    std::string info = buffer;
    for (auto c : utf16) {
        sprintf_s(buffer, " %04X", static_cast<unsigned>(c));
        info += buffer;
    }
    info += "\nUTF-8:";
    for (unsigned char c : utf8) {
        sprintf_s(buffer, " %02X", c);
        info += buffer;
    }
    info += "\nHTML: &#" + std::to_string(character) + ";";
    using CharName = int32_t(__cdecl *)(uint32_t, int, char *, int32_t, int *);
    HMODULE icu = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (icu) {
        auto function = reinterpret_cast<CharName>(GetProcAddress(icu, "u_charName"));
        if (function) {
            int error = 0;
            if (function(character, 2, buffer, 256, &error) > 0) {
                info += "\nName: ";
                info += buffer;
            }
        }
        using IntProperty = int32_t(__cdecl *)(uint32_t, int);
        using PropertyName = const char *(__cdecl *)(int, int32_t, int);
        auto property = reinterpret_cast<IntProperty>(GetProcAddress(icu, "u_getIntPropertyValue"));
        auto name = reinterpret_cast<PropertyName>(GetProcAddress(icu, "u_getPropertyValueName"));
        if (property && name) {
            int category = property(character, 0x1005);
            auto shortName = name(0x1005, category, 0), longName = name(0x1005, category, 1);
            if (shortName && longName) {
                info += "\nCategory: ";
                info += shortName;
                info += ' ';
                info += longName;
            }
        }
        FreeLibrary(icu);
    }
    auto wide = RTF_UTF16(info);
    if (OpenEditClipboard(d.hwnd)) {
        EmptyClipboard();
        SetData(CF_UNICODETEXT, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
        CloseClipboard();
    }
    d.Send(SCI_SHOWNOTIFICATION, position << 2, reinterpret_cast<LPARAM>(info.c_str()));
}

// Scintilla style export adapted from notepad4 Bridge.cpp / SciTE ExportRTF.
#include "EditRichText.h"
#include "EditClipboard.h"
#include <cstdio>
#include <set>
namespace {
std::string RtfText(const std::wstring &text) {
    std::string out;
    for (wchar_t c : text) {
        if (c == L'\\' || c == L'{' || c == L'}') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 32 && c < 127)
            out += static_cast<char>(c);
        else
            out += "\\u" + std::to_string(static_cast<short>(c)) + "?";
    }
    return out;
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
    auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
    if (start == end) {
        start = 0;
        end = d.Send(SCI_GETLENGTH);
    }
    if (start == end)
        return;
    d.Send(SCI_COLOURISE, start, end);
    std::set<int> styles{STYLE_DEFAULT};
    for (auto i = start; i < end; i = d.Send(SCI_POSITIONAFTER, i))
        styles.insert(static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT, i)));
    std::map<int, int> ids;
    int index = 0;
    std::string out = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl";
    for (int style : styles) {
        ids[style] = index;
        char font[256]{};
        d.Send(SCI_STYLEGETFONT, style, reinterpret_cast<LPARAM>(font));
        auto charset = d.Send(SCI_STYLEGETCHARACTERSET, style);
        out += "{\\f" + std::to_string(index++) + "\\fnil";
        if (charset != DEFAULT_CHARSET && charset != ANSI_CHARSET)
            out += "\\fcharset" + std::to_string(charset);
        out += " " + RtfText(files::FromUtf8(font)) + ";}";
    }
    out += "}{\\colortbl;";
    for (int style : styles) {
        for (UINT message : {SCI_STYLEGETFORE, SCI_STYLEGETBACK}) {
            auto color = static_cast<COLORREF>(d.Send(message, style));
            out += "\\red" + std::to_string(GetRValue(color)) + "\\green" + std::to_string(GetGValue(color)) +
                   "\\blue" + std::to_string(GetBValue(color)) + ";";
        }
    }
    out += "}";
    const int defaultBackground = ids[STYLE_DEFAULT] * 2 + 2;
    auto lineBackground = [&](LRESULT position) {
        auto line = d.Send(SCI_LINEFROMPOSITION, position), nextLine = d.Send(SCI_POSITIONFROMLINE, line + 1);
        if (nextLine > position && nextLine < end) {
            int style = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT, nextLine - 1));
            if (d.Send(SCI_STYLEGETEOLFILLED, style))
                return ids[style] * 2 + 2;
        }
        return defaultBackground;
    };
    int background = lineBackground(start);
    out += "\\pard\\box\\cbpat" + std::to_string(background) + " ";
    auto text = d.Text();
    int previous = -1;
    size_t column = 0;
    const auto tabWidth = static_cast<size_t>(max<LRESULT>(1, d.Send(SCI_GETTABWIDTH)));
    for (auto pos = start; pos < end;) {
        int style = static_cast<int>(d.Send(SCI_GETSTYLEINDEXAT, pos));
        if (style != previous) {
            int id = ids[style];
            out += "\\f" + std::to_string(id) + "\\cf" + std::to_string(id * 2 + 1) + "\\highlight" +
                   std::to_string(id * 2 + 2 == background ? 0 : id * 2 + 2) + "\\fs" +
                   std::to_string((d.Send(SCI_STYLEGETSIZEFRACTIONAL, style) + 25) / 50);
            out += d.Send(SCI_STYLEGETWEIGHT, style) >= FW_SEMIBOLD ? "\\b" : "\\b0";
            out += d.Send(SCI_STYLEGETITALIC, style) ? "\\i" : "\\i0";
            out += d.Send(SCI_STYLEGETUNDERLINE, style) ? "\\ul" : "\\ulnone";
            out += d.Send(SCI_STYLEGETSTRIKE, style) ? "\\strike " : "\\strike0 ";
            previous = style;
        }
        auto next = d.Send(SCI_POSITIONAFTER, pos);
        char c = text[static_cast<size_t>(pos)];
        if (c == '\r' || c == '\n') {
            column = 0;
            if (c == '\r' && next < end && text[static_cast<size_t>(next)] == '\n')
                next = d.Send(SCI_POSITIONAFTER, next);
            int nextBackground = next < end ? lineBackground(next) : background;
            if (nextBackground != background) {
                background = nextBackground;
                out += "\\par\n\\pard\\box\\cbpat" + std::to_string(background) + " ";
                previous = -1;
            } else
                out += "\\line\n";
        } else if (c == '\t') {
            const auto padding = tabWidth - column % tabWidth;
            if (d.Send(SCI_GETUSETABS))
                out += "\\tab ";
            else
                out.append(padding, ' ');
            column += padding;
        } else {
            out += RtfText(files::FromUtf8(text.substr(pos, next - pos)));
            ++column;
        }
        pos = next;
    }
    out += "\\par\n}";
    auto wide = files::FromUtf8(text.substr(start, end - start));
    if (OpenEditClipboard(d.hwnd)) {
        EmptyClipboard();
        SetData(CF_UNICODETEXT, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
        SetData(RegisterClipboardFormatW(L"Rich Text Format"), out.c_str(), out.size() + 1);
        CloseClipboard();
    }
}
void ShowCharacterInfo(EditorDocument &d) {
    auto position = d.Send(SCI_GETSELECTIONSTART);
    if (position >= d.Send(SCI_GETLENGTH))
        return;
    auto next = d.Send(SCI_POSITIONAFTER, position);
    auto utf8 = d.Text().substr(position, next - position);
    auto utf16 = files::FromUtf8(utf8);
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
    auto wide = files::FromUtf8(info);
    if (OpenEditClipboard(d.hwnd)) {
        EmptyClipboard();
        SetData(CF_UNICODETEXT, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
        CloseClipboard();
    }
    d.Send(SCI_SHOWNOTIFICATION, position << 2, reinterpret_cast<LPARAM>(info.c_str()));
}

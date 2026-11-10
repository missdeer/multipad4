// Document-scoped adaptations of operations dispatched by notepad4 Edit.cpp.
// See THIRD_PARTY.md for the pinned upstream authors and license.
#include "EditAdvanced.h"
#include "DialogResizeGrip.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "EditCaseMapping.h"
#include "EditClipboard.h"
#include "EditCodeFormat.h"
#include "EditComments.h"
#include "EditEncodingName.h"
#include "EditNumbers.h"
#include "EditRichText.h"
#include "EditSelection.h"
#include "EditWhitespace.h"
#include "StyleManager.h"
#include "resource.h"
#include <cctype>
#include <charconv>
#include <chrono>
#include <climits>
#include <ctime>
#include <cwctype>
#include <random>
#include <set>
#include <shlwapi.h>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
namespace {
struct Range {
    LRESULT start, end;
};
Range Selection(EditorDocument &d, bool lines = false) {
    Range r{d.Send(SCI_GETSELECTIONSTART), d.Send(SCI_GETSELECTIONEND)};
    if (lines) {
        if (r.start == r.end)
            return {0, d.Send(SCI_GETLENGTH)};
        auto first = d.Send(SCI_LINEFROMPOSITION, r.start), last = d.Send(SCI_LINEFROMPOSITION, r.end);
        if (last > first && r.end == d.Send(SCI_POSITIONFROMLINE, last))
            --last;
        r.start = d.Send(SCI_POSITIONFROMLINE, first);
        r.end = last + 1 < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, last + 1) : d.Send(SCI_GETLENGTH);
    }
    return r;
}
std::string Text(EditorDocument &d, Range r) {
    auto text = d.Text();
    return text.substr(r.start, r.end - r.start);
}
void Replace(EditorDocument &d, Range r, const std::string &text) {
    d.Send(SCI_BEGINUNDOACTION);
    d.Send(SCI_SETTARGETRANGE, r.start, r.end);
    d.Send(SCI_REPLACETARGET, text.size(), reinterpret_cast<LPARAM>(text.data()));
    d.Send(SCI_SETSEL, r.start, r.start + text.size());
    d.Send(SCI_ENDUNDOACTION);
}
void Enclose(EditorDocument &document, Range range, const std::string &prefix, const std::string &suffix) {
    if (prefix.empty() && suffix.empty())
        return;
    const auto oldCaret = document.Send(SCI_GETCURRENTPOS), oldAnchor = document.Send(SCI_GETANCHOR);
    Replace(document, range, prefix + Text(document, range) + suffix);
    const auto start = range.start + static_cast<LRESULT>(prefix.size()),
               end = range.end + static_cast<LRESULT>(prefix.size());
    document.Send(SCI_SETSEL, oldCaret < oldAnchor ? end : start, oldCaret < oldAnchor ? start : end);
}
void PadDocumentLines(EditorDocument &document) {
    const auto start = document.Send(SCI_GETSELECTIONSTART), end = document.Send(SCI_GETSELECTIONEND);
    const auto caret = document.Send(SCI_GETCURRENTPOS), anchor = document.Send(SCI_GETANCHOR);
    const bool rectangular = document.Send(SCI_SELECTIONISRECTANGLE) != 0;
    const auto caretLine = document.Send(SCI_LINEFROMPOSITION, caret),
               anchorLine = document.Send(SCI_LINEFROMPOSITION, anchor);
    const auto caretColumn = document.Send(SCI_GETCOLUMN, caret), anchorColumn = document.Send(SCI_GETCOLUMN, anchor);
    auto first = document.Send(SCI_LINEFROMPOSITION, start), last = document.Send(SCI_LINEFROMPOSITION, end);
    const bool multipleLines = first != last;
    bool reduced = false;
    if (rectangular || first == last) {
        first = 0;
        last = document.Send(SCI_GETLINECOUNT) - 1;
    } else if (end <= document.Send(SCI_POSITIONFROMLINE, last)) {
        --last;
        reduced = true;
    }
    LRESULT width = 0;
    for (auto line = first; line <= last; ++line) {
        auto position = document.Send(rectangular ? SCI_GETLINESELENDPOSITION : SCI_GETLINEENDPOSITION, line);
        if (position >= 0)
            width = max(width, document.Send(SCI_GETCOLUMN, position));
    }
    std::string padding(static_cast<size_t>(width), ' ');
    document.Send(SCI_BEGINUNDOACTION);
    for (auto line = last; line >= first; --line) {
        auto position = document.Send(SCI_GETLINEENDPOSITION, line);
        if (rectangular) {
            auto selectedEnd = document.Send(SCI_GETLINESELENDPOSITION, line);
            if (selectedEnd < 0 || position > selectedEnd) {
                if (line == 0)
                    break;
                continue;
            }
        }
        auto count = width - document.Send(SCI_GETCOLUMN, position);
        if (count > 0) {
            document.Send(SCI_SETTARGETRANGE, position, position);
            document.Send(SCI_REPLACETARGET, count, reinterpret_cast<LPARAM>(padding.data()));
        }
        if (line == 0)
            break;
    }
    document.Send(SCI_ENDUNDOACTION);
    if (rectangular) {
        document.Send(SCI_SETRECTANGULARSELECTIONCARET, document.Send(SCI_FINDCOLUMN, caretLine, caretColumn));
        document.Send(SCI_SETRECTANGULARSELECTIONANCHOR, document.Send(SCI_FINDCOLUMN, anchorLine, anchorColumn));
    } else if (multipleLines) {
        auto low = document.Send(SCI_POSITIONFROMLINE, first),
             high = document.Send(reduced ? SCI_POSITIONFROMLINE : SCI_GETLINEENDPOSITION, reduced ? last + 1 : last);
        document.Send(SCI_SETSEL, caret < anchor ? high : low, caret < anchor ? low : high);
    }
}
std::string Eol(EditorDocument &d) {
    auto e = d.Send(SCI_GETEOLMODE);
    return e == SC_EOL_CRLF ? "\r\n" : e == SC_EOL_LF ? "\n" : "\r";
}
struct Line {
    std::string text, eol;
};
std::vector<Line> Lines(const std::string &text) {
    std::vector<Line> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        auto end = text.find_first_of("\r\n", pos);
        if (end == std::string::npos) {
            lines.push_back({text.substr(pos), {}});
            break;
        }
        size_t next = end + 1;
        if (text[end] == '\r' && next < text.size() && text[next] == '\n')
            ++next;
        lines.push_back({text.substr(pos, end - pos), text.substr(end, next - end)});
        pos = next;
    }
    return lines;
}
std::string Join(const std::vector<Line> &lines) {
    std::string out;
    for (const auto &line : lines)
        out += line.text + line.eol;
    return out;
}
bool Blank(const std::string &text) {
    return text.find_first_not_of(" \t") == std::string::npos;
}
void TrimLeft(std::string &text) {
    auto p = text.find_first_not_of(" \t");
    text.erase(0, p == std::string::npos ? text.size() : p);
}
void TrimRight(std::string &text) {
    auto p = text.find_last_not_of(" \t");
    text.erase(p == std::string::npos ? 0 : p + 1);
}
std::string Clipboard(HWND owner) {
    std::wstring text;
    if (!OpenEditClipboard(owner))
        throw std::runtime_error("Cannot read clipboard text; document retained.");
    auto memory = GetClipboardData(CF_UNICODETEXT);
    if (memory) {
        auto *data = static_cast<const wchar_t *>(GlobalLock(memory));
        if (data) {
            text = data;
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    return files::ToUtf8(text);
}
void Copy(EditorDocument &d, const std::string &text) {
    d.Send(SCI_COPYTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
}
constexpr UINT cases[] = {IDM_EDIT_CHAR2HEX,
                          IDM_EDIT_HEX2CHAR,
                          IDM_EDIT_ESCAPECCHARS,
                          IDM_EDIT_UNESCAPECCHARS,
                          IDM_EDIT_XHTML_ESCAPE_CHAR,
                          IDM_EDIT_XHTML_UNESCAPE_CHAR,
                          IDM_EDIT_INVERTCASE,
                          IDM_EDIT_SENTENCECASE,
                          IDM_EDIT_TITLECASE,
                          IDM_EDIT_MAP_FULLWIDTH,
                          IDM_EDIT_MAP_HALFWIDTH,
                          IDM_EDIT_MAP_SIMPLIFIED_CHINESE,
                          IDM_EDIT_MAP_TRADITIONAL_CHINESE,
                          IDM_EDIT_MAP_HIRAGANA,
                          IDM_EDIT_MAP_KATAKANA,
                          IDM_EDIT_MAP_MALAYALAM_LATIN,
                          IDM_EDIT_MAP_DEVANAGARI_LATIN,
                          IDM_EDIT_MAP_CYRILLIC_LATIN,
                          IDM_EDIT_MAP_BENGALI_LATIN,
                          IDM_EDIT_MAP_HANGUL_DECOMPOSITION,
                          IDM_EDIT_MAP_HANJA_HANGUL};
constexpr UINT transformations[] = {IDM_EDIT_TRIMLINES,
                                    IDM_EDIT_TRIMLEAD,
                                    IDM_EDIT_STRIP1STCHAR,
                                    IDM_EDIT_STRIPLASTCHAR,
                                    IDM_EDIT_COMPRESSWS,
                                    IDM_EDIT_MERGEBLANKLINES,
                                    IDM_EDIT_REMOVEBLANKLINES,
                                    IDM_EDIT_MERGEDUPLICATELINE,
                                    IDM_EDIT_REMOVEDUPLICATELINE,
                                    IDM_EDIT_PADWITHSPACES,
                                    IDM_EDIT_CONVERTTABS,
                                    IDM_EDIT_CONVERTTABS2,
                                    IDM_EDIT_CONVERTSPACES,
                                    IDM_EDIT_CONVERTSPACES2,
                                    IDM_EDIT_JOINLINES,
                                    IDM_EDIT_JOINLINESEX,
                                    IDM_EDIT_SPLITLINES,
                                    IDM_EDIT_SORTLINES,
                                    IDM_EDIT_MODIFYLINES,
                                    IDM_EDIT_ENCLOSESELECTION,
                                    IDM_EDIT_INSERT_XMLTAG,
                                    IDM_EDIT_ALIGN,
                                    IDM_EDIT_COLUMNWRAP,
                                    IDM_EDIT_SWAP,
                                    IDM_EDIT_LINECOMMENT,
                                    IDM_EDIT_STREAMCOMMENT,
                                    IDM_EDIT_URLENCODE,
                                    IDM_EDIT_URLCOMPONENTENCODE,
                                    IDM_EDIT_URLDECODE,
                                    IDM_EDIT_BASE64_ENCODE,
                                    IDM_EDIT_BASE64_SAFE_ENCODE,
                                    IDM_EDIT_BASE64_HTML_EMBEDDED_IMAGE,
                                    IDM_EDIT_BASE64_DECODE,
                                    IDM_EDIT_BASE64_DECODE_AS_HEX,
                                    IDM_EDIT_NUM2BIN,
                                    IDM_EDIT_NUM2OCT,
                                    IDM_EDIT_NUM2DEC,
                                    IDM_EDIT_NUM2HEX,
                                    CMD_ENCLOSE_TRIPLE_SQ,
                                    CMD_ENCLOSE_TRIPLE_DQ,
                                    CMD_ENCLOSE_TRIPLE_BT,
                                    CMD_INCREASENUM,
                                    CMD_DECREASENUM};
constexpr UINT insertions[] = {IDM_EDIT_INSERT_GUID,         IDM_EDIT_INSERT_ENCODING,     IDM_EDIT_INSERT_SHORTDATE,
                               IDM_EDIT_INSERT_LONGDATE,     IDM_EDIT_INSERT_LOC_DATE,     IDM_EDIT_INSERT_LOC_DATETIME,
                               IDM_EDIT_INSERT_UTC_DATETIME, IDM_EDIT_INSERT_TIMESTAMP,    IDM_EDIT_INSERT_TIMESTAMP_MS,
                               IDM_EDIT_INSERT_TIMESTAMP_US, IDM_EDIT_INSERT_TIMESTAMP_NS, IDM_EDIT_INSERT_FILENAME,
                               IDM_EDIT_INSERT_PATHNAME,     CMD_INSERTFILENAME_NOEXT};
std::wstring Field(HWND window, int id) {
    int length = GetWindowTextLengthW(GetDlgItem(window, id));
    std::wstring value(length + 1, L'\0');
    GetDlgItemTextW(window, id, value.data(), length + 1);
    value.resize(length);
    return value;
}
struct Options {
    UINT resource = 0;
    std::string prefix, suffix;
    bool skip = false;
    int order = IDC_SORT_ASC, align = IDC_ALIGN_LEFT, column = 80;
    bool merge = false, remove = false, unique = false, ignoreCase = false, logical = false, group = false;
    bool columnSort = false, xml = false;
    int layoutWidth = 0, layoutHeight = 0;
    struct Control {
        HWND window;
        RECT rectangle;
        bool right, stretch, lower, editor;
    };
    std::vector<Control> controls;
};
std::map<UINT, Options> savedOptions;
INT_PTR CALLBACK OptionsProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *o = reinterpret_cast<Options *>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        o = reinterpret_cast<Options *>(l);
        SetWindowLongPtrW(window, DWLP_USER, l);
        if (o->resource == IDD_SORT) {
            CheckRadioButton(window, IDC_SORT_NONE, IDC_SORT_SHUFFLE, o->order);
            CheckDlgButton(window, IDC_SORT_COLUMN, o->columnSort ? BST_CHECKED : BST_UNCHECKED);
            for (const auto &setting : {std::pair{IDC_SORT_MERGE_DUP, o->merge},
                                        {IDC_SORT_REMOVE_DUP, o->remove},
                                        {IDC_SORT_REMOVE_UNIQUE, o->unique},
                                        {IDC_SORT_IGNORE_CASE, o->ignoreCase},
                                        {IDC_SORT_LOGICAL_NUMBER, o->logical},
                                        {IDC_SORT_GROUPBY_FILE_TYPE, o->group}})
                CheckDlgButton(window, setting.first, setting.second ? BST_CHECKED : BST_UNCHECKED);
        } else if (o->resource == IDD_ALIGN) {
            CheckRadioButton(window, IDC_ALIGN_LEFT, IDC_ALIGN_JUSTIFY_PAR, o->align);
        } else if (o->resource == IDD_COLUMNWRAP) {
            SetDlgItemInt(window, IDC_COLUMNWRAP, o->column, FALSE);
        } else {
            SetDlgItemTextW(window, IDC_MODIFY_LINE_PREFIX, files::FromUtf8(o->prefix).c_str());
            SetDlgItemTextW(window, IDC_MODIFY_LINE_APPEND, files::FromUtf8(o->suffix).c_str());
            CheckDlgButton(window, IDC_MODIFY_LINE_SKIP_EMPTY, o->skip ? BST_CHECKED : BST_UNCHECKED);
        }
        CWindow(window).CenterWindow(GetParent(window));
        if (o->resource == IDD_MODIFYLINES || o->resource == IDD_ENCLOSESELECTION || o->resource == IDD_INSERTTAG) {
            PositionDialogResizeGrip(window, IDC_RESIZEGRIP2);
            RECT client{};
            GetClientRect(window, &client);
            o->layoutWidth = client.right;
            o->layoutHeight = client.bottom;
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
                RECT rectangle{};
                GetWindowRect(child, &rectangle);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&rectangle), 2);
                int id = GetDlgCtrlID(child);
                bool editor = id == IDC_MODIFY_LINE_PREFIX || id == IDC_MODIFY_LINE_APPEND;
                bool lower = rectangle.top > client.bottom / 2;
                o->controls.push_back({child, rectangle, id == IDOK || id == IDCANCEL, editor, lower, editor});
            }
        }
        return TRUE;
    }
    if (o && message == WM_GETMINMAXINFO && o->layoutWidth) {
        auto *bounds = reinterpret_cast<MINMAXINFO *>(l);
        RECT frame{}, client{};
        GetWindowRect(window, &frame);
        GetClientRect(window, &client);
        bounds->ptMinTrackSize = {o->layoutWidth + frame.right - frame.left - client.right,
                                  o->layoutHeight + frame.bottom - frame.top - client.bottom};
        return TRUE;
    }
    if (o && message == WM_SIZE && o->layoutWidth) {
        int dx = LOWORD(l) - o->layoutWidth, dy = HIWORD(l) - o->layoutHeight;
        for (const auto &control : o->controls) {
            auto rectangle = control.rectangle;
            if (control.right)
                OffsetRect(&rectangle, dx, 0);
            if (control.stretch)
                rectangle.right += dx;
            if (control.editor) {
                if (GetDlgCtrlID(control.window) == IDC_MODIFY_LINE_APPEND)
                    OffsetRect(&rectangle, 0, dy / 2);
                rectangle.bottom += dy / 2;
            } else if (GetDlgCtrlID(control.window) == IDC_MODIFY_LINE_TIP2)
                OffsetRect(&rectangle, 0, dy / 2);
            else if (control.lower)
                OffsetRect(&rectangle, 0, dy);
            SetWindowPos(control.window, nullptr, rectangle.left, rectangle.top, rectangle.right - rectangle.left,
                         rectangle.bottom - rectangle.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        PositionDialogResizeGrip(window, IDC_RESIZEGRIP2);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message == WM_NOTIFY && o) {
        auto *notification = reinterpret_cast<NMHDR *>(l);
        if (notification->code == NM_CLICK || notification->code == NM_RETURN) {
            const wchar_t *token = nullptr;
            switch (notification->idFrom) {
            case IDC_MODIFY_LINE_DLN_NP:
                token = L"$(L)";
                break;
            case IDC_MODIFY_LINE_DLN_ZP:
                token = L"$(0L)";
                break;
            case IDC_MODIFY_LINE_CN_NP:
                token = L"$(N)";
                break;
            case IDC_MODIFY_LINE_CN_ZP:
                token = L"$(0N)";
                break;
            case IDC_MODIFY_LINE_ZCN_NP:
                token = L"$(I)";
                break;
            case IDC_MODIFY_LINE_ZCN_ZP:
                token = L"$(0I)";
                break;
            }
            if (token) {
                auto target = GetDlgItem(window, IDC_MODIFY_LINE_PREFIX);
                SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(token));
                SetFocus(target);
                return TRUE;
            }
        }
    }
    if (message != WM_COMMAND || !o)
        return FALSE;
    if (o->resource == IDD_INSERTTAG && LOWORD(w) == IDC_MODIFY_LINE_PREFIX && HIWORD(w) == EN_CHANGE) {
        auto tag = Field(window, IDC_MODIFY_LINE_PREFIX);
        std::wstring closing;
        auto start = tag.find(L'<');
        if (start != std::wstring::npos) {
            ++start;
            auto end = start;
            auto tagCharacter = [](wchar_t c) {
                return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L':' ||
                       c == L'_' || c == L'-' || c == L'.';
            };
            while (end < tag.size() && tagCharacter(tag[end]))
                ++end;
            auto bracket = tag.find(L'>', end);
            if (end > start && bracket != std::wstring::npos && bracket > 0 && tag[bracket - 1] != L'/') {
                auto name = tag.substr(start, end - start), lower = name;
                for (auto &character : lower)
                    if (character >= L'A' && character <= L'Z')
                        character += L'a' - L'A';
                const std::wstring voidTags = L" area base basefont br col command embed frame hr img input isindex "
                                              L"keygen link meta param source track wbr ";
                if (o->xml || voidTags.find(L" " + lower + L" ") == std::wstring::npos)
                    closing = L"</" + name + L">";
            }
        }
        SetDlgItemTextW(window, IDC_MODIFY_LINE_APPEND, closing.c_str());
        return TRUE;
    }
    if (LOWORD(w) != IDOK)
        return FALSE;
    try {
        o->prefix = files::ToUtf8(Field(window, IDC_MODIFY_LINE_PREFIX));
        o->suffix = files::ToUtf8(Field(window, IDC_MODIFY_LINE_APPEND));
        o->skip = IsDlgButtonChecked(window, IDC_MODIFY_LINE_SKIP_EMPTY) == BST_CHECKED;
        for (int id : {IDC_SORT_NONE, IDC_SORT_ASC, IDC_SORT_DESC, IDC_SORT_SHUFFLE})
            if (IsDlgButtonChecked(window, id))
                o->order = id;
        for (int id : {IDC_ALIGN_LEFT, IDC_ALIGN_RIGHT, IDC_ALIGN_CENTER, IDC_ALIGN_JUSTIFY, IDC_ALIGN_JUSTIFY_PAR})
            if (IsDlgButtonChecked(window, id))
                o->align = id;
        o->merge = IsDlgButtonChecked(window, IDC_SORT_MERGE_DUP) == BST_CHECKED;
        o->remove = IsDlgButtonChecked(window, IDC_SORT_REMOVE_DUP) == BST_CHECKED;
        o->unique = IsDlgButtonChecked(window, IDC_SORT_REMOVE_UNIQUE) == BST_CHECKED;
        o->ignoreCase = IsDlgButtonChecked(window, IDC_SORT_IGNORE_CASE) == BST_CHECKED;
        o->logical = IsDlgButtonChecked(window, IDC_SORT_LOGICAL_NUMBER) == BST_CHECKED;
        o->group = IsDlgButtonChecked(window, IDC_SORT_GROUPBY_FILE_TYPE) == BST_CHECKED;
        o->columnSort = IsDlgButtonChecked(window, IDC_SORT_COLUMN) == BST_CHECKED;
        if (o->resource == IDD_COLUMNWRAP) {
            BOOL valid = FALSE;
            o->column = GetDlgItemInt(window, IDC_COLUMNWRAP, &valid, FALSE);
            if (!valid || o->column < 1 || o->column > 1000000) {
                MessageBeep(MB_ICONWARNING);
                return TRUE;
            }
        }
        EndDialog(window, IDOK);
    } catch (const std::exception &error) {
        auto text = files::FromUtf8(error.what());
        MessageBoxW(window, text.c_str(), L"Invalid dialog input", MB_OK | MB_ICONERROR);
    }
    return TRUE;
}
bool Dialog(EditorDocument &d, Options &options) {
    const auto resource = options.resource;
    const bool columnSort = options.columnSort;
    if (auto saved = savedOptions.find(resource); saved != savedOptions.end())
        options = saved->second;
    options.resource = resource;
    options.columnSort = columnSort;
    options.xml = GetDocumentLexer(d)->rid == NP2LEX_XML;
    options.controls.clear();
    options.layoutWidth = options.layoutHeight = 0;
    if (DialogBoxParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(options.resource), GetAncestor(d.hwnd, GA_ROOT),
                        OptionsProc, reinterpret_cast<LPARAM>(&options)) != IDOK)
        return false;
    auto normalize = [&](std::string &text) {
        std::string output;
        const auto eol = Eol(d);
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r' || text[i] == '\n') {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                    ++i;
                output += eol;
            } else
                output += text[i];
        }
        text = std::move(output);
    };
    normalize(options.prefix);
    normalize(options.suffix);
    auto saved = options;
    saved.controls.clear();
    saved.layoutWidth = saved.layoutHeight = 0;
    savedOptions[resource] = std::move(saved);
    return true;
}

std::string Base64(const std::string &input, bool safe) {
    const char *abc = safe ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                           : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned acc = 0;
    int bits = 0;
    for (unsigned char c : input) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += abc[(acc >> bits) & 63];
        }
    }
    if (bits)
        out += abc[(acc << (6 - bits)) & 63];
    while (out.size() % 4)
        out += '=';
    return out;
}
std::string Unbase64(const std::string &input) {
    std::string out;
    unsigned acc = 0;
    int bits = 0;
    bool padded = false;
    for (unsigned char c : input) {
        if (isspace(c))
            continue;
        if (c == '=') {
            padded = true;
            continue;
        }
        if (padded)
            throw std::runtime_error("Invalid Base64 padding.");
        int value = c >= 'A' && c <= 'Z'   ? c - 'A'
                    : c >= 'a' && c <= 'z' ? c - 'a' + 26
                    : c >= '0' && c <= '9' ? c - '0' + 52
                    : c == '+' || c == '-' ? 62
                    : c == '/' || c == '_' ? 63
                                           : -1;
        if (value < 0)
            throw std::runtime_error("Invalid Base64 text.");
        acc = (acc << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>(acc >> bits);
        }
    }
    if (bits == 6)
        throw std::runtime_error("Invalid Base64 length.");
    return out;
}
std::string SelectionBytes(const EditorDocument &document, const std::string &text) {
    return files::CodePage(document.encoding) == CP_UTF8 ? text : files::Encode({text, document.encoding});
}
std::string DecodedBytes(const EditorDocument &document, const std::string &text) {
    return files::CodePage(document.encoding) == CP_UTF8 ? text : files::Decode(text, document.encoding).utf8;
}
std::string Hex(const std::string &text) {
    std::string out;
    for (unsigned char c : text) {
        if (!out.empty())
            out += ' ';
        out += "0123456789ABCDEF"[c >> 4];
        out += "0123456789ABCDEF"[c & 15];
    }
    return out;
}
std::string ExpandLineVariables(std::string text, size_t line, size_t index, size_t count, size_t lastDocumentLine) {
    for (const auto &pair : {std::pair{"L", line}, {"N", index + 1}, {"I", index}}) {
        for (bool padded : {true, false}) {
            std::string token = std::string("$(") + (padded ? "0" : "") + pair.first + ")";
            auto replacement = std::to_string(pair.second);
            const size_t maximum = std::string_view(pair.first) == "L"   ? lastDocumentLine
                                   : std::string_view(pair.first) == "I" ? count - 1
                                                                         : count;
            if (padded) {
                const auto width = std::to_string(maximum).size();
                replacement.insert(0, width > replacement.size() ? width - replacement.size() : 0, '0');
            }
            size_t position = 0;
            while ((position = text.find(token, position)) != std::string::npos) {
                text.replace(position, token.size(), replacement);
                position += replacement.size();
            }
        }
    }
    return text;
}
std::string Justify(std::string text, size_t width) {
    TrimLeft(text);
    TrimRight(text);
    std::vector<std::string> words;
    for (size_t i = 0; i < text.size();) {
        auto end = text.find_first_of(" \t", i);
        if (end == std::string::npos)
            end = text.size();
        if (end > i)
            words.push_back(text.substr(i, end - i));
        i = text.find_first_not_of(" \t", end);
        if (i == std::string::npos)
            break;
    }
    if (words.size() < 2)
        return text;
    size_t letters = 0;
    for (const auto &word : words)
        letters += files::FromUtf8(word).size();
    size_t spaces = width > letters ? width - letters : words.size() - 1;
    std::string out;
    for (size_t i = 0; i < words.size(); ++i) {
        out += words[i];
        if (i + 1 < words.size()) {
            size_t gap = spaces / (words.size() - 1) + (i >= (words.size() - 1) - spaces % (words.size() - 1));
            out.append(gap, ' ');
        }
    }
    return out;
}

} // namespace
void LoadEditPreferences(const std::filesystem::path &path) {
    if (path.empty())
        return;
    auto decode = [](const std::wstring &input) {
        std::wstring output;
        if (input.size() % 4)
            return std::string{};
        for (size_t i = 0; i < input.size(); i += 4) {
            unsigned value = 0;
            for (size_t j = 0; j < 4; ++j) {
                wchar_t c = input[i + j];
                unsigned digit = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'A' && c <= L'F' ? c - L'A' + 10 : 99;
                if (digit > 15)
                    return std::string{};
                value = value * 16 + digit;
            }
            output += static_cast<wchar_t>(value);
        }
        try {
            return files::ToUtf8(output);
        } catch (...) {
            return std::string{};
        }
    };
    for (UINT resource : {IDD_SORT, IDD_ALIGN, IDD_COLUMNWRAP, IDD_MODIFYLINES, IDD_ENCLOSESELECTION, IDD_INSERTTAG}) {
        auto section = L"EditDialog" + std::to_wstring(resource);
        Options options;
        options.resource = resource;
        auto get = [&](const wchar_t *name, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(section.c_str(), name, fallback, path.c_str()));
        };
        options.order = get(L"Order", IDC_SORT_ASC);
        options.align = get(L"Align", IDC_ALIGN_LEFT);
        options.column = std::clamp(get(L"Column", 80), 1, 1000000);
        int flags = get(L"Flags", 0);
        options.skip = flags & 1;
        options.merge = flags & 2;
        options.remove = flags & 4;
        options.unique = flags & 8;
        options.ignoreCase = flags & 16;
        options.logical = flags & 32;
        options.group = flags & 64;
        wchar_t buffer[16384]{};
        GetPrivateProfileStringW(section.c_str(), L"PrefixHex", L"", buffer, 16384, path.c_str());
        options.prefix = decode(buffer);
        GetPrivateProfileStringW(section.c_str(), L"SuffixHex", L"", buffer, 16384, path.c_str());
        options.suffix = decode(buffer);
        savedOptions[resource] = std::move(options);
    }
}
void SaveEditPreferences(const std::filesystem::path &path) {
    if (path.empty())
        return;
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (!std::filesystem::exists(path)) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            const wchar_t bom = 0xFEFF;
            DWORD written = 0;
            WriteFile(file, &bom, sizeof(bom), &written, nullptr);
            CloseHandle(file);
        }
    }
    auto encode = [](const std::string &input) {
        std::wstring out;
        wchar_t hex[5];
        for (wchar_t c : files::FromUtf8(input)) {
            swprintf_s(hex, L"%04X", static_cast<unsigned>(c));
            out += hex;
        }
        return out;
    };
    for (const auto &[resource, options] : savedOptions) {
        auto section = L"EditDialog" + std::to_wstring(resource);
        auto put = [&](const wchar_t *name, int value) {
            auto text = std::to_wstring(value);
            WritePrivateProfileStringW(section.c_str(), name, text.c_str(), path.c_str());
        };
        put(L"Order", options.order);
        put(L"Align", options.align);
        put(L"Column", options.column);
        put(L"Flags", static_cast<int>(options.skip) | (options.merge << 1) | (options.remove << 2) |
                          (options.unique << 3) | (options.ignoreCase << 4) | (options.logical << 5) |
                          (options.group << 6));
        auto prefix = encode(options.prefix), suffix = encode(options.suffix);
        WritePrivateProfileStringW(section.c_str(), L"PrefixHex", prefix.c_str(), path.c_str());
        WritePrivateProfileStringW(section.c_str(), L"SuffixHex", suffix.c_str(), path.c_str());
    }
}
CommandState AdvancedEditState(const EditorDocument *d, UINT id) {
    bool supported = false, modifies = true;
    for (auto item : cases)
        supported |= item == id;
    for (auto item : transformations)
        supported |= item == id;
    for (auto item : insertions)
        supported |= item == id;
    switch (id) {
    case IDM_EDIT_COPYADD:
    case IDM_EDIT_CLEARCLIPBOARD:
    case CMD_COPYFILENAME:
    case CMD_COPYFILENAME_NOEXT:
    case CMD_COPYPATHNAME:
    case CMD_COPYWINPOS:
    case IDM_EDIT_SHOW_CHAR_INFO:
    case IDM_EDIT_COPYRTF:
        supported = true;
        modifies = false;
        break;
    }
    if (id == IDM_EDIT_SHOW_HEX)
        supported = true;
    if (id == IDM_EDIT_LINECOMMENT || id == IDM_EDIT_STREAMCOMMENT) {
        const auto *lexer = d ? GetDocumentLexer(*d) : nullptr;
        const auto mask = id == IDM_EDIT_LINECOMMENT ? LexerAttr_NoLineComment : LexerAttr_NoBlockComment;
        return {true, d && !d->Send(SCI_GETREADONLY) && lexer && !(lexer->lexerAttr & mask), false};
    }
    if (id == IDM_EDIT_CODE_PRETTY || id == IDM_EDIT_CODE_COMPRESS)
        return {true, d && !d->Send(SCI_GETREADONLY) && CanFormatDocumentCode(*d), false};
    if (!supported)
        return {};
    return {true, d && (!modifies || !d->Send(SCI_GETREADONLY)), false};
}
bool ExecuteAdvancedEdit(EditorDocument &d, UINT id) {
    auto state = AdvancedEditState(&d, id);
    if (!state.supported)
        return false;
    if (!state.enabled)
        return true;
    if (ExecuteWhitespaceEdit(d, id))
        return true;
    switch (id) {
    case IDM_EDIT_STRIP1STCHAR:
    case IDM_EDIT_STRIPLASTCHAR:
    case IDM_EDIT_COPYADD:
    case IDM_EDIT_ENCLOSESELECTION:
    case IDM_EDIT_INSERT_XMLTAG:
    case CMD_ENCLOSE_TRIPLE_SQ:
    case CMD_ENCLOSE_TRIPLE_DQ:
    case CMD_ENCLOSE_TRIPLE_BT:
    case IDM_EDIT_MODIFYLINES:
    case IDM_EDIT_ALIGN:
    case IDM_EDIT_COLUMNWRAP:
    case IDM_EDIT_CONVERTTABS:
    case IDM_EDIT_CONVERTTABS2:
    case IDM_EDIT_CONVERTSPACES:
    case IDM_EDIT_CONVERTSPACES2:
    case IDM_EDIT_COMPRESSWS:
    case IDM_EDIT_MERGEBLANKLINES:
    case IDM_EDIT_REMOVEBLANKLINES:
    case IDM_EDIT_JOINLINESEX:
    case IDM_EDIT_URLENCODE:
    case IDM_EDIT_URLCOMPONENTENCODE:
    case IDM_EDIT_URLDECODE:
    case IDM_EDIT_BASE64_ENCODE:
    case IDM_EDIT_BASE64_SAFE_ENCODE:
    case IDM_EDIT_BASE64_HTML_EMBEDDED_IMAGE:
    case IDM_EDIT_BASE64_DECODE:
    case IDM_EDIT_BASE64_DECODE_AS_HEX:
    case IDM_EDIT_NUM2BIN:
    case IDM_EDIT_NUM2OCT:
    case IDM_EDIT_NUM2DEC:
    case IDM_EDIT_NUM2HEX:
    case CMD_INCREASENUM:
    case CMD_DECREASENUM:
        if (RejectRectangularEdit(d))
            return true;
        break;
    }
    if (id == IDM_EDIT_PADWITHSPACES) {
        PadDocumentLines(d);
        return true;
    }
    for (auto item : cases)
        if (item == id) {
            if (id == IDM_EDIT_MAP_HANJA_HANGUL)
                ::SendMessageW(d.hwnd, WM_IME_KEYDOWN, VK_HANJA, 0);
            else
                ApplyDocumentCaseMapping(d, id);
            return true;
        }
    if (id == IDM_EDIT_LINECOMMENT || id == IDM_EDIT_STREAMCOMMENT) {
        ToggleDocumentComment(d, id == IDM_EDIT_STREAMCOMMENT);
        return true;
    }
    if (id == IDM_EDIT_STRIP1STCHAR || id == IDM_EDIT_STRIPLASTCHAR) {
        if (d.Send(SCI_SELECTIONISRECTANGLE))
            return true; // Same restriction as upstream.
        auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
        auto first = d.Send(SCI_LINEFROMPOSITION, start), last = d.Send(SCI_LINEFROMPOSITION, end);
        if (first != last) {
            if (id == IDM_EDIT_STRIP1STCHAR) {
                if (start > d.Send(SCI_POSITIONFROMLINE, first))
                    ++first;
                if (end <= d.Send(SCI_POSITIONFROMLINE, last))
                    --last;
            } else {
                if (start >= d.Send(SCI_GETLINEENDPOSITION, first))
                    ++first;
                if (end < d.Send(SCI_GETLINEENDPOSITION, last))
                    --last;
            }
        }
        d.Send(SCI_BEGINUNDOACTION);
        for (auto line = last; line >= first; --line) {
            auto begin = d.Send(SCI_POSITIONFROMLINE, line), finish = d.Send(SCI_GETLINEENDPOSITION, line);
            if (finish > begin) {
                auto position = id == IDM_EDIT_STRIP1STCHAR ? begin : d.Send(SCI_POSITIONBEFORE, finish);
                auto limit = id == IDM_EDIT_STRIP1STCHAR ? d.Send(SCI_POSITIONAFTER, begin) : finish;
                d.Send(SCI_DELETERANGE, position, limit - position);
            }
            if (line == 0)
                break;
        }
        d.Send(SCI_ENDUNDOACTION);
        return true;
    }
    Range r = Selection(d);
    std::string text = Text(d, r), out = text;
    bool lineCommand = false;
    for (auto item : insertions)
        if (item == id) {
            SYSTEMTIME time{};
            id == IDM_EDIT_INSERT_UTC_DATETIME ? GetSystemTime(&time) : GetLocalTime(&time);
            wchar_t date[256]{}, clock[256]{};
            switch (id) {
            case IDM_EDIT_INSERT_GUID: {
                GUID guid{};
                if (SUCCEEDED(CoCreateGuid(&guid))) {
                    char value[40]{};
                    sprintf_s(value, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                              static_cast<unsigned>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
                              guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
                    out = value;
                }
                break;
            }
            case IDM_EDIT_INSERT_ENCODING:
                out = InsertEncodingName(d);
                if (const auto *lexer = GetDocumentLexer(d);
                    lexer && (lexer->iLexer == SCLEX_PYTHON || lexer->iLexer == SCLEX_RUBY)) {
                    auto position = d.Send(SCI_GETCURRENTPOS), line = d.Send(SCI_LINEFROMPOSITION, position);
                    if (line < 2 && position == d.Send(SCI_POSITIONFROMLINE, line))
                        out = "#-*- coding: " + out + " -*-";
                }
                break;
            case IDM_EDIT_INSERT_FILENAME:
                out = files::ToUtf8(d.path.empty() ? d.untitled : d.path.filename().wstring());
                break;
            case IDM_EDIT_INSERT_PATHNAME:
                out = files::ToUtf8(d.path.empty() ? d.untitled : d.path.wstring());
                break;
            case CMD_INSERTFILENAME_NOEXT:
                out = files::ToUtf8(d.path.empty() ? d.untitled : d.path.stem().wstring());
                break;
            case IDM_EDIT_INSERT_TIMESTAMP:
            case IDM_EDIT_INSERT_TIMESTAMP_MS:
            case IDM_EDIT_INSERT_TIMESTAMP_US:
            case IDM_EDIT_INSERT_TIMESTAMP_NS: {
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
                auto divisor = id == IDM_EDIT_INSERT_TIMESTAMP      ? 1000000000LL
                               : id == IDM_EDIT_INSERT_TIMESTAMP_MS ? 1000000LL
                               : id == IDM_EDIT_INSERT_TIMESTAMP_US ? 1000LL
                                                                    : 1LL;
                out = std::to_string(ns / divisor);
                break;
            }
            case IDM_EDIT_INSERT_LOC_DATE:
                swprintf_s(date, L"%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
                out = files::ToUtf8(date);
                break;
            case IDM_EDIT_INSERT_LOC_DATETIME:
                swprintf_s(date, L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour,
                           time.wMinute, time.wSecond);
                out = files::ToUtf8(date);
                break;
            case IDM_EDIT_INSERT_UTC_DATETIME:
                swprintf_s(date, L"%04u-%02u-%02uT%02u:%02u:%02uZ", time.wYear, time.wMonth, time.wDay, time.wHour,
                           time.wMinute, time.wSecond);
                out = files::ToUtf8(date);
                break;
            default: {
                const auto &format = id == IDM_EDIT_INSERT_SHORTDATE ? d.runtime.dateTimeShort : d.runtime.dateTimeLong;
                if (!format.empty()) {
                    auto now = std::time(nullptr);
                    std::tm local{};
                    localtime_s(&local, &now);
                    if (!wcsftime(date, 256, format.c_str(), &local))
                        throw std::runtime_error("Date/time template is invalid or too long; document retained.");
                    out = files::ToUtf8(date);
                } else {
                    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,
                                    id == IDM_EDIT_INSERT_LONGDATE ? DATE_LONGDATE : DATE_SHORTDATE, &time, nullptr,
                                    date, 256, nullptr);
                    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, clock, 256);
                    out = files::ToUtf8(clock) + " " + files::ToUtf8(date);
                }
                break;
            }
            }
            Replace(d, r, out);
            return true;
        }
    switch (id) {
    case IDM_EDIT_CLEARCLIPBOARD:
        if (OpenClipboard(d.hwnd)) {
            EmptyClipboard();
            CloseClipboard();
        }
        return true;
    case IDM_EDIT_COPYADD: {
        if (d.Send(SCI_SELECTIONISRECTANGLE))
            return true;
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            d.Send(SCI_COPY);
            return true;
        }
        auto added = r.start == r.end ? d.Text() : text;
        if (!added.empty())
            Copy(d, Clipboard(d.hwnd) + "\r\n\r\n" + added);
        return true;
    }
    case IDM_EDIT_SWAP: {
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
            return true;
        out = Clipboard(d.hwnd);
        const bool backwards = d.Send(SCI_GETCURRENTPOS) < d.Send(SCI_GETANCHOR);
        if (r.start == r.end) {
            Replace(d, r, out);
            if (OpenEditClipboard(d.hwnd)) {
                EmptyClipboard();
                CloseClipboard();
            }
        } else {
            Copy(d, text);
            Replace(d, r, out);
            d.Send(SCI_SETSEL, backwards ? r.start + out.size() : r.start, backwards ? r.start : r.start + out.size());
        }
        return true;
    }
    case CMD_COPYFILENAME:
        Copy(d, files::ToUtf8(d.path.empty() ? d.untitled : d.path.filename().wstring()));
        return true;
    case CMD_COPYFILENAME_NOEXT:
        Copy(d, files::ToUtf8(d.path.empty() ? d.untitled : d.path.stem().wstring()));
        return true;
    case CMD_COPYPATHNAME:
        Copy(d, files::ToUtf8(d.path.empty() ? d.untitled : d.path.wstring()));
        return true;
    case CMD_COPYWINPOS: {
        RECT rect{};
        GetWindowRect(GetAncestor(d.hwnd, GA_ROOT), &rect);
        Copy(d, std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
                    std::to_string(rect.right - rect.left) + "," + std::to_string(rect.bottom - rect.top));
        return true;
    }
    case IDM_EDIT_CODE_PRETTY:
    case IDM_EDIT_CODE_COMPRESS:
        FormatDocumentCode(d, id == IDM_EDIT_CODE_PRETTY);
        return true;
    case IDM_EDIT_COPYRTF:
        CopyRichText(d);
        return true;
    case IDM_EDIT_SHOW_CHAR_INFO:
        ShowCharacterInfo(d);
        return true;
    case IDM_EDIT_SHOW_HEX: {
        if (!text.empty()) {
            auto hex = "[" + Hex(SelectionBytes(d, text)) + "]";
            Replace(d, {r.end, r.end}, hex);
        }
        return true;
    }
    case IDM_EDIT_JOINLINESEX: {
        if (r.start == r.end)
            return true;
        r.start = d.Send(SCI_POSITIONFROMLINE, d.Send(SCI_LINEFROMPOSITION, r.start));
        text = Text(d, r);
        out.clear();
        const auto eol = Eol(d);
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r' || text[i] == '\n') {
                if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                    ++i;
                if (i + 1 < text.size() && text[i + 1] != '\r' && text[i + 1] != '\n')
                    out += ' ';
                else {
                    while (i + 1 < text.size() && (text[i + 1] == '\r' || text[i + 1] == '\n'))
                        ++i;
                    if (i + 1 < text.size()) {
                        if (!out.empty())
                            out += eol;
                        out += eol;
                    }
                }
            } else
                out += text[i];
        }
        Replace(d, r, out);
        return true;
    }
    case IDM_EDIT_SPLITLINES:
        d.Send(SCI_TARGETFROMSELECTION);
        d.Send(SCI_LINESSPLIT);
        return true;
    case IDM_EDIT_JOINLINES:
        d.Send(SCI_TARGETFROMSELECTION);
        d.Send(SCI_LINESJOIN);
        return true;
    case CMD_ENCLOSE_TRIPLE_SQ:
        Enclose(d, r, std::string(3, '\''), std::string(3, '\''));
        return true;
    case CMD_ENCLOSE_TRIPLE_DQ:
        Enclose(d, r, "\"\"\"", "\"\"\"");
        return true;
    case CMD_ENCLOSE_TRIPLE_BT:
        Enclose(d, r, "```", "```");
        return true;
    case IDM_EDIT_ENCLOSESELECTION:
    case IDM_EDIT_INSERT_XMLTAG: {
        Options o;
        o.resource = id == IDM_EDIT_ENCLOSESELECTION ? IDD_ENCLOSESELECTION : IDD_INSERTTAG;
        if (!Dialog(d, o))
            return true;
        Enclose(d, r, o.prefix, o.suffix);
        return true;
    }
    case IDM_EDIT_STREAMCOMMENT: {
        std::string start = "/*", end = "*/";
        if (d.language == L"Python") {
            start = end = "\"\"\"";
        }
        if (text.starts_with(start) && text.ends_with(end))
            out = text.substr(start.size(), text.size() - start.size() - end.size());
        else
            out = start + text + end;
        break;
    }
    case IDM_EDIT_BASE64_ENCODE:
    case IDM_EDIT_BASE64_SAFE_ENCODE:
    case IDM_EDIT_BASE64_HTML_EMBEDDED_IMAGE:
        out = Base64(SelectionBytes(d, text), id == IDM_EDIT_BASE64_SAFE_ENCODE);
        if (id == IDM_EDIT_BASE64_HTML_EMBEDDED_IMAGE)
            out = "<img src=\"data:image/" +
                  files::ToUtf8(d.path.extension().wstring()).substr(d.path.has_extension() ? 1 : 0) + ";base64," +
                  out + "\" />";
        break;
    case IDM_EDIT_BASE64_DECODE:
    case IDM_EDIT_BASE64_DECODE_AS_HEX:
        out = Unbase64(text);
        if (id == IDM_EDIT_BASE64_DECODE_AS_HEX) {
            std::string hex;
            for (size_t i = 0; i < out.size(); i += 16) {
                if (i)
                    hex += Eol(d);
                hex += Hex(out.substr(i, 16));
            }
            out = std::move(hex);
        } else
            out = DecodedBytes(d, out);
        break;
    case IDM_EDIT_URLENCODE:
    case IDM_EDIT_URLCOMPONENTENCODE: {
        auto wide = files::FromUtf8(text);
        auto first = wide.find_first_not_of(L" \a\b\f\n\r\t\v");
        if (first == std::wstring::npos)
            return true;
        auto last = wide.find_last_not_of(L" \a\b\f\n\r\t\v");
        wide = wide.substr(first, last - first + 1);
        DWORD capacity = static_cast<DWORD>(min<size_t>(MAXDWORD, wide.size() * 12 + 16));
        std::wstring escaped(capacity, L'\0');
        DWORD flags = URL_ESCAPE_AS_UTF8;
        if (id == IDM_EDIT_URLCOMPONENTENCODE)
            flags |= URL_ESCAPE_ASCII_URI_COMPONENT | URL_ESCAPE_SEGMENT_ONLY;
        auto status = UrlEscapeW(wide.c_str(), escaped.data(), &capacity, flags);
        if (FAILED(status))
            throw std::runtime_error("Cannot URL-encode the selected text.");
        escaped.resize(capacity);
        out = files::ToUtf8(escaped);
        break;
    }
    case IDM_EDIT_URLDECODE: {
        auto wide = files::FromUtf8(text);
        DWORD capacity = static_cast<DWORD>(wide.size() + 1);
        std::wstring unescaped(capacity, L'\0');
        auto status = UrlUnescapeW(wide.data(), unescaped.data(), &capacity, URL_UNESCAPE_AS_UTF8);
        if (FAILED(status))
            throw std::runtime_error("Cannot URL-decode the selected text.");
        unescaped.resize(capacity);
        out = files::ToUtf8(unescaped);
        break;
    }
    case IDM_EDIT_NUM2BIN:
    case IDM_EDIT_NUM2OCT:
    case IDM_EDIT_NUM2DEC:
    case IDM_EDIT_NUM2HEX:
        out = ConvertNumbers(text, id == IDM_EDIT_NUM2BIN   ? 2
                                   : id == IDM_EDIT_NUM2OCT ? 8
                                   : id == IDM_EDIT_NUM2DEC ? 10
                                                            : 16);
        break;
    case CMD_INCREASENUM:
    case CMD_DECREASENUM:
        out = ModifyNumber(text, id == CMD_INCREASENUM);
        break;
    default:
        lineCommand = true;
        break;
    }
    if (!lineCommand) {
        Replace(d, r, out);
        return true;
    }
    const bool rectangular = d.Send(SCI_SELECTIONISRECTANGLE) != 0;
    const auto oldCaret = d.Send(SCI_GETCURRENTPOS), oldAnchor = d.Send(SCI_GETANCHOR);
    const auto caretLine = d.Send(SCI_LINEFROMPOSITION, oldCaret), anchorLine = d.Send(SCI_LINEFROMPOSITION, oldAnchor);
    const auto caretColumn = d.Send(SCI_GETCOLUMN, oldCaret), anchorColumn = d.Send(SCI_GETCOLUMN, oldAnchor);
    r = Selection(d, true);
    if ((id == IDM_EDIT_MODIFYLINES || id == IDM_EDIT_ALIGN) && d.Send(SCI_GETSELECTIONEMPTY)) {
        auto line = d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETCURRENTPOS));
        r.start = d.Send(SCI_POSITIONFROMLINE, line);
        r.end = line + 1 < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, line + 1) : d.Send(SCI_GETLENGTH);
    }
    if (id == IDM_EDIT_COLUMNWRAP)
        r.end = d.Send(SCI_GETSELECTIONEND);
    if (id == IDM_EDIT_MERGEBLANKLINES || id == IDM_EDIT_REMOVEBLANKLINES) {
        auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
        if (start == end) {
            start = 0;
            end = d.Send(SCI_GETLENGTH);
        }
        auto first = d.Send(SCI_LINEFROMPOSITION, start), last = d.Send(SCI_LINEFROMPOSITION, end);
        if (start > d.Send(SCI_POSITIONFROMLINE, first))
            ++first;
        if (end <= d.Send(SCI_POSITIONFROMLINE, last) && last != d.Send(SCI_GETLINECOUNT) - 1)
            --last;
        if (last < first)
            return true;
        r.start = d.Send(SCI_POSITIONFROMLINE, first);
        r.end = last + 1 < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, last + 1) : d.Send(SCI_GETLENGTH);
    }
    if ((id == IDM_EDIT_STRIP1STCHAR || id == IDM_EDIT_STRIPLASTCHAR) && d.Send(SCI_GETSELECTIONEMPTY)) {
        const auto line = d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETCURRENTPOS));
        r.start = d.Send(SCI_POSITIONFROMLINE, line);
        r.end = d.Send(SCI_GETLINEENDPOSITION, line);
    }
    if (rectangular) {
        const auto firstLine = min(caretLine, anchorLine), lastLine = max(caretLine, anchorLine);
        r.start = d.Send(SCI_POSITIONFROMLINE, firstLine);
        r.end = lastLine + 1 < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, lastLine + 1)
                                                        : d.Send(SCI_GETLENGTH);
    }
    auto lines = Lines(Text(d, r));
    if (lines.empty() && id == IDM_EDIT_MODIFYLINES)
        lines.push_back({"", ""});
    std::unordered_map<std::string, size_t> duplicateCount;
    if (id == IDM_EDIT_MERGEDUPLICATELINE || id == IDM_EDIT_REMOVEDUPLICATELINE)
        for (const auto &line : lines)
            ++duplicateCount[line.text];
    Options options;
    options.columnSort = rectangular;
    if (id == IDM_EDIT_SORTLINES || id == IDM_EDIT_MODIFYLINES || id == IDM_EDIT_ALIGN || id == IDM_EDIT_COLUMNWRAP) {
        options.resource = id == IDM_EDIT_SORTLINES     ? IDD_SORT
                           : id == IDM_EDIT_MODIFYLINES ? IDD_MODIFYLINES
                           : id == IDM_EDIT_ALIGN       ? IDD_ALIGN
                                                        : IDD_COLUMNWRAP;
        if (!Dialog(d, options))
            return true;
    }
    if (id == IDM_EDIT_MODIFYLINES) {
        if (options.prefix.empty() && options.suffix.empty())
            return true;
        struct Insertion {
            LRESULT start, end;
            std::string prefix, suffix;
        };
        std::vector<Insertion> lineInsertions;
        lineInsertions.reserve(lines.size());
        auto position = r.start;
        size_t modified = 0;
        const auto firstLine = d.Send(SCI_LINEFROMPOSITION, r.start);
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto &line = lines[i];
            auto end = position + static_cast<LRESULT>(line.text.size());
            if (!options.skip || !line.text.empty()) {
                lineInsertions.push_back({position, end,
                                      ExpandLineVariables(options.prefix, firstLine + i + 1, modified, lines.size(),
                                                          firstLine + lines.size()),
                                      ExpandLineVariables(options.suffix, firstLine + i + 1, modified, lines.size(),
                                                          firstLine + lines.size())});
                ++modified;
            }
            position = end + static_cast<LRESULT>(line.eol.size());
        }
        d.Send(SCI_BEGINUNDOACTION);
        for (auto item = lineInsertions.rbegin(); item != lineInsertions.rend(); ++item) {
            if (!item->suffix.empty()) {
                d.Send(SCI_SETTARGETRANGE, item->end, item->end);
                d.Send(SCI_REPLACETARGET, item->suffix.size(), reinterpret_cast<LPARAM>(item->suffix.data()));
            }
            if (!item->prefix.empty()) {
                d.Send(SCI_SETTARGETRANGE, item->start, item->start);
                d.Send(SCI_REPLACETARGET, item->prefix.size(), reinterpret_cast<LPARAM>(item->prefix.data()));
            }
        }
        d.Send(SCI_ENDUNDOACTION);
        if (oldCaret != oldAnchor) {
            auto start = d.Send(SCI_POSITIONFROMLINE, firstLine),
                 end = d.Send(SCI_POSITIONFROMLINE, firstLine + lines.size());
            d.Send(SCI_SETSEL, oldCaret < oldAnchor ? end : start, oldCaret < oldAnchor ? start : end);
        }
        return true;
    }
    const int tab = static_cast<int>(d.Send(SCI_GETTABWIDTH));
    size_t minIndent = SIZE_MAX;
    if (id == IDM_EDIT_ALIGN) {
        for (const auto &line : lines) {
            if (Blank(line.text))
                continue;
            size_t indent = 0;
            for (char character : line.text) {
                if (character == ' ')
                    ++indent;
                else if (character == '\t')
                    indent += tab - indent % tab;
                else
                    break;
            }
            minIndent = min(minIndent, indent);
        }
        if (minIndent == SIZE_MAX)
            minIndent = 0;
    }
    auto displayWidth = [&](const std::string &text) {
        auto wide = files::FromUtf8(text);
        size_t column = 0;
        for (size_t i = 0; i < wide.size(); ++i) {
            if (wide[i] == L'\t')
                column += tab - column % tab;
            else {
                ++column;
                if (wide[i] >= 0xD800 && wide[i] <= 0xDBFF && i + 1 < wide.size())
                    ++i;
            }
        }
        return column;
    };
    size_t width = 0;
    for (auto &line : lines)
        width = max(width, displayWidth(line.text));
    std::vector<Line> result;
    std::set<std::string> seen;
    bool wasBlank = false;
    auto eol = Eol(d);
    size_t lineIndex = 0, modifiedIndex = 0;
    const auto firstDocumentLine = d.Send(SCI_LINEFROMPOSITION, r.start);
    for (auto line : lines) {
        const size_t currentLine = firstDocumentLine + lineIndex++ + 1;
        switch (id) {
        case IDM_EDIT_TRIMLINES:
            TrimRight(line.text);
            break;
        case IDM_EDIT_TRIMLEAD:
            TrimLeft(line.text);
            break;
        case IDM_EDIT_STRIP1STCHAR: {
            auto wide = files::FromUtf8(line.text);
            if (!wide.empty()) {
                size_t count = wide.size() > 1 && wide[0] >= 0xD800 && wide[0] <= 0xDBFF ? 2 : 1;
                wide.erase(0, count);
            }
            line.text = files::ToUtf8(wide);
            break;
        }
        case IDM_EDIT_STRIPLASTCHAR: {
            auto wide = files::FromUtf8(line.text);
            if (!wide.empty()) {
                wchar_t last = wide.back();
                wide.pop_back();
                if (last >= 0xDC00 && last <= 0xDFFF && !wide.empty())
                    wide.pop_back();
            }
            line.text = files::ToUtf8(wide);
            break;
        }
        case IDM_EDIT_PADWITHSPACES:
            line.text.append(width - displayWidth(line.text), ' ');
            break;
        case IDM_EDIT_COMPRESSWS: {
            TrimLeft(line.text);
            TrimRight(line.text);
            std::string compressed;
            bool blank = false;
            for (char c : line.text) {
                if (c == ' ' || c == '\t') {
                    if (!blank)
                        compressed += ' ';
                    blank = true;
                } else {
                    compressed += c;
                    blank = false;
                }
            }
            line.text = compressed;
            break;
        }
        case IDM_EDIT_CONVERTTABS:
        case IDM_EDIT_CONVERTTABS2: {
            std::string converted;
            int column = 0;
            bool leading = true;
            for (char c : line.text) {
                if (c == '\t' && (id == IDM_EDIT_CONVERTTABS || leading)) {
                    int spaces = tab - column % tab;
                    converted.append(spaces, ' ');
                    column += spaces;
                } else {
                    converted += c;
                    ++column;
                }
                if (c != ' ' && c != '\t')
                    leading = false;
            }
            line.text = converted;
            break;
        }
        case IDM_EDIT_CONVERTSPACES:
        case IDM_EDIT_CONVERTSPACES2: {
            std::string converted;
            size_t column = 0;
            bool leading = true;
            for (size_t i = 0; i < line.text.size();) {
                if (line.text[i] == ' ' && (id == IDM_EDIT_CONVERTSPACES || leading)) {
                    size_t count = 0;
                    while (i + count < line.text.size() && line.text[i + count] == ' ')
                        ++count;
                    while (count) {
                        size_t step = tab - column % tab;
                        if (count >= step && step > 1) {
                            converted += '\t';
                            column += step;
                            i += step;
                            count -= step;
                        } else {
                            converted += ' ';
                            ++column;
                            ++i;
                            --count;
                        }
                    }
                } else {
                    char c = line.text[i++];
                    converted += c;
                    column = c == '\t' ? column + tab - column % tab : column + 1;
                    if (c != '\t' && c != ' ')
                        leading = false;
                }
            }
            line.text = converted;
            break;
        }
        case IDM_EDIT_MERGEBLANKLINES:
            if (Blank(line.text)) {
                if (wasBlank)
                    continue;
                wasBlank = true;
            } else
                wasBlank = false;
            break;
        case IDM_EDIT_REMOVEBLANKLINES:
            if (Blank(line.text))
                continue;
            break;
        case IDM_EDIT_MERGEDUPLICATELINE:
            if (!seen.insert(line.text).second)
                continue;
            break;
        case IDM_EDIT_REMOVEDUPLICATELINE:
            if (duplicateCount[line.text] > 1)
                continue;
            break;
        case IDM_EDIT_LINECOMMENT: {
            std::string marker = d.language == L"Python" ? "#" : "//";
            auto start = line.text.find_first_not_of(" \t");
            if (start == std::string::npos)
                start = 0;
            if (line.text.compare(start, marker.size(), marker) == 0) {
                line.text.erase(start, marker.size());
                if (start < line.text.size() && line.text[start] == ' ')
                    line.text.erase(start, 1);
            } else
                line.text.insert(start, marker + " ");
            break;
        }
        case IDM_EDIT_MODIFYLINES:
            if (!(options.skip && line.text.empty())) {
                line.text = ExpandLineVariables(options.prefix, currentLine, modifiedIndex, lines.size(),
                                                firstDocumentLine + lines.size()) +
                            line.text +
                            ExpandLineVariables(options.suffix, currentLine, modifiedIndex, lines.size(),
                                                firstDocumentLine + lines.size());
                ++modifiedIndex;
            }
            break;
        case IDM_EDIT_ALIGN: {
            TrimLeft(line.text);
            TrimRight(line.text);
            if (line.text.empty())
                break;
            // Upstream normalizes word separators before measuring alignment.
            line.text = Justify(line.text, files::FromUtf8(line.text).size());
            auto normalized = line.text;
            std::string compact;
            bool whitespace = false;
            for (char character : normalized) {
                if (character == ' ' || character == '\t') {
                    if (!whitespace)
                        compact += ' ';
                    whitespace = true;
                } else {
                    compact += character;
                    whitespace = false;
                }
            }
            line.text = compact;
            size_t contentWidth = width > minIndent ? width - minIndent : width;
            size_t length = files::FromUtf8(line.text).size();
            if (options.align == IDC_ALIGN_RIGHT)
                line.text.insert(0, contentWidth > length ? contentWidth - length : 0, ' ');
            else if (options.align == IDC_ALIGN_CENTER)
                line.text.insert(0, contentWidth > length ? (contentWidth - length) / 2 : 0, ' ');
            else if (options.align == IDC_ALIGN_JUSTIFY || options.align == IDC_ALIGN_JUSTIFY_PAR) {
                bool finalParagraphLine = lineIndex >= lines.size() || Blank(lines[lineIndex].text);
                size_t letters = 0;
                for (char character : line.text)
                    if (character != ' ' && character != '\t')
                        ++letters;
                if (options.align != IDC_ALIGN_JUSTIFY_PAR || !finalParagraphLine || lines.size() == 1 ||
                    letters * 4 > contentWidth * 3)
                    line.text = Justify(line.text, contentWidth);
            }
            line.text.insert(0, minIndent, ' ');
            break;
        }
        }
        result.push_back(std::move(line));
    }
    if (id == IDM_EDIT_SORTLINES) {
        auto fullKey = [&](const Line &line) {
            auto wide = files::FromUtf8(line.text);
            if (options.ignoreCase && !wide.empty()) {
                int size = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, wide.data(),
                                         static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
                std::wstring mapped(size, L'\0');
                if (size && LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, wide.data(),
                                          static_cast<int>(wide.size()), mapped.data(), size, nullptr, nullptr, 0))
                    wide = std::move(mapped);
            }
            return wide;
        };
        auto key = [&](const Line &line) {
            auto wide = fullKey(line);
            if (options.columnSort) {
                size_t position = 0;
                LRESULT column = 0;
                const auto target = rectangular ? min(caretColumn, anchorColumn) : caretColumn;
                while (position < wide.size() && column < target) {
                    if (wide[position] == L'\t') {
                        auto next = column + tab - column % tab;
                        if (next > target)
                            break;
                        column = next;
                        ++position;
                    } else {
                        ++column;
                        if (wide[position] >= 0xD800 && wide[position] <= 0xDBFF && position + 1 < wide.size())
                            ++position;
                        ++position;
                    }
                }
                wide.erase(0, position);
            }
            return wide;
        };
        if (options.order == IDC_SORT_SHUFFLE) {
            std::mt19937 engine(std::random_device{}());
            std::shuffle(result.begin(), result.end(), engine);
        } else if (options.order != IDC_SORT_NONE)
            std::stable_sort(result.begin(), result.end(), [&](const Line &a, const Line &b) {
                auto x = key(a), y = key(b);
                int compare = 0;
                if (options.group) {
                    auto ext = [](const std::wstring &s) {
                        auto p = s.find_last_of(L'.');
                        return p == std::wstring::npos ? std::wstring{} : s.substr(p);
                    };
                    compare = ext(x).compare(ext(y));
                }
                if (!compare)
                    compare = options.logical ? StrCmpLogicalW(x.c_str(), y.c_str()) : x.compare(y);
                if (!compare && (options.columnSort || options.group)) {
                    auto fullA = fullKey(a), fullB = fullKey(b);
                    compare = options.logical ? StrCmpLogicalW(fullA.c_str(), fullB.c_str()) : fullA.compare(fullB);
                }
                return options.order == IDC_SORT_DESC ? compare > 0 : compare < 0;
            });
        std::map<std::wstring, size_t> count;
        for (const auto &line : result)
            ++count[fullKey(line)];
        std::set<std::wstring> emitted;
        std::erase_if(result, [&](const Line &line) {
            auto k = fullKey(line);
            return (options.remove && count[k] > 1) || (options.unique && count[k] == 1) ||
                   (options.merge && !emitted.insert(k).second);
        });
        // EOL belongs to positions, not sorted payloads; preserve the original final-line state.
        auto original = Lines(Text(d, r));
        for (size_t i = 0; i < result.size(); ++i)
            result[i].eol = i + 1 < result.size() ? eol : (!original.empty() ? original.back().eol : std::string{});
    }
    if (id == IDM_EDIT_MERGEDUPLICATELINE || id == IDM_EDIT_REMOVEDUPLICATELINE) {
        for (size_t i = 0; i < result.size(); ++i)
            result[i].eol = i + 1 < result.size() ? eol : (!lines.empty() ? lines.back().eol : std::string{});
    }
    if (id == IDM_EDIT_JOINLINESEX) {
        out.clear();
        for (auto &line : result) {
            TrimLeft(line.text);
            TrimRight(line.text);
            if (!out.empty() && !line.text.empty())
                out += ' ';
            out += line.text;
        }
        if (!result.empty())
            out += result.back().eol;
    } else if (id == IDM_EDIT_COLUMNWRAP) {
        out.clear();
        for (auto &line : result) {
            auto wide = files::FromUtf8(line.text);
            while (wide.size() > static_cast<size_t>(options.column)) {
                size_t split = wide.rfind(L' ', options.column);
                if (split == std::wstring::npos || split == 0)
                    split = options.column;
                if (split < wide.size() && wide[split] >= 0xDC00 && wide[split] <= 0xDFFF) {
                    if (split == 1)
                        ++split;
                    else
                        --split;
                }
                if (!split)
                    split = min<size_t>(2, wide.size());
                out += files::ToUtf8(wide.substr(0, split)) + eol;
                wide.erase(0, split);
                while (!wide.empty() && wide.front() == L' ')
                    wide.erase(0, 1);
            }
            out += files::ToUtf8(wide) + line.eol;
        }
    } else {
        if (!result.empty() && !lines.empty())
            result.back().eol = lines.back().eol;
        out = Join(result);
    }
    Replace(d, r, out);
    if (rectangular && !result.empty()) {
        const auto first = min(caretLine, anchorLine), last = first + static_cast<LRESULT>(result.size()) - 1;
        auto newCaretLine = caretLine < anchorLine ? first : last;
        auto newAnchorLine = caretLine < anchorLine ? last : first;
        d.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
        d.Send(SCI_SETRECTANGULARSELECTIONCARET, d.Send(SCI_FINDCOLUMN, newCaretLine, caretColumn));
        d.Send(SCI_SETRECTANGULARSELECTIONANCHOR, d.Send(SCI_FINDCOLUMN, newAnchorLine, anchorColumn));
    }
    return true;
}

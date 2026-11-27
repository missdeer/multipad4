// Comment syntax tables from pinned notepad4 EditAutoC.cpp; see THIRD_PARTY.md.
#include "EditComments.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "CompletionEngine.h"
#include "EditSelection.h"
#include "StyleManager.h"
#include "resource.h"
#include <climits>
#include <cstring>
#ifdef MULTIPAD4_TESTING
#include "../tests/CommentMetrics.h"
#endif
namespace {
bool MarkerAt(const std::string &text, size_t position, const std::string &marker) {
    return position <= text.size() && marker.size() <= text.size() - position &&
           _strnicmp(text.c_str() + position, marker.c_str(), marker.size()) == 0;
}
size_t FindMarker(const std::string &text, const std::string &marker, size_t from, bool backwards = false) {
    if (marker.size() > text.size())
        return std::string::npos;
    if (backwards) {
        size_t position = min(from, text.size() - marker.size());
        for (;;) {
            if (MarkerAt(text, position, marker))
                return position;
            if (!position)
                break;
            --position;
        }
    } else
        for (size_t position = from; position <= text.size() - marker.size(); ++position)
            if (MarkerAt(text, position, marker))
                return position;
    return std::string::npos;
}
bool MarkerAt(EditorDocument &d, LRESULT position, const std::string &marker) {
    const auto length = d.Send(SCI_GETLENGTH);
    return position >= 0 && position <= length && marker.size() <= static_cast<size_t>(length - position) &&
           MarkerAt(d.Range(static_cast<size_t>(position), marker.size()), 0, marker);
}
LRESULT FindMarker(EditorDocument &d, const std::string &marker, LRESULT from, bool backwards = false) {
    const auto length = d.Send(SCI_GETLENGTH), markerLength = static_cast<LRESULT>(marker.size());
    if (from < 0 || markerLength > length) return -1;
    LRESULT size = 1024;
    if (backwards) {
        auto position = (std::min)(from, length - markerLength);
        for (;;) {
            const auto first = (std::max)(LRESULT(0), position - size + 1);
            const auto text = d.Range(static_cast<size_t>(first), static_cast<size_t>(position - first + markerLength));
            const auto found = FindMarker(text, marker, static_cast<size_t>(position - first), true);
            if (found != std::string::npos) return first + static_cast<LRESULT>(found);
            if (!first) return -1;
            position = first - 1; size = (std::min)(size * 2, LRESULT(65536));
        }
    }
    for (auto position = from; position <= length - markerLength;) {
        const auto last = position + (std::min)(size - 1, length - markerLength - position);
        const auto text = d.Range(static_cast<size_t>(position), static_cast<size_t>(last - position + markerLength));
        const auto found = FindMarker(text, marker, 0);
        if (found != std::string::npos) return position + static_cast<LRESULT>(found);
        position = last + 1; size = (std::min)(size * 2, LRESULT(65536));
    }
    return -1;
}
void StyleThrough(EditorDocument &d, LRESULT position) {
    const auto styled = d.Send(SCI_GETENDSTYLED);
    const auto length = d.Send(SCI_GETLENGTH);
    position = (std::min)(position, length);
    if (!position) return;
    const auto nextLine = d.Send(SCI_LINEFROMPOSITION, position - 1) + 1;
    const auto required = nextLine < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, nextLine) : length;
    if (styled < required) d.Send(SCI_COLOURISE, styled, position);
}
bool CommentSelection(EditorDocument &d, LRESULT first, LRESULT last, const CompletionCache &cache) {
    if (first == last) return cache.Test(cache.CommentStyleMask, static_cast<unsigned>(d.Send(SCI_GETSTYLEINDEXAT, first)));
    while (first < last) {
        const auto limit = first + (std::min)(last - first, LRESULT(65536)), count = limit - first;
        std::string packed(static_cast<size_t>(count) * 2 + 2, '\0');
        Sci_TextRangeFull range{{first, limit}, packed.data()};
        d.Send(SCI_GETSTYLEDTEXTFULL, 0, reinterpret_cast<LPARAM>(&range));
        // This fork packs N styles + NUL before N text bytes + NUL.
        const auto text = packed.data() + count + 1;
        auto position = first;
        while (position < limit) {
            const auto offset = static_cast<size_t>(position - first);
            if (!cache.Test(cache.CommentStyleMask, static_cast<unsigned char>(packed[offset]))) return false;
            position = static_cast<unsigned char>(text[offset]) < 128 && text[offset] != '\r' ? position + 1 : d.Send(SCI_POSITIONAFTER, position);
        }
        first = position;
    }
    return true;
}
int EmbeddedBlock(EditorDocument &d) {
    const auto position = d.Send(SCI_GETCURRENTPOS);
    StyleThrough(d, position + (position < d.Send(SCI_GETLENGTH)));
    const auto style = d.Send(SCI_GETSTYLEINDEXAT, position);
    if (style >= SCE_PHP_DEFAULT)
        return 5;
    if (style >= css_style(SCE_CSS_DEFAULT))
        return 6;
    if (style >= js_style(SCE_JS_DEFAULT))
        return 3;
    if (style >= SCE_HB_DEFAULT)
        return 4;
    if (style >= SCE_H_SGML_DEFAULT)
        return 2;
    if (style == SCE_H_CDATA)
        return 1;
    return 0;
}

std::string LineMarker(EditorDocument &d) {
    LPCWSTR pwszComment = nullptr;
    const auto *lexer = GetDocumentLexer(d);
    switch (lexer->rid) {
    case NP2LEX_ASM: {
        int choice = d.style.completionOptions.contains(IDC_ASM_LINE_COMMENT_SEMICOLON)
                         ? d.style.completionOptions.at(IDC_ASM_LINE_COMMENT_SEMICOLON)
                         : IDC_ASM_LINE_COMMENT_SEMICOLON;
        pwszComment = choice == IDC_ASM_LINE_COMMENT_SHARP   ? L"# "
                      : choice == IDC_ASM_LINE_COMMENT_SLASH ? L"//"
                      : choice == IDC_ASM_LINE_COMMENT_AT    ? L"@ "
                                                             : L";";
        break;
    }
    case NP2LEX_BASH:
        pwszComment = d.style.dialect == IDM_LEXER_M4 ? L"dnl " : L"#";
        break;
    case NP2LEX_CSS:
        if (d.style.dialect > IDM_LEXER_CSS)
            pwszComment = L"//";
        break;
    case NP2LEX_HTML:
    case NP2LEX_PHP:
    case NP2LEX_XML: {
        auto block = EmbeddedBlock(d);
        if (block == 4)
            pwszComment = L"'";
        else if (block == 1 || block == 3 || block == 5)
            pwszComment = L"//";
        break;
    }
    case NP2LEX_INNOSETUP:
        pwszComment = d.Send(SCI_GETLINESTATE, d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETSELECTIONSTART))) & (1 << 6)
                          ? L"//"
                          : L";";
        break;
    case NP2LEX_VIM:
        pwszComment = d.Send(SCI_GETLINESTATE, d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETSELECTIONSTART))) & (1 << 3)
                          ? L"#"
                          : L"\"";
        break;
    case NP2LEX_MATLAB:
        pwszComment = d.style.dialect == IDM_LEXER_SCILAB ? L"//" : L"%";
        break;
        // CommentLine++Autogenerated -- start of section automatically generated
    case NP2LEX_ABAQUS:
        pwszComment = L"**";
        break;

    case NP2LEX_ACTIONSCRIPT:
    case NP2LEX_ASYMPTOTE:
    case NP2LEX_BLOCKDIAG:
    case NP2LEX_CANGJIE:
    case NP2LEX_CIL:
    case NP2LEX_CPP:
    case NP2LEX_CSHARP:
    case NP2LEX_DART:
    case NP2LEX_DLANG:
    case NP2LEX_FSHARP:
    case NP2LEX_GO:
    case NP2LEX_GRADLE:
    case NP2LEX_GRAPHVIZ:
    case NP2LEX_GROOVY:
    case NP2LEX_HAXE:
    case NP2LEX_JAVA:
    case NP2LEX_JAVASCRIPT:
    case NP2LEX_JSON:
    case NP2LEX_KOTLIN:
    case NP2LEX_PASCAL:
    case NP2LEX_POWERBUILDER:
    case NP2LEX_RESOURCESCRIPT:
    case NP2LEX_RUST:
    case NP2LEX_SCALA:
    case NP2LEX_SWIFT:
    case NP2LEX_TYPESCRIPT:
    case NP2LEX_TYPST:
    case NP2LEX_VERILOG:
    case NP2LEX_WINHEX:
    case NP2LEX_ZIG:
        pwszComment = L"//";
        break;

    case NP2LEX_APDL:
    case NP2LEX_FORTRAN:
        pwszComment = L"!";
        break;

    case NP2LEX_AUTOHOTKEY:
    case NP2LEX_AUTOIT3:
    case NP2LEX_INI:
    case NP2LEX_LISP:
    case NP2LEX_LLVM:
    case NP2LEX_REBOL:
        pwszComment = L";";
        break;

    case NP2LEX_AVISYNTH:
    case NP2LEX_AWK:
    case NP2LEX_CMAKE:
    case NP2LEX_COFFEESCRIPT:
    case NP2LEX_CONFIG:
    case NP2LEX_ELIXIR:
    case NP2LEX_GN:
    case NP2LEX_JAMFILE:
    case NP2LEX_JULIA:
    case NP2LEX_MAKEFILE:
    case NP2LEX_NIM:
    case NP2LEX_NSIS:
    case NP2LEX_PERL:
    case NP2LEX_POWERSHELL:
    case NP2LEX_PYTHON:
    case NP2LEX_RLANG:
    case NP2LEX_RUBY:
    case NP2LEX_SMALI:
    case NP2LEX_TCL:
    case NP2LEX_TOML:
    case NP2LEX_YAML:
        pwszComment = L"#";
        break;

    case NP2LEX_BATCH:
        pwszComment = L"@rem ";
        break;

    case NP2LEX_ERLANG:
    case NP2LEX_LATEX:
        pwszComment = L"%";
        break;

    case NP2LEX_HASKELL:
    case NP2LEX_LUA:
    case NP2LEX_VHDL:
        pwszComment = L"--";
        break;

    case NP2LEX_SQL:
        pwszComment = L"-- ";
        break;

    case NP2LEX_TEXINFO:
        pwszComment = L"@c ";
        break;

    case NP2LEX_VBSCRIPT:
    case NP2LEX_VISUALBASIC:
        pwszComment = L"\'";
        break;

    case NP2LEX_WASM:
        pwszComment = L";;";
        break;
    }
    return pwszComment ? files::ToUtf8(pwszComment) : std::string{};
}
struct Block {
    std::string start, end;
    bool newline = false;
};
Block BlockMarker(EditorDocument &d) {
    LPCWSTR pwszOpen = nullptr, pwszClose = nullptr;
    bool newLine = false;
    const auto *lexer = GetDocumentLexer(d);
    switch (lexer->rid) {
    case NP2LEX_BLOCKDIAG:
    case NP2LEX_GRAPHVIZ: {
        auto state = d.Send(SCI_GETLINESTATE, d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETSELECTIONSTART)));
        pwszOpen = state ? L"<!--" : L"/*";
        pwszClose = state ? L"-->" : L"*/";
        break;
    } break;
    case NP2LEX_INNOSETUP: {
        auto state = d.Send(SCI_GETLINESTATE, d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETSELECTIONSTART)));
        if (state & (1 << 6)) {
            pwszOpen = L"{";
            pwszClose = L"}";
        } else if (state & (1 << 5)) {
            pwszOpen = L"/*";
            pwszClose = L"*/";
        }
        break;
    } break;
    case NP2LEX_HTML:
    case NP2LEX_PHP:
    case NP2LEX_XML: {
        auto block = EmbeddedBlock(d);
        if (block == 0) {
            pwszOpen = L"<!--";
            pwszClose = L"-->";
        } else if (block == 2) {
            pwszOpen = L"--";
            pwszClose = L"--";
        } else if (block != 4) {
            pwszOpen = L"/*";
            pwszClose = L"*/";
        }
    } break;
    case NP2LEX_MATLAB:
        if (d.style.dialect == IDM_LEXER_SCILAB) {
            pwszOpen = L"/*";
            pwszClose = L"*/";
        } else {
            pwszOpen = L"%{";
            pwszClose = L"%}";
            newLine = true;
        }
        break;
        // CommentBlock++Autogenerated -- start of section automatically generated
    case NP2LEX_ACTIONSCRIPT:
    case NP2LEX_ASM:
    case NP2LEX_ASYMPTOTE:
    case NP2LEX_AUTOHOTKEY:
    case NP2LEX_AVISYNTH:
    case NP2LEX_CANGJIE:
    case NP2LEX_CIL:
    case NP2LEX_CPP:
    case NP2LEX_CSHARP:
    case NP2LEX_CSS:
    case NP2LEX_DART:
    case NP2LEX_DLANG:
    case NP2LEX_GO:
    case NP2LEX_GRADLE:
    case NP2LEX_GROOVY:
    case NP2LEX_HAXE:
    case NP2LEX_JAVA:
    case NP2LEX_JAVASCRIPT:
    case NP2LEX_JSON:
    case NP2LEX_KOTLIN:
    case NP2LEX_NSIS:
    case NP2LEX_POWERBUILDER:
    case NP2LEX_RESOURCESCRIPT:
    case NP2LEX_RUST:
    case NP2LEX_SAS:
    case NP2LEX_SCALA:
    case NP2LEX_SQL:
    case NP2LEX_SWIFT:
    case NP2LEX_TYPESCRIPT:
    case NP2LEX_TYPST:
    case NP2LEX_VERILOG:
    case NP2LEX_VHDL:
        pwszOpen = L"/*";
        pwszClose = L"*/";
        break;

    case NP2LEX_AUTOIT3:
        pwszOpen = L"#cs";
        pwszClose = L"#ce";
        newLine = true;
        break;

    case NP2LEX_CMAKE:
        pwszOpen = L"#[[";
        pwszClose = L"]]";
        break;

    case NP2LEX_COFFEESCRIPT:
        pwszOpen = L"###";
        pwszClose = L"###";
        break;

    case NP2LEX_FSHARP:
    case NP2LEX_MATHEMATICA:
    case NP2LEX_OCAML:
        pwszOpen = L"(*";
        pwszClose = L"*)";
        break;

    case NP2LEX_HASKELL:
        pwszOpen = L"{-";
        pwszClose = L"-}";
        break;

    case NP2LEX_JAMFILE:
    case NP2LEX_LISP:
        pwszOpen = L"#|";
        pwszClose = L"|#";
        break;

    case NP2LEX_JULIA:
        pwszOpen = L"#=";
        pwszClose = L"=#";
        break;

    case NP2LEX_LATEX:
        pwszOpen = L"\\begin{comment}";
        pwszClose = L"\\end{comment}";
        newLine = true;
        break;

    case NP2LEX_LUA:
        pwszOpen = L"--[[";
        pwszClose = L"--]]";
        break;

    case NP2LEX_MARKDOWN:
        pwszOpen = L"<!--";
        pwszClose = L"-->";
        break;

    case NP2LEX_NIM:
        pwszOpen = L"#[";
        pwszClose = L"]#";
        break;

    case NP2LEX_PASCAL:
        pwszOpen = L"{";
        pwszClose = L"}";
        break;

    case NP2LEX_POWERSHELL:
        pwszOpen = L"<#";
        pwszClose = L"#>";
        break;

    case NP2LEX_REBOL:
        pwszOpen = L"comment {";
        pwszClose = L"}";
        newLine = true;
        break;

    case NP2LEX_TEXINFO:
        pwszOpen = L"@ignore";
        pwszClose = L"@end ignore";
        newLine = true;
        break;

    case NP2LEX_WASM:
        pwszOpen = L"(;";
        pwszClose = L";)";
        break;
    }
    return {pwszOpen ? files::ToUtf8(pwszOpen) : std::string{}, pwszClose ? files::ToUtf8(pwszClose) : std::string{},
            newLine};
}
} // namespace
void ToggleDocumentComment(EditorDocument &d, bool block) {
#ifdef MULTIPAD4_TESTING
#include "../tests/hooks/EditComments.cpp.01.inc"
#endif
    if (RejectRectangularEdit(d.hwnd))
        return;
    if (d.Send(SCI_GETREADONLY))
        return;
    auto marker = LineMarker(d);
    auto pair = BlockMarker(d);
    if (marker.empty())
        block = true;
    else if (pair.start.empty())
        block = false;
    if (block && pair.start.empty())
        return;
    if (block) {
        auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
        // Unwrap an exact selected block, or the innermost surrounding block.
        const bool exact = end - start >= static_cast<LRESULT>(pair.start.size() + pair.end.size()) &&
                           MarkerAt(d, start, pair.start) && MarkerAt(d, end - static_cast<LRESULT>(pair.end.size()), pair.end);
        bool knownNonComment = false;
        if (d.Send(SCI_GETENDSTYLED) >= d.Send(SCI_GETLENGTH)) {
            const auto &cache = GetCompletionCache(*GetDocumentLexer(d));
            knownNonComment = !cache.Test(cache.CommentStyleMask, static_cast<unsigned>(d.Send(SCI_GETSTYLEINDEXAT, start)));
        }
        LRESULT open = start, close = end - static_cast<LRESULT>(pair.end.size());
        bool surrounding = false;
        if (!exact && !knownNonComment) {
            open = FindMarker(d, pair.start, start, true);
            close = open < 0 ? -1 : FindMarker(d, pair.end, end);
            surrounding = open >= 0 && close >= end && FindMarker(d, pair.end, open + static_cast<LRESULT>(pair.start.size())) == close;
        }
        bool canUnwrap = false;
        if ((surrounding || exact) && !knownNonComment) {
            StyleThrough(d, (std::max)(end, start + (start < d.Send(SCI_GETLENGTH))));
            canUnwrap = CommentSelection(d, start, end, GetCompletionCache(*GetDocumentLexer(d)));
        }
        LRESULT openStart = 0, openEnd = 0, closeStart = 0, closeEnd = 0;
        if (canUnwrap) {
            openStart = static_cast<LRESULT>(open);
            openEnd = openStart + pair.start.size();
            closeStart = static_cast<LRESULT>(close);
            closeEnd = closeStart + pair.end.size();
            if (pair.newline) {
                auto openLine = d.Send(SCI_LINEFROMPOSITION, openStart),
                     closeLine = d.Send(SCI_LINEFROMPOSITION, closeStart);
                if (openLine == closeLine || d.Send(SCI_GETLINEINDENTPOSITION, openLine) != openStart ||
                    d.Send(SCI_GETLINEINDENTPOSITION, closeLine) != closeStart)
                    canUnwrap = false;
                else {
                    openStart = d.Send(SCI_POSITIONFROMLINE, openLine);
                    openEnd = d.Send(SCI_POSITIONFROMLINE, openLine + 1);
                    closeStart = d.Send(SCI_POSITIONFROMLINE, closeLine);
                    closeEnd = closeLine + 1 < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, closeLine + 1)
                                                                        : d.Send(SCI_GETLENGTH);
                }
            }
        }
        if (canUnwrap) {
            d.Send(SCI_BEGINUNDOACTION);
            d.Send(SCI_DELETERANGE, closeStart, closeEnd - closeStart);
            d.Send(SCI_DELETERANGE, openStart, openEnd - openStart);
        } else {
            const auto oldCaret = d.Send(SCI_GETCURRENTPOS), oldAnchor = d.Send(SCI_GETANCHOR);
            std::string prefix = pair.start, suffix = pair.end;
            if (pair.newline) {
                const std::string eol = d.Send(SCI_GETEOLMODE) == SC_EOL_CRLF ? "\r\n"
                                        : d.Send(SCI_GETEOLMODE) == SC_EOL_LF ? "\n"
                                                                              : "\r";
                prefix =
                    (start != d.Send(SCI_POSITIONFROMLINE, d.Send(SCI_LINEFROMPOSITION, start)) ? eol : std::string{}) +
                    pair.start + eol;
                suffix =
                    (end != d.Send(SCI_POSITIONFROMLINE, d.Send(SCI_LINEFROMPOSITION, end)) ? eol : std::string{}) +
                    pair.end + eol;
            }
            const auto result = prefix + d.Range(static_cast<size_t>(start), static_cast<size_t>(end - start)) + suffix;
            d.Send(SCI_BEGINUNDOACTION);
            d.Send(SCI_SETTARGETRANGE, start, end);
            d.Send(SCI_REPLACETARGET, result.size(), reinterpret_cast<LPARAM>(result.data()));
            const auto innerStart = start + prefix.size(), innerEnd = end + prefix.size();
            d.Send(SCI_SETSEL, oldCaret < oldAnchor ? innerEnd : innerStart,
                   oldCaret < oldAnchor ? innerStart : innerEnd);
        }
        d.Send(SCI_ENDUNDOACTION);
        return;
    }
    auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
    auto first = d.Send(SCI_LINEFROMPOSITION, start), last = d.Send(SCI_LINEFROMPOSITION, end);
    if (last > first && end == d.Send(SCI_POSITIONFROMLINE, last))
        --last;
    const bool forcedSpace = marker.back() == ' ';
    const bool spaceAfter = forcedSpace || !d.style.completionOptions.contains(IDC_AUTO_INSERT_SPACE_COMMENT) ||
                            d.style.completionOptions.at(IDC_AUTO_INSERT_SPACE_COMMENT) != 0;
    if (forcedSpace)
        marker.pop_back();
    bool remove = false, chosen = false;
    LRESULT column = LLONG_MAX;
    auto hasMarker = [&](LRESULT indent, LRESULT finish) {
        auto available = finish - indent;
        auto prefix = d.Range(indent, marker.size() + 1);
        return available >= static_cast<LRESULT>(marker.size()) &&
               _strnicmp(prefix.c_str(), marker.c_str(), marker.size()) == 0 &&
               (!forcedSpace || available == static_cast<LRESULT>(marker.size()) ||
                static_cast<unsigned char>(prefix[marker.size()]) <= ' ');
    };
    for (auto line = first; line <= last; ++line) {
        auto indent = d.Send(SCI_GETLINEINDENTPOSITION, line), finish = d.Send(SCI_GETLINEENDPOSITION, line);
        if (indent < finish) {
            column = min(column, d.Send(SCI_GETCOLUMN, indent));
            if (!chosen) {
                remove = hasMarker(indent, finish);
                chosen = true;
            }
        }
    }
    if (column == LLONG_MAX)
        column = 0;
    d.Send(SCI_BEGINUNDOACTION);
    for (auto line = last; line >= first; --line) {
        auto indent = d.Send(SCI_GETLINEINDENTPOSITION, line), finish = d.Send(SCI_GETLINEENDPOSITION, line);
        if (remove) {
            if (hasMarker(indent, finish)) {
                size_t size = marker.size();
                if (spaceAfter && indent + static_cast<LRESULT>(size) < finish && d.Send(SCI_GETCHARAT, indent + size) == ' ')
                    ++size;
                d.Send(SCI_DELETERANGE, indent, size);
            }
        } else if (indent < finish || first == last) {
            auto position = d.Send(SCI_FINDCOLUMN, line, column);
            if (hasMarker(indent, finish) && position == indent) {
                if (line == 0)
                    break;
                continue;
            }
            auto prefix = marker + (position < finish && spaceAfter ? " " : "");
            d.Send(SCI_INSERTTEXT, position, reinterpret_cast<LPARAM>(prefix.c_str()));
        }
        if (line == 0)
            break;
    }
    d.Send(SCI_ENDUNDOACTION);
}

#include "EditExtra.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "EditExpression.h"
#include "StyleManager.h"
#include "resource.h"
#include <ctime>
namespace {
void Replace(EditorDocument &d, LRESULT start, LRESULT end, const std::string &text) {
    d.Send(SCI_BEGINUNDOACTION);
    d.Send(SCI_SETTARGETRANGE, start, end);
    d.Send(SCI_REPLACETARGET, text.size(), reinterpret_cast<LPARAM>(text.data()));
    d.Send(SCI_SETSEL, start, start + text.size());
    d.Send(SCI_ENDUNDOACTION);
}
std::string Selected(EditorDocument &d) {
    auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
    return d.Range(start, end - start);
}
std::string Shebang(EditorDocument &d) {
    const auto *lexer = GetDocumentLexer(d);
    std::string name;
    switch (lexer->rid) {
    case NP2LEX_AWK:
        name = "awk";
        break;
    case NP2LEX_ERLANG:
        name = "escript";
        break;
    case NP2LEX_GROOVY:
        name = "groovy";
        break;
    case NP2LEX_JAVASCRIPT:
        name = "node";
        break;
    case NP2LEX_LUA:
        name = "lua";
        break;
    case NP2LEX_PERL:
        name = "perl";
        break;
    case NP2LEX_PHP:
        name = "php";
        break;
    case NP2LEX_PYTHON:
        name = "python3";
        break;
    case NP2LEX_RLANG:
        name = "Rscript";
        break;
    case NP2LEX_RUBY:
        name = "ruby";
        break;
    case NP2LEX_SCALA:
        name = "scala";
        break;
    case NP2LEX_TCL:
        name = "wish";
        break;
    }
    std::string text = "#!/usr/bin/env " + name;
    if (lexer->rid == NP2LEX_BASH) {
        if (d.style.dialect == IDM_LEXER_CSHELL)
            text = "#!/bin/csh";
        else if (d.style.dialect == IDM_LEXER_M4)
            text = "#!/usr/bin/env m4";
        else
            text = "#!/bin/bash";
    }
    if (d.Send(SCI_GETCURRENTPOS) == 0 && (!name.empty() || lexer->iLexer == SCLEX_BASH)) {
        auto mode = d.Send(SCI_GETEOLMODE);
        text += mode == SC_EOL_CRLF ? "\r\n" : mode == SC_EOL_LF ? "\n" : "\r";
    }
    return text;
}
} // namespace
CommandState ExtraEditState(const EditorDocument *d, UINT id) {
    bool modifies = true, supported = false;
    if (id >= IDM_INSERT_UNICODE_LRM && id <= IDM_INSERT_UNICODE_SHY)
        supported = true;
    switch (id) {
    case IDM_EDIT_SELECTLINE_BLOCK:
    case IDM_EDIT_SAVEFIND:
    case IDM_EDIT_SELTONEXT:
    case IDM_EDIT_SELTOPREV:
    case CMD_FINDNEXTSEL:
    case CMD_FINDPREVSEL:
    case CMD_JUMP2SELSTART:
    case CMD_JUMP2SELEND:
        modifies = false;
        supported = true;
        break;
    case IDM_EDIT_REPLACENEXT:
    case IDM_EDIT_INSERT_SHEBANG:
    case CMD_TIMESTAMPS:
    case CMD_CALCULATE_EXPR:
    case CMD_EVALUATE_JS_EXPR:
    case CMD_CTRLENTER:
        supported = true;
        break;
    }
    return {supported, supported && d && (!modifies || !d->Send(SCI_GETREADONLY)), false};
}
bool ExecuteExtraEdit(EditorDocument &d, UINT id, const std::function<bool(bool)> &confirmWrap) {
    auto state = ExtraEditState(&d, id);
    if (!state.supported)
        return false;
    if (!state.enabled)
        return true;
    if (id == CMD_CTRLENTER) {
        const bool autoIndent = d.style.autoIndent;
        d.style.autoIndent = !autoIndent;
        d.Send(SCI_NEWLINE);
        d.style.autoIndent = autoIndent;
        return true;
    }
    if (id >= IDM_INSERT_UNICODE_LRM && id <= IDM_INSERT_UNICODE_SHY) {
        constexpr wchar_t characters[] = {0x200E, 0x200F, 0x200D, 0x200C, 0x202A, 0x202B, 0x202D, 0x202E, 0x202C,
                                          0x206E, 0x206F, 0x206B, 0x206A, 0x206D, 0x206C, 0x1E,   0x1F,   0x2028,
                                          0x2029, 0x200B, 0x2060, 0x2066, 0x2067, 0x2068, 0x2069, 0x61C,  0xAD};
        auto text = files::ToUtf8(std::wstring(1, characters[id - IDM_INSERT_UNICODE_LRM]));
        Replace(d, d.Send(SCI_GETSELECTIONSTART), d.Send(SCI_GETSELECTIONEND), text);
        return true;
    }
    switch (id) {
    case CMD_CALCULATE_EXPR:
    case CMD_EVALUATE_JS_EXPR:
        try {
            EvaluateDocumentExpression(d, id == CMD_CALCULATE_EXPR);
        } catch (const std::exception &error) {
            d.Send(SCI_SHOWNOTIFICATION, d.Send(SCI_GETSELECTIONSTART) << 2, reinterpret_cast<LPARAM>(error.what()));
        }
        break;
    case CMD_JUMP2SELSTART:
    case CMD_JUMP2SELEND: {
        auto anchor = d.Send(SCI_GETANCHOR), caret = d.Send(SCI_GETCURRENTPOS);
        if ((id == CMD_JUMP2SELSTART && caret > anchor) || (id == CMD_JUMP2SELEND && caret < anchor)) {
            auto mode = d.Send(SCI_GETSELECTIONMODE);
            d.Send(SCI_SETSEL, caret, anchor);
            d.Send(SCI_SETSELECTIONMODE, mode);
            d.Send(SCI_CHOOSECARETX);
        }
        break;
    }
    case IDM_EDIT_SAVEFIND:
    case CMD_FINDNEXTSEL:
    case CMD_FINDPREVSEL: {
        auto selection = Selected(d);
        if (selection.empty()) {
            auto pos = d.Send(SCI_GETCURRENTPOS), start = d.Send(SCI_WORDSTARTPOSITION, pos, TRUE),
                 end = d.Send(SCI_WORDENDPOSITION, pos, TRUE);
            selection = d.Range(start, end - start);
        }
        if (selection.empty())
            break;
        d.search.findText = files::FromUtf8(selection);
        d.search.regex = d.search.wildcard = d.search.transformBackslashes = false;
        d.search.lastMatchStart = d.search.lastMatchEnd = -1;
        if (id != IDM_EDIT_SAVEFIND)
            d.FindNext(id == CMD_FINDPREVSEL, confirmWrap);
        break;
    }
    case IDM_EDIT_REPLACENEXT:
        d.ReplaceOne();
        d.FindNext(false, confirmWrap);
        break;
    case IDM_EDIT_SELTONEXT:
    case IDM_EDIT_SELTOPREV: {
        const bool backwards = id == IDM_EDIT_SELTOPREV;
        const auto oldAnchor = d.Send(SCI_GETANCHOR), oldCaret = d.Send(SCI_GETCURRENTPOS);
        struct RestoreWrap {
            bool &value;
            bool old;
            ~RestoreWrap() {
                value = old;
            }
        } restore{d.search.wrap, d.search.wrap};
        d.search.wrap = false;
        if (d.FindNext(backwards)) {
            auto caret = d.Send(backwards ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND);
            d.Send(SCI_SETSEL, backwards ? max(oldAnchor, oldCaret) : min(oldAnchor, oldCaret), caret);
        }
        break;
    }
    case IDM_EDIT_INSERT_SHEBANG:
        Replace(d, d.Send(SCI_GETSELECTIONSTART), d.Send(SCI_GETSELECTIONEND), Shebang(d));
        break;
    case CMD_TIMESTAMPS: {
        auto saved = d.search;
        auto now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        auto specification = d.runtime.timestampTemplate;
        auto delimiter = specification.find(L'|');
        if (delimiter == std::wstring::npos)
            break;
        auto pattern = specification.substr(0, delimiter), format = specification.substr(delimiter + 1);
        auto trim = [](std::wstring &text) {
            auto first = text.find_first_not_of(L" ");
            if (first == std::wstring::npos)
                text.clear();
            else {
                text.erase(0, first);
                text.resize(text.find_last_not_of(L" ") + 1);
            }
        };
        trim(pattern);
        trim(format);
        if (pattern.empty() || format.empty())
            break;
        wchar_t replacement[256]{};
        if (!wcsftime(replacement, 256, format.c_str(), &local))
            throw std::runtime_error("Timestamp template is invalid or too long; document retained.");
        d.search = {};
        d.search.regex = true;
        d.search.findText = pattern;
        d.search.replaceText = replacement;
        try {
            d.ReplaceAll(!d.Send(SCI_GETSELECTIONEMPTY));
        } catch (...) {
            d.search = std::move(saved);
            throw;
        }
        d.search = std::move(saved);
        break;
    }
    case IDM_EDIT_SELECTLINE_BLOCK: {
        auto caret = d.Send(SCI_GETCURRENTPOS), anchor = d.Send(SCI_GETANCHOR);
        auto line = d.Send(SCI_LINEFROMPOSITION, caret), start = line;
        auto level = d.Send(SCI_GETFOLDLEVEL, line);
        if (!(level & SC_FOLDLEVELHEADERFLAG))
            start = d.Send(SCI_GETFOLDPARENT, line);
        if (start < 0) {
            d.Send(SCI_SELECTALL);
            break;
        }
        auto last = d.Send(SCI_GETLASTCHILD, start, -1);
        bool backward = caret < anchor || (caret == anchor && last - line > line - start);
        auto begin = d.Send(SCI_POSITIONFROMLINE, start),
             end = last + 1 < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, last + 1) : d.Send(SCI_GETLENGTH);
        d.Send(SCI_SETSEL, backward ? end : begin, backward ? begin : end);
        break;
    }
    }
    return true;
}

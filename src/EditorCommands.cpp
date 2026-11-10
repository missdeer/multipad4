#include "EditorCommands.h"
#include "EditAdvanced.h"
#include "EditCommandRequirements.h"
#include "EditExtra.h"
#include "resource.h"
namespace {
struct DirectCommand {
    UINT id, message;
    bool modifies;
};
// The same Scintilla operations dispatched by upstream Notepad4.cpp, with an
// explicit document receiver instead of the SDI g_hScintilla singleton.
constexpr DirectCommand direct[] = {
    {IDM_EDIT_UNDO, SCI_UNDO, true},
    {IDM_EDIT_REDO, SCI_REDO, true},
    {IDM_EDIT_CUT, SCI_CUT, true},
    {IDM_EDIT_COPY, SCI_COPY, false},
    {IDM_EDIT_PASTE, SCI_PASTE, true},
    {IDM_EDIT_DELETE, SCI_CLEAR, true},
    {IDM_EDIT_SELECTALL, SCI_SELECTALL, false},
    {IDM_EDIT_MOVELINEUP, SCI_MOVESELECTEDLINESUP, true},
    {IDM_EDIT_MOVELINEDOWN, SCI_MOVESELECTEDLINESDOWN, true},
    {IDM_EDIT_DUPLICATELINE, SCI_LINEDUPLICATE, true},
    {IDM_EDIT_SELECTIONDUPLICATE, SCI_SELECTIONDUPLICATE, true},
    {IDM_EDIT_LINETRANSPOSE, SCI_LINETRANSPOSE, true},
    {IDM_EDIT_CUTLINE, SCI_LINECUT, true},
    {IDM_EDIT_COPYLINE, SCI_LINECOPY, false},
    {IDM_EDIT_DELETELINE, SCI_LINEDELETE, true},
    {IDM_EDIT_DELETELINELEFT, SCI_DELLINELEFT, true},
    {IDM_EDIT_DELETELINERIGHT, SCI_DELLINERIGHT, true},
    {IDM_EDIT_CONVERTUPPERCASE, SCI_UPPERCASE, true},
    {IDM_EDIT_CONVERTLOWERCASE, SCI_LOWERCASE, true},
    {IDM_EDIT_INDENT, SCI_TAB, true},
    {IDM_EDIT_UNINDENT, SCI_BACKTAB, true},
    {IDM_EDIT_SELTODOCSTART, SCI_DOCUMENTSTARTEXTEND, false},
    {IDM_EDIT_SELTODOCEND, SCI_DOCUMENTENDEXTEND, false},
    {CMD_CTRLBACK, SCI_DELWORDLEFT, true},
    {CMD_CTRLDEL, SCI_DELWORDRIGHT, true},
    {IDM_VIEW_ZOOMIN, SCI_ZOOMIN, false},
    {IDM_VIEW_ZOOMOUT, SCI_ZOOMOUT, false},
};
struct ToggleCommand {
    UINT id, get, set;
    WPARAM off, on;
    bool inverse = false;
};
constexpr ToggleCommand toggles[] = {
    {IDM_FILE_READONLY_MODE, SCI_GETREADONLY, SCI_SETREADONLY, 0, 1},
    {IDM_VIEW_WORDWRAP, SCI_GETWRAPMODE, SCI_SETWRAPMODE, SC_WRAP_NONE, SC_WRAP_WORD},
    {IDM_VIEW_SHOWWHITESPACE, SCI_GETVIEWWS, SCI_SETVIEWWS, SCWS_INVISIBLE, SCWS_VISIBLEALWAYS},
    {IDM_VIEW_SHOWEOLS, SCI_GETVIEWEOL, SCI_SETVIEWEOL, 0, 1},
    {IDM_VIEW_SHOWINDENTGUIDES, SCI_GETINDENTATIONGUIDES, SCI_SETINDENTATIONGUIDES, SC_IV_NONE, SC_IV_LOOKBOTH},
    {IDM_VIEW_WORDWRAPSYMBOLS, SCI_GETWRAPVISUALFLAGS, SCI_SETWRAPVISUALFLAGS, 0, SC_WRAPVISUALFLAG_END},
    {IDM_VIEW_TABSASSPACES, SCI_GETUSETABS, SCI_SETUSETABS, 1, 0, true},
    {IDM_SET_MULTIPLE_SELECTION, SCI_GETMULTIPLESELECTION, SCI_SETMULTIPLESELECTION, 0, 1},
};
} // namespace
static CommandState EditorCommandStateCore(const EditorDocument *d, UINT id) {
    const auto extra = ExtraEditState(d, id);
    if (extra.supported)
        return extra;
    const auto advanced = AdvancedEditState(d, id);
    if (advanced.supported)
        return advanced;
    const bool writable = d && !d->Send(SCI_GETREADONLY);
    for (const auto &command : direct)
        if (command.id == id) {
            bool enabled = d && (!command.modifies || writable);
            if (d && id == IDM_EDIT_UNDO)
                enabled = writable && d->Send(SCI_CANUNDO);
            if (d && id == IDM_EDIT_REDO)
                enabled = writable && d->Send(SCI_CANREDO);
            if (d && id == IDM_EDIT_PASTE)
                enabled = writable && d->Send(SCI_CANPASTE);
            return {true, enabled, false};
        }
    for (const auto &command : toggles)
        if (command.id == id) {
            bool checked = d && d->Send(command.get) != 0;
            if (command.inverse)
                checked = d && !checked;
            return {true, d != nullptr, checked};
        }
    switch (id) {
    case IDM_ENCODING_UTF8:
    case IDM_ENCODING_UTF8SIGN:
    case IDM_ENCODING_UNICODE:
    case IDM_ENCODING_UNICODEREV: {
        const auto encoding = id == IDM_ENCODING_UTF8       ? files::Encoding::Utf8
                              : id == IDM_ENCODING_UTF8SIGN ? files::Encoding::Utf8Bom
                              : id == IDM_ENCODING_UNICODE  ? files::Encoding::Utf16LE
                                                            : files::Encoding::Utf16BE;
        return {true, writable, d && d->encoding == encoding};
    }
    case IDM_LINEENDINGS_CRLF:
    case IDM_LINEENDINGS_LF:
    case IDM_LINEENDINGS_CR: {
        const int eol = id == IDM_LINEENDINGS_CRLF ? SC_EOL_CRLF : id == IDM_LINEENDINGS_LF ? SC_EOL_LF : SC_EOL_CR;
        return {true, writable, d && d->Send(SCI_GETEOLMODE) == eol};
    }
    case IDM_VIEW_LINENUMBERS:
        return {true, d != nullptr, d && d->Send(SCI_GETMARGINWIDTHN, 0) != 0};
    case IDM_VIEW_MARGIN:
        return {true, d != nullptr, d && d->Send(SCI_GETMARGINWIDTHN, 1) != 0};
    case IDM_VIEW_LONGLINEMARKER:
        return {true, d != nullptr, d && d->Send(SCI_GETEDGEMODE) != EDGE_NONE};
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE:
        return {true, d != nullptr, d && !(d->Send(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK) & 0xFF000000)};
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_BACK:
        return {true, d != nullptr,
                d && (d->Send(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK) & 0xFF000000) &&
                    !d->Send(SCI_GETCARETLINEFRAME)};
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_FRAME:
        return {true, d != nullptr,
                d && (d->Send(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK) & 0xFF000000) &&
                    d->Send(SCI_GETCARETLINEFRAME)};
    case IDM_EDIT_CLEARDOCUMENT:
        return {true, writable && d->Send(SCI_GETLENGTH) != 0, false};
    case IDM_VIEW_RESETZOOM:
    case IDM_EDIT_COPYALL:
    case IDM_EDIT_SELECTWORD:
    case IDM_EDIT_SELECTLINE:
    case IDM_EDIT_FINDMATCHINGBRACE:
    case IDM_EDIT_SELTOMATCHINGBRACE:
        return {true, d != nullptr, false};
    default:
        return {};
    }
}
CommandState EditorCommandState(const EditorDocument *document, UINT id) {
    return ConstrainUpstreamEditorState(document, id, EditorCommandStateCore(document, id));
}
bool ExecuteEditorCommand(EditorDocument &d, UINT id, const std::function<bool(bool)> &confirmWrap) {
    const auto state = EditorCommandState(&d, id);
    if (!state.supported)
        return false;
    if (!state.enabled)
        return true;
    if (ExecuteExtraEdit(d, id, confirmWrap))
        return true;
    if (ExecuteAdvancedEdit(d, id))
        return true;
    if ((id == IDM_EDIT_CUT || id == IDM_EDIT_COPY) && d.Send(SCI_GETSELECTIONEMPTY) &&
        d.runtime.lineSelectionMode != 0) {
        const auto column = d.Send(SCI_GETCOLUMN, d.Send(SCI_GETCURRENTPOS));
        const auto visualStudio = d.runtime.lineSelectionMode & 1;
        d.Send(id == IDM_EDIT_CUT ? SCI_LINECUT : SCI_LINECOPY, visualStudio);
        if (id == IDM_EDIT_CUT) {
            auto line = d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETCURRENTPOS));
            if (d.runtime.lineSelectionMode == 3 && line > 0)
                --line;
            d.Send(SCI_GOTOPOS, d.Send(SCI_FINDCOLUMN, line, column));
        }
        return true;
    }
    for (const auto &command : direct)
        if (command.id == id) {
            d.Send(command.message,
                   (id == IDM_EDIT_CUTLINE || id == IDM_EDIT_COPYLINE) ? d.runtime.lineSelectionMode & 1 : 0);
            return true;
        }
    for (const auto &command : toggles)
        if (command.id == id) {
            d.Send(command.set, state.checked ? command.off : command.on);
            return true;
        }
    switch (id) {
    case IDM_ENCODING_UTF8:
        d.SetEncoding(files::Encoding::Utf8);
        break;
    case IDM_ENCODING_UTF8SIGN:
        d.SetEncoding(files::Encoding::Utf8Bom);
        break;
    case IDM_ENCODING_UNICODE:
        d.SetEncoding(files::Encoding::Utf16LE);
        break;
    case IDM_ENCODING_UNICODEREV:
        d.SetEncoding(files::Encoding::Utf16BE);
        break;
    case IDM_LINEENDINGS_CRLF:
    case IDM_LINEENDINGS_LF:
    case IDM_LINEENDINGS_CR: {
        int mode = id == IDM_LINEENDINGS_CRLF ? SC_EOL_CRLF : id == IDM_LINEENDINGS_LF ? SC_EOL_LF : SC_EOL_CR;
        d.Send(SCI_BEGINUNDOACTION);
        d.Send(SCI_CONVERTEOLS, mode);
        d.Send(SCI_ENDUNDOACTION);
        d.Send(SCI_SETEOLMODE, mode);
        break;
    }
    case IDM_VIEW_LINENUMBERS:
        d.Send(SCI_SETMARGINWIDTHN, 0, state.checked ? 0 : MulDiv(54, GetDpiForWindow(d.hwnd), 96));
        break;
    case IDM_VIEW_MARGIN:
        d.Send(SCI_SETMARGINWIDTHN, 1, state.checked ? 0 : MulDiv(16, GetDpiForWindow(d.hwnd), 96));
        break;
    case IDM_VIEW_LONGLINEMARKER:
        d.Send(SCI_SETEDGEMODE, state.checked ? EDGE_NONE : d.edgeMode);
        break;
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_NONE:
        d.Send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, 0);
        break;
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_BACK:
    case IDM_VIEW_HIGHLIGHTCURRENTLINE_FRAME:
        d.Send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, 0xFFFDF7F2);
        d.Send(SCI_SETCARETLINEFRAME, id == IDM_VIEW_HIGHLIGHTCURRENTLINE_FRAME ? 1 : 0);
        break;
    case IDM_VIEW_RESETZOOM:
        d.Send(SCI_SETZOOM, 100);
        break;
    case IDM_EDIT_CLEARDOCUMENT:
        d.Send(SCI_CLEARALL);
        break;
    case IDM_EDIT_COPYALL: {
        const auto text = d.Text();
        d.Send(SCI_COPYTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
        break;
    }
    case IDM_EDIT_SELECTWORD: {
        auto pos = d.Send(SCI_GETCURRENTPOS);
        d.Send(SCI_SETSEL, d.Send(SCI_WORDSTARTPOSITION, pos, TRUE), d.Send(SCI_WORDENDPOSITION, pos, TRUE));
        break;
    }
    case IDM_EDIT_SELECTLINE: {
        auto line = d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETCURRENTPOS));
        auto end = line + 1 < d.Send(SCI_GETLINECOUNT) ? d.Send(SCI_POSITIONFROMLINE, line + 1) : d.Send(SCI_GETLENGTH);
        d.Send(SCI_SETSEL, d.Send(SCI_POSITIONFROMLINE, line), end);
        break;
    }
    case IDM_EDIT_FINDMATCHINGBRACE:
    case IDM_EDIT_SELTOMATCHINGBRACE: {
        auto pos = d.Send(SCI_GETCURRENTPOS), match = d.Send(SCI_BRACEMATCH, pos, 0);
        if (match < 0 && pos) {
            --pos;
            match = d.Send(SCI_BRACEMATCH, pos, 0);
        }
        if (match >= 0) {
            if (id == IDM_EDIT_SELTOMATCHINGBRACE)
                d.Send(SCI_SETSEL, pos, match);
            else
                d.Send(SCI_GOTOPOS, match);
            d.Send(SCI_SCROLLCARET);
        }
        break;
    }
    }
    return true;
}

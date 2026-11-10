// Selection-range and conversion semantics adapted from notepad4 Edit.cpp.
#include "EditWhitespace.h"
#include "EditSelection.h"
#include "resource.h"
namespace {
void Replace(EditorDocument &d, LRESULT start, LRESULT end, const std::string &input, const std::string &output) {
    if (input == output)
        return;
    d.Send(SCI_BEGINUNDOACTION);
    d.Send(SCI_SETTARGETRANGE, start, end);
    d.Send(SCI_REPLACETARGET, output.size(), reinterpret_cast<LPARAM>(output.data()));
    d.Send(SCI_ENDUNDOACTION);
}
} // namespace
bool ExecuteWhitespaceEdit(EditorDocument &d, UINT id) {
    if (id != IDM_EDIT_TRIMLINES && id != IDM_EDIT_TRIMLEAD && id != IDM_EDIT_COMPRESSWS &&
        id != IDM_EDIT_CONVERTTABS && id != IDM_EDIT_CONVERTTABS2 && id != IDM_EDIT_CONVERTSPACES &&
        id != IDM_EDIT_CONVERTSPACES2)
        return false;
    if (d.Send(SCI_GETREADONLY))
        return true;
    if (id == IDM_EDIT_TRIMLINES || id == IDM_EDIT_TRIMLEAD) {
        struct Restore {
            EditorDocument &d;
            SearchState saved;
            ~Restore() {
                d.search = std::move(saved);
                d.search.lastMatchStart = d.search.lastMatchEnd = -1;
            }
        } restore{d, d.search};
        bool selection = !d.Send(SCI_GETSELECTIONEMPTY) && !d.Send(SCI_SELECTIONISRECTANGLE);
        d.search = {};
        d.search.regex = true;
        d.search.findText = id == IDM_EDIT_TRIMLINES ? L"[ \\t]+$" : L"^[ \\t]+";
        d.ReplaceAll(selection);
        return true;
    }
    if (RejectRectangularEdit(d))
        return true;
    const auto all = d.Text();
    auto start = d.Send(SCI_GETSELECTIONSTART), end = d.Send(SCI_GETSELECTIONEND);
    if (id == IDM_EDIT_COMPRESSWS) {
        if (start == end) {
            start = 0;
            end = static_cast<LRESULT>(all.size());
        }
        auto input = all.substr(start, end - start);
        auto buffer = input;
        const char following = end < static_cast<LRESULT>(all.size()) ? all[end] : '\n';
        char previous = start ? all[start - 1] : '\n';
        buffer += following;
        std::string output;
        output.reserve(input.size());
        for (size_t i = 0; i < input.size();) {
            const char character = buffer[i++];
            if (character != ' ' && character != '\t') {
                previous = character;
                output += character;
            } else if (previous != '\n' && previous != '\r') {
                while (i < input.size() && (buffer[i] == ' ' || buffer[i] == '\t'))
                    ++i;
                if (buffer[i] != '\n' && buffer[i] != '\r') {
                    previous = ' ';
                    output += ' ';
                }
            }
        }
        Replace(d, start, end, input, output);
        return true;
    }
    if (start == end)
        return true;
    start = d.Send(SCI_POSITIONFROMLINE, d.Send(SCI_LINEFROMPOSITION, start));
    auto input = all.substr(start, end - start);
    auto wide = files::FromUtf8(input);
    const auto length = wide.size();
    wide += L'\0';
    std::wstring output;
    const auto width = static_cast<size_t>(max<LRESULT>(1, d.Send(SCI_GETTABWIDTH)));
    const bool indentOnly = id == IDM_EDIT_CONVERTTABS2 || id == IDM_EDIT_CONVERTSPACES2;
    bool leading = true;
    size_t column = 0;
    if (id == IDM_EDIT_CONVERTTABS || id == IDM_EDIT_CONVERTTABS2) {
        for (size_t index = 0; index < length; ++index) {
            wchar_t c = wide[index];
            if (c == L'\t' && (!indentOnly || leading)) {
                output.append(width - column % width, L' ');
                column = 0;
            } else {
                ++column;
                if (c == L'\r' || c == L'\n') {
                    column = 0;
                    leading = true;
                } else if (c != L' ')
                    leading = false;
                output += c;
            }
        }
    } else {
        std::wstring spaces;
        for (size_t index = 0; index < length; ++index) {
            wchar_t c = wide[index];
            if ((c == L' ' || c == L'\t') && (!indentOnly || leading)) {
                spaces += c;
                if (spaces.size() == width - column % width || c == L'\t') {
                    if (spaces.size() > 1 || wide[index + 1] == L' ' || wide[index + 1] == L'\t')
                        output += L'\t';
                    else
                        output += c;
                    column = 0;
                    spaces.clear();
                }
            } else {
                column += spaces.size() + 1;
                output += spaces;
                spaces.clear();
                if (c == L'\n' || c == L'\r') {
                    column = 0;
                    leading = true;
                } else
                    leading = false;
                output += c;
            }
        }
        output += spaces;
    }
    Replace(d, start, end, input, files::ToUtf8(output));
    return true;
}

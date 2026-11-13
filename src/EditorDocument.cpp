#include "EditorDocument.h"
#include "SimdText.h"
#include "StyleManager.h"
#include <cwctype>
#include <stdexcept>
std::wstring EditorDocument::Title() const {
    return (Modified() ? L"* " : L"") + (path.empty() ? untitled : path.filename().wstring());
}
bool EditorDocument::Create(HWND parent) {
    hwnd = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, nullptr, _Module.GetModuleInstance(), nullptr);
    if (!hwnd) return false;
    Send(SCI_SETCODEPAGE, SC_CP_UTF8);
    Send(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>("Consolas"));
    Send(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    Send(SCI_STYLESETFORE, STYLE_DEFAULT, RGB(32, 36, 42));
    Send(SCI_STYLESETBACK, STYLE_DEFAULT, RGB(255, 255, 255));
    Send(SCI_STYLECLEARALL);
    Send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    Send(SCI_SETMARGINWIDTHN, 0, MulDiv(54, GetDpiForWindow(hwnd), 96));
    Send(SCI_STYLESETFORE, STYLE_LINENUMBER, RGB(130, 138, 148));
    Send(SCI_STYLESETBACK, STYLE_LINENUMBER, RGB(245, 246, 248));
    Send(SCI_SETTABWIDTH, 4); Send(SCI_SETUSETABS, FALSE);
    Send(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    Send(SCI_SETEDGECOLUMN, 80); Send(SCI_SETEDGECOLOUR, RGB(220, 220, 220));
    Send(SCI_MARKERDEFINE, 0, SC_MARK_BOOKMARK);
    Send(SCI_MARKERSETBACKTRANSLUCENT, 0, 0xFFB46400);
    Send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    Send(SCI_SETMARGINMASKN, 1, 1);
    Send(SCI_SETCARETLINEVISIBLEALWAYS, TRUE);
    Send(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, 0xFFFDF7F2);
    Send(SCI_SETSCROLLWIDTHTRACKING, TRUE); Send(SCI_SETSCROLLWIDTH, 1);
    Send(SCI_SETEOLMODE, SC_EOL_CRLF);
    ConfigureLexer(); return true;
}
void EditorDocument::Load(const files::Text& text, int additionalDocumentOptions) {
    // Populate a separate Scintilla document before replacing the live buffer.
    // Scintilla reports allocation failure through SCI_GETSTATUS, not a C++
    // exception; clearing the live buffer first would lose its text/undo state.
    struct WindowOwner { HWND value; ~WindowOwner() { if (value) ::DestroyWindow(value); } } staging{
        CreateWindowExW(0, L"Scintilla", nullptr, WS_CHILD, 0, 0, 1, 1, hwnd, nullptr, _Module.GetModuleInstance(), nullptr)};
    if (!staging.value) throw std::runtime_error("Cannot create a staging editor; the current document was retained.");
    auto stage = [&](UINT message, WPARAM w = 0, LPARAM l = 0) { return ::SendMessageW(staging.value, message, w, l); };
    stage(SCI_SETCODEPAGE, SC_CP_UTF8);
    auto options = Send(SCI_GETDOCUMENTOPTIONS) | additionalDocumentOptions;
    auto stagedDocument = stage(SCI_CREATEDOCUMENT, text.utf8.size(), options);
    if (!stagedDocument) throw std::runtime_error("Cannot allocate the document; the current document was retained.");
    stage(SCI_SETDOCPOINTER, 0, stagedDocument); stage(SCI_RELEASEDOCUMENT, 0, stagedDocument);
    stage(SCI_SETCODEPAGE, SC_CP_UTF8);
    stage(SCI_SETUNDOCOLLECTION, FALSE); stage(SCI_SETSTATUS, SC_STATUS_OK);
    const auto endings = simd::AnalyzeLineEndings(text.utf8, static_cast<int>(Send(SCI_GETEOLMODE)));
    stage(SCI_ALLOCATELINES, endings.totalLineCount);
    stage(SCI_ADDTEXT, text.utf8.size(), reinterpret_cast<LPARAM>(text.utf8.data()));
    auto status = stage(SCI_GETSTATUS);
    if ((status != SC_STATUS_OK && status < SC_STATUS_WARN_START) || stage(SCI_GETLENGTH) != static_cast<LRESULT>(text.utf8.size()))
        throw std::runtime_error("The complete document could not be loaded; the current document was retained.");
    stage(SCI_SETUNDOCOLLECTION, TRUE); stage(SCI_EMPTYUNDOBUFFER); stage(SCI_SETSAVEPOINT);
    const auto oldDocument = Send(SCI_GETDOCPOINTER), readOnly = Send(SCI_GETREADONLY);
    const auto tabWidth = Send(SCI_GETTABWIDTH), indent = Send(SCI_GETINDENT), useTabs = Send(SCI_GETUSETABS);
    const auto oldPosition = Send(SCI_GETCURRENTPOS), oldAnchor = Send(SCI_GETANCHOR);
    const auto changeHistory = Send(SCI_GETCHANGEHISTORY), undoSelection = Send(SCI_GETUNDOSELECTIONHISTORY);
    Send(SCI_ADDREFDOCUMENT, 0, oldDocument);
    struct DocumentReference { const EditorDocument& editor; LRESULT value; ~DocumentReference() { editor.Send(SCI_RELEASEDOCUMENT, 0, value); } } retained{*this, oldDocument};
    Send(SCI_SETDOCPOINTER, 0, stagedDocument);
    try {
        ConfigureLexer();
    } catch (...) {
        Send(SCI_SETDOCPOINTER, 0, oldDocument); Send(SCI_SETSEL, oldAnchor, oldPosition); throw;
    }
    Send(SCI_SETTABWIDTH, tabWidth); Send(SCI_SETINDENT, indent); Send(SCI_SETUSETABS, useTabs); Send(SCI_SETREADONLY, readOnly);
    Send(SCI_SETCHANGEHISTORY, changeHistory); Send(SCI_SETUNDOSELECTIONHISTORY, undoSelection);
    style.autoClosers.clear();
    style.colorStart = style.colorEnd = -1; style.calltipHex.clear(); style.calltipColor = 0;
    encodingDirty = false;
    encoding = text.encoding;
    Send(SCI_SETEOLMODE, endings.iEOLMode);
    Send(SCI_EMPTYUNDOBUFFER); Send(SCI_SETSAVEPOINT); Send(SCI_GOTOPOS, 0);
    style.occurrenceNeedle.clear(); style.occurrenceCursor = 0; style.occurrences = 0;
    style.occurrencePending = false; style.occurrenceRevision = style.revision;
}
void EditorDocument::SetEncoding(files::Encoding value) {
    if (encoding != value) { encoding = value; encodingDirty = true; }
}
std::string EditorDocument::Text() const {
    auto n = static_cast<size_t>(Send(SCI_GETLENGTH));
    std::string text(n + 1, '\0');
    Send(SCI_GETTEXT, n + 1, reinterpret_cast<LPARAM>(text.data()));
    text.resize(n); return text;
}
void EditorDocument::ConfigureLexer() { ConfigureDocumentStyle(*this); }

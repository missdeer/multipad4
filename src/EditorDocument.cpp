#include "EditorDocument.h"
#include "FileAssociations.h"
#include "SimdText.h"
#include "StyleManager.h"
#include <cwctype>
#include <stdexcept>
#include <chrono>
#include <imm.h>
namespace {
LRESULT CALLBACK CompletionWindowProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto& document=*reinterpret_cast<EditorDocument*>(data);
    if (message == WM_LBUTTONDOWN && (w & MK_CONTROL)) {
        const auto position = document.Send(SCI_POSITIONFROMPOINTCLOSE,static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l)));
        const auto target = document.LinkTargetAt(position);
        if (!target.empty()) {
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL)) <= 32)
                MessageBeep(MB_ICONERROR);
            return 0; // Keep Ctrl+click from adding a second caret.
        }
    }
    if(message==WM_KILLFOCUS || message==WM_IME_STARTCOMPOSITION || message==SCI_CANCEL ||
        (message==WM_KEYDOWN && w==VK_ESCAPE)){document.style.completionPending=false;++document.completionEpoch;}
    if(message==WM_IME_STARTCOMPOSITION)document.completionComposing=true;
    if(message==WM_IME_CHAR || (message==WM_IME_COMPOSITION && (l&GCS_RESULTSTR)))document.completionComposing=false;
    if(message==WM_IME_COMPOSITION && (l&GCS_COMPSTR))document.completionComposing=true;
    if(message==WM_IME_ENDCOMPOSITION)document.completionComposing=false;
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,CompletionWindowProc,id);
    return DefSubclassProc(window,message,w,l);
}
using LoadClock = std::chrono::steady_clock;
double LoadElapsed(LoadClock::time_point start) { return std::chrono::duration<double, std::milli>(LoadClock::now() - start).count(); }
}

bool EditorChild::Create(HWND mdi, const wchar_t* title) {
    static constexpr auto name = L"MultiPad4.MdiDocument";
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProcedure;
    type.hInstance = instance;
    type.lpszClassName = name;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    return CreateMDIWindowW(name, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_MAXIMIZE,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, mdi, instance, reinterpret_cast<LPARAM>(this)) != nullptr;
}

void EditorChild::UpdateLayout() {
    RECT area{};
    GetClientRect(m_hWnd, &area);
    if (m_hWndClient) SetWindowPos(m_hWndClient, nullptr, 0, 0, area.right, area.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

void EditorChild::Notify(const NMHDR& notification) {
    if (document && notification.hwndFrom == document->hwnd) {
        const auto& sci = reinterpret_cast<const SCNotification&>(notification);
        if (!SendMessageW(owner, WM_APP + 0x120, 0, reinterpret_cast<LPARAM>(&sci)))
            HandleStyleNotification(*document, sci);
        if (sci.nmhdr.code == SCN_SAVEPOINTLEFT || sci.nmhdr.code == SCN_SAVEPOINTREACHED) document->titleDirty = true;
        if (sci.nmhdr.code == SCN_MODIFIED && (sci.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) &&
            sci.position <= document->Send(SCI_GETLINEENDPOSITION, 0)) ++document->excerptRevision;
    }
    if (notification.code == SCN_SAVEPOINTLEFT || notification.code == SCN_SAVEPOINTREACHED || notification.code == SCN_ZOOM)
        PostMessageW(owner, WM_DOCUMENT_CHANGED, 0, 0);
    else if (notification.code == SCN_UPDATEUI)
        PostMessageW(owner, WM_DOCUMENT_CHANGED, 1, 0);
}

LRESULT CALLBACK EditorChild::WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<const CREATESTRUCTW*>(l);
        auto* mdi = static_cast<const MDICREATESTRUCTW*>(create->lpCreateParams);
        auto* self = reinterpret_cast<EditorChild*>(mdi->lParam);
        self->m_hWnd = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<EditorChild*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self) {
        switch (message) {
        case WM_CLOSE:
            PostMessageW(self->owner, WM_CLOSE_TAB, reinterpret_cast<WPARAM>(window), 0);
            return 0;
        case WM_NOTIFY:
            self->Notify(*reinterpret_cast<const NMHDR*>(l));
            break;
        case WM_SIZE:
            DefMDIChildProcW(window, message, w, l);
            self->UpdateLayout();
            return 0;
        case WM_SETFOCUS:
            if (self->m_hWndClient) SetFocus(self->m_hWndClient);
            break;
        case WM_MDIACTIVATE:
            if (reinterpret_cast<HWND>(l) == window)
                SendMessageW(self->owner, WM_TAB_ACTIVATED, 0, l);
            PostMessageW(self->owner, WM_DOCUMENT_CHANGED, 0, 0);
            break;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        }
    }
    return DefMDIChildProcW(window, message, w, l);
}

void EditorChild::UpdateFileIcon() {
    const int resource = DocumentIconResourceId(document->path);
    const UINT dpi = GetDpiForWindow(owner);
    if (resource == fileIconResource && dpi == fileIconDpi) return;
    const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), 0));
    if (!icon) return;
    SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    if (fileIcon) DestroyIcon(fileIcon);
    fileIcon = icon;
    fileIconResource = resource;
    fileIconDpi = dpi;
    ::DrawMenuBar(owner);
}
std::wstring EditorDocument::Title() const {
    return (Modified() ? L"* " : L"") + (path.empty() ? untitled : path.filename().wstring());
}
std::wstring EditorDocument::LinkTargetAt(LRESULT position) const {
    if (position < 0 || position >= Send(SCI_GETLENGTH) || !Send(SCI_INDICATORVALUEAT,INDICATOR_LINK,position)) return {};
    const auto begin = Send(SCI_INDICATORSTART,INDICATOR_LINK,position);
    const auto end = Send(SCI_INDICATOREND,INDICATOR_LINK,position);
    if (end <= begin || end - begin > 32767) return {};
    auto target = files::FromUtf8(Range(static_cast<size_t>(begin),static_cast<size_t>(end-begin)));
    if (_wcsnicmp(target.c_str(),L"https://",8)==0 || _wcsnicmp(target.c_str(),L"http://",7)==0) return target;
    if (target.find(L'@')!=std::wstring::npos && target.find_first_of(L":/?#&=\\") == std::wstring::npos) return L"mailto:" + target;
    if (target.starts_with(L"www.")) return L"https://" + target;
    return {};
}
bool EditorDocument::Create(HWND parent) {
    hwnd = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;
    if(!SetWindowSubclass(hwnd,CompletionWindowProc,1,reinterpret_cast<DWORD_PTR>(this))){DestroyWindow(hwnd);hwnd=nullptr;return false;}
    directFunction = reinterpret_cast<SciFnDirect>(::SendMessageW(hwnd, SCI_GETDIRECTFUNCTION, 0, 0));
    directPointer = ::SendMessageW(hwnd, SCI_GETDIRECTPOINTER, 0, 0);
    editorThread = GetCurrentThreadId();
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
    auto options = additionalDocumentOptions;
    if (text.utf8.size() >= files::LargeFileThreshold || (options & SC_DOCUMENTOPTION_TEXT_LARGE))
        options |= SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE;
    LoadStream(text.utf8.size(), options, [&](const files::ChunkConsumer& consume) { consume(text.utf8); return text.encoding; });
}
void EditorDocument::LoadFile(const std::filesystem::path& source, const files::ReadOptions& options, std::optional<files::Encoding> forced) {
    const auto size = std::filesystem::file_size(source);
    if (size > static_cast<unsigned long long>(PTRDIFF_MAX)) throw std::runtime_error("File exceeds available address space.");
    LoadStream(static_cast<size_t>(size), SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE,
        [&](const files::ChunkConsumer& consume) { return files::ReadChunks(source, consume, options, forced, &loadMetrics.fileRead); });
}
void EditorDocument::LoadStream(size_t reserve, int additionalDocumentOptions, const std::function<files::Encoding(const files::ChunkConsumer&)>& produce) {
    ++style.revision;
    // Populate a separate Scintilla document before replacing the live buffer.
    // Scintilla reports allocation failure through SCI_GETSTATUS, not a C++
    // exception; clearing the live buffer first would lose its text/undo state.
    struct WindowOwner { HWND value; ~WindowOwner() { if (value) ::DestroyWindow(value); } } staging{
        CreateWindowExW(0, L"Scintilla", nullptr, WS_CHILD, 0, 0, 1, 1, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr)};
    if (!staging.value) throw std::runtime_error("Cannot create a staging editor; the current document was retained.");
    auto stage = [&](UINT message, WPARAM w = 0, LPARAM l = 0) { return ::SendMessageW(staging.value, message, w, l); };
    stage(SCI_SETCODEPAGE, SC_CP_UTF8);
    auto options = additionalDocumentOptions;
    // Leave bounded editing slack: an exact-sized gap buffer would otherwise
    // copy the entire multi-gigabyte allocation on the first typed character.
    if ((options & SC_DOCUMENTOPTION_TEXT_LARGE) && reserve >= files::LargeFileThreshold && reserve <= static_cast<size_t>(PTRDIFF_MAX) - 16 * 1024 * 1024)
        reserve += 16 * 1024 * 1024;
    loadMetrics = {};
    auto tick = LoadClock::now();
    auto stagedDocument = stage(SCI_CREATEDOCUMENT, reserve, options);
    loadMetrics.allocation = LoadElapsed(tick);
    if (!stagedDocument) throw std::runtime_error("Cannot allocate the document; the current document was retained.");
    stage(SCI_SETDOCPOINTER, 0, stagedDocument); stage(SCI_RELEASEDOCUMENT, 0, stagedDocument);
    stage(SCI_SETCODEPAGE, SC_CP_UTF8);
    stage(SCI_SETUNDOCOLLECTION, FALSE); stage(SCI_SETSTATUS, SC_STATUS_OK);
    stage(SCI_SETMODEVENTMASK, SC_MOD_NONE);
    simd::LineEndings endings{};
    size_t loaded = 0;
    bool previousCR = false;
    tick = LoadClock::now();
    const auto loadedEncoding = produce([&](std::string_view chunk) {
        auto scanStart = LoadClock::now();
        auto part = simd::AnalyzeLineEndings(chunk);
        for (int i = 0; i < 3; ++i) endings.linesCount[i] += part.linesCount[i];
        if (previousCR && !chunk.empty() && chunk.front() == '\n') {
            --endings.linesCount[SC_EOL_CR]; --endings.linesCount[SC_EOL_LF]; ++endings.linesCount[SC_EOL_CRLF];
        }
        if (!chunk.empty()) previousCR = chunk.back() == '\r';
        if (!loaded && !chunk.empty()) {
            // Estimate once from the first block; cap speculative line storage.
            const auto estimate = static_cast<size_t>((static_cast<long double>(reserve) / chunk.size()) * (part.totalLineCount - 1)) + 1;
            stage(SCI_ALLOCATELINES, (std::min)(estimate, size_t(16 * 1024 * 1024)));
        }
        loadMetrics.lineScan += LoadElapsed(scanStart);
        auto insertStart = LoadClock::now();
        stage(SCI_APPENDTEXT, chunk.size(), reinterpret_cast<LPARAM>(chunk.data()));
        loadMetrics.insertion += LoadElapsed(insertStart);
        loaded += chunk.size();
        auto status = stage(SCI_GETSTATUS);
        if ((status != SC_STATUS_OK && status < SC_STATUS_WARN_START) || stage(SCI_GETLENGTH) != static_cast<LRESULT>(loaded))
            throw std::runtime_error("The complete document could not be loaded; the current document was retained.");
    });
    loadMetrics.readDecode = LoadElapsed(tick) - loadMetrics.lineScan - loadMetrics.insertion;
    endings.iEOLMode = static_cast<int>(Send(SCI_GETEOLMODE));
    for (int i = 0; i < 3; ++i) if (endings.linesCount[i] > endings.linesCount[endings.iEOLMode]) endings.iEOLMode = i;
    stage(SCI_SETMODEVENTMASK, SC_MODEVENTMASKALL);
    stage(SCI_SETUNDOCOLLECTION, TRUE); stage(SCI_EMPTYUNDOBUFFER); stage(SCI_SETSAVEPOINT);
    const auto oldDocument = Send(SCI_GETDOCPOINTER), readOnly = Send(SCI_GETREADONLY);
    const auto tabWidth = Send(SCI_GETTABWIDTH), indent = Send(SCI_GETINDENT), useTabs = Send(SCI_GETUSETABS);
    const auto oldPosition = Send(SCI_GETCURRENTPOS), oldAnchor = Send(SCI_GETANCHOR);
    const auto changeHistory = Send(SCI_GETCHANGEHISTORY), undoSelection = Send(SCI_GETUNDOSELECTIONHISTORY);
    Send(SCI_ADDREFDOCUMENT, 0, oldDocument);
    struct DocumentReference { const EditorDocument& editor; LRESULT value; ~DocumentReference() { editor.Send(SCI_RELEASEDOCUMENT, 0, value); } } retained{*this, oldDocument};
    Send(SCI_SETDOCPOINTER, 0, stagedDocument);
    tick = LoadClock::now();
    try {
        ConfigureLexer();
        loadMetrics.configure = LoadElapsed(tick);
    } catch (...) {
        Send(SCI_SETDOCPOINTER, 0, oldDocument); Send(SCI_SETSEL, oldAnchor, oldPosition); throw;
    }
    Send(SCI_SETTABWIDTH, tabWidth); Send(SCI_SETINDENT, indent); Send(SCI_SETUSETABS, useTabs); Send(SCI_SETREADONLY, readOnly);
    Send(SCI_SETCHANGEHISTORY, LargeFile() ? 0 : changeHistory); Send(SCI_SETUNDOSELECTIONHISTORY, LargeFile() ? 0 : undoSelection);
    style.autoClosers.clear();
    style.colorStart = style.colorEnd = -1; style.calltipHex.clear(); style.calltipColor = 0;
    encodingDirty = false;
    ++excerptRevision; titleDirty = true;
    encoding = loadedEncoding;
    Send(SCI_SETEOLMODE, endings.iEOLMode);
    Send(SCI_EMPTYUNDOBUFFER); Send(SCI_SETSAVEPOINT); Send(SCI_GOTOPOS, 0);
    style.occurrenceNeedle.clear(); style.occurrenceCursor = 0; style.occurrences = 0;
    style.occurrencePending = false; style.occurrenceRevision = style.revision;
    loadMetrics.endings = endings;
    loadMetrics.revision = style.revision;
    loadMetrics.document = Send(SCI_GETDOCPOINTER);
}
void EditorDocument::SetEncoding(files::Encoding value) {
    if (FastView()) { if (encoding == value) return; throw std::runtime_error("Use the full editor to convert the file encoding."); }
    if (encoding != value) { encoding = value; encodingDirty = true; }
}
std::string EditorDocument::Text() const {
    if (FastView()) throw std::runtime_error("Use the full editor for this whole-document command.");
    auto n = static_cast<size_t>(Send(SCI_GETLENGTH));
    std::string text(n + 1, '\0');
    Send(SCI_GETTEXT, n + 1, reinterpret_cast<LPARAM>(text.data()));
    text.resize(n); return text;
}
void EditorDocument::ConfigureLexer() { ConfigureDocumentStyle(*this); }

std::string EditorDocument::Range(size_t start, size_t length) const {
    const size_t total = static_cast<size_t>(Send(SCI_GETLENGTH));
    start = (std::min)(start, total); length = (std::min)(length, total - start);
    std::string result(length + 1, '\0');
    Sci_TextRangeFull range{{static_cast<Sci_Position>(start), static_cast<Sci_Position>(start + length)}, result.data()};
    Send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));
    result.resize(length); return result;
}
void EditorDocument::SaveStreaming(const std::filesystem::path& destination) const {
    if (FastView()) { fastView->SaveCopy(destination); return; }
    files::WriteAtomicChunks(destination, [&](const files::ChunkConsumer& consume) {
        auto chunkEncoding = encoding;
        consume(files::Encode({{}, chunkEncoding})); // Emit the BOM exactly once, including for empty files.
        if (chunkEncoding == files::Encoding::Utf8Bom) chunkEncoding = files::Encoding::Utf8;
        if (chunkEncoding == files::Encoding::Utf16LE) chunkEncoding = files::Encoding::Utf16LEPlain;
        if (chunkEncoding == files::Encoding::Utf16BE) chunkEncoding = files::Encoding::Utf16BEPlain;
        const auto length = static_cast<size_t>(Send(SCI_GETLENGTH));
        for (size_t offset = 0; offset < length;) {
            auto chunk = Range(offset, 16 * 1024 * 1024 + 4);
            size_t count = chunk.size();
            if (offset + count < length) {
                count -= 4;
                while (count && (static_cast<unsigned char>(chunk[count]) & 0xC0) == 0x80) --count;
                chunk.resize(count);
            }
            files::EncodeChunks({std::move(chunk), chunkEncoding}, consume); offset += count;
        }
    });
}

void EditorDocument::OpenFastFile(const std::filesystem::path& source, const files::ReadOptions& options, std::optional<files::Encoding> forced) {
    auto open = [&](std::optional<files::Encoding> selected) {
        auto view = std::make_unique<FastFileView>(*this, source, options, selected);
        view->Attach();
        fastView = std::move(view);
    };
    try { open(forced); }
    catch (const files::InvalidByteSequence&) {
        if (forced) throw;
        open(files::Latin1);
    }
}

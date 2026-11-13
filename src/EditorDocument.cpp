#include "EditorDocument.h"
#include "SimdText.h"
#include "StyleManager.h"
#include <cwctype>
#include <stdexcept>
#include <chrono>
namespace {
using LoadClock = std::chrono::steady_clock;
double LoadElapsed(LoadClock::time_point start) { return std::chrono::duration<double, std::milli>(LoadClock::now() - start).count(); }
}
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
    // Populate a separate Scintilla document before replacing the live buffer.
    // Scintilla reports allocation failure through SCI_GETSTATUS, not a C++
    // exception; clearing the live buffer first would lose its text/undo state.
    struct WindowOwner { HWND value; ~WindowOwner() { if (value) ::DestroyWindow(value); } } staging{
        CreateWindowExW(0, L"Scintilla", nullptr, WS_CHILD, 0, 0, 1, 1, hwnd, nullptr, _Module.GetModuleInstance(), nullptr)};
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
    encoding = loadedEncoding;
    Send(SCI_SETEOLMODE, endings.iEOLMode);
    Send(SCI_EMPTYUNDOBUFFER); Send(SCI_SETSAVEPOINT); Send(SCI_GOTOPOS, 0);
    style.occurrenceNeedle.clear(); style.occurrenceCursor = 0; style.occurrences = 0;
    style.occurrencePending = false; style.occurrenceRevision = style.revision;
}
void EditorDocument::SetEncoding(files::Encoding value) {
    if (FastView()) throw std::runtime_error("Load the file for editing before changing its encoding.");
    if (encoding != value) { encoding = value; encodingDirty = true; }
}
std::string EditorDocument::Text() const {
    if (FastView()) throw std::runtime_error("Load the file for editing before running a whole-document command.");
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
            consume(files::Encode({std::move(chunk), chunkEncoding})); offset += count;
        }
    });
}

void EditorDocument::OpenFastFile(const std::filesystem::path& source, const files::ReadOptions& options, std::optional<files::Encoding> forced) {
    auto view = std::make_unique<FastFileView>(*this, source, options, forced);
    view->Attach();
    fastView = std::move(view);
}

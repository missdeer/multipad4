#include "FastFileView.h"
#include "EditorDocument.h"
#include "resource.h"
#include "SimdText.h"
#include "EditClipboard.h"
#include <stdexcept>
namespace {
constexpr size_t WindowBytes = 128 * 1024, LookBehind = 32 * 1024;
constexpr int ScrollRange = 1000000;
bool Utf16(files::Encoding e) {
    return e == files::Encoding::Utf16LE || e == files::Encoding::Utf16BE || e == files::Encoding::Utf16LEPlain || e == files::Encoding::Utf16BEPlain;
}
bool Utf8(files::Encoding e) { return e == files::Encoding::Utf8 || e == files::Encoding::Utf8Bom; }
unsigned Unit(std::string_view bytes, size_t i, files::Encoding e) {
    auto a = static_cast<unsigned char>(bytes[i]);
    if (!Utf16(e)) return a;
    auto b = static_cast<unsigned char>(bytes[i + 1]);
    return e == files::Encoding::Utf16LE || e == files::Encoding::Utf16LEPlain ? a | (b << 8) : (a << 8) | b;
}
}
FastFileView::FastFileView(EditorDocument& d, const std::filesystem::path& path, const files::ReadOptions& readOptions, std::optional<files::Encoding> selected) :
    document(d), options(readOptions), forced(selected) {
    auto file = std::make_shared<files::MappedFile>(path, nullptr, true);
    auto sample = file->Read(0, 4096);
    std::string signature;
    if (forced) encoding = *forced;
    else if (sample.starts_with("\xFF\xFE")) encoding = files::Encoding::Utf16LE;
    else if (sample.starts_with("\xFE\xFF")) encoding = files::Encoding::Utf16BE;
    else if (sample.starts_with("\xEF\xBB\xBF")) encoding = files::Encoding::Utf8Bom;
    else {
        if (sample.size() == 4096 && !simd::DetectUtf16(sample)) {
            size_t n = sample.size() - 4;
            while (n && (static_cast<unsigned char>(sample[n]) & 0xC0) == 0x80) --n;
            sample.resize(n);
        }
        encoding = files::DetectSample(sample, options).encoding;
    }
    if (Utf8(encoding) && sample.starts_with("\xEF\xBB\xBF")) signature = "\xEF\xBB\xBF";
    if ((encoding == files::Encoding::Utf16LE || encoding == files::Encoding::Utf16LEPlain) && sample.starts_with("\xFF\xFE")) signature = "\xFF\xFE";
    if ((encoding == files::Encoding::Utf16BE || encoding == files::Encoding::Utf16BEPlain) && sample.starts_with("\xFE\xFF")) signature = "\xFE\xFF";
    const auto page = files::CodePage(encoding);
    if (!Utf16(encoding) && (page == CP_UTF7 || page == 50220 || page == 50221 || page == 50222))
        throw std::runtime_error("This encoding requires the full editor.");
    model = std::make_unique<large::Document>(file, encoding, signature);
    const auto attributes = GetFileAttributesW(path.c_str());
    readOnly = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY);
}
FastFileView::~FastFileView() {
    CancelTask();
    if (::IsWindow(document.hwnd)) RemoveWindowSubclass(document.hwnd, EditorProc, reinterpret_cast<UINT_PTR>(this));
    if (::IsWindow(window)) DestroyWindow(window);
}
std::string FastFileView::Encode(std::string_view text) const {
    auto plain = encoding;
    if (plain == files::Encoding::Utf8Bom) plain = files::Encoding::Utf8;
    if (plain == files::Encoding::Utf16LE) plain = files::Encoding::Utf16LEPlain;
    if (plain == files::Encoding::Utf16BE) plain = files::Encoding::Utf16BEPlain;
    return files::Encode({std::string(text), plain});
}
std::string FastFileView::Decode(std::string_view raw) const { return files::Decode(raw, encoding, false).utf8; }
large::Position FastFileView::NativePrefix(std::string_view text, size_t count) const {
    count = (std::min)(count, text.size());
    return Utf8(encoding) ? count : Encode(text.substr(0, count)).size();
}
large::Selection FastFileView::Selection() const {
    if (extendedSelection) return *extendedSelection;
    return {begin + NativePrefix(viewText, static_cast<size_t>(document.Send(SCI_GETCURRENTPOS))), begin + NativePrefix(viewText, static_cast<size_t>(document.Send(SCI_GETANCHOR)))};
}
void FastFileView::RestoreSelection(large::Selection selection) {
    auto caret = (std::min)(selection.caret, Size()), anchor = (std::min)(selection.anchor, Size());
    extendedSelection.reset();
    if (caret < begin || caret > end || anchor < begin || anchor > end) Seek((std::min)(caret, anchor));
    auto position = [&](large::Position offset) {
        offset = (std::clamp)(offset, begin, end);
        return Utf8(encoding) ? static_cast<size_t>(offset - begin) : Decode(model->Read(begin, static_cast<size_t>(offset - begin))).size();
    };
    document.Send(SCI_SETSEL, position(anchor), position(caret)); document.Send(SCI_SCROLLCARET);
    if (caret < begin || caret > end || anchor < begin || anchor > end) extendedSelection = large::Selection{caret, anchor};
}
void FastFileView::Attach() {
    HWND parent = GetParent(document.hwnd);
    if (document.fastView) parent = GetParent(document.fastView->Window());
    WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = _Module.GetModuleInstance();
    cls.lpszClassName = L"MultiPad4.FastFileView"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    window = CreateWindowExW(0, cls.lpszClassName, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL,
        0, 0, 1, 1, parent, nullptr, cls.hInstance, this);
    if (!window) throw std::runtime_error("Cannot create Fast View.");
    label = CreateWindowExW(0, L"STATIC", L"Large file · Editable on demand", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 1, 1, window, nullptr, cls.hInstance, nullptr);
    button = CreateWindowExW(0, L"BUTTON", L"Full editor...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 1, 1, window, reinterpret_cast<HMENU>(ID_LOAD_FOR_EDITING), cls.hInstance, nullptr);
    for (HWND control : {label, button}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    // Prepare the prefix before reparenting or replacing an existing viewport.
    Seek(0);
    SetParent(document.hwnd, window);
    SetWindowSubclass(document.hwnd, EditorProc, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this));
    ShowWindow(document.hwnd, SW_SHOW); Layout(); UpdateBar();
}
void FastFileView::Seek(unsigned long long offset, bool bottom) {
    if (!model) return;
    extendedSelection.reset();
    offset = (std::min)(offset, Size());
    if (offset == Size()) offset = Size() > WindowBytes / 2 ? Size() - WindowBytes / 2 : 0;
    auto start = offset > LookBehind ? offset - LookBehind : 0;
    const size_t unit = Utf16(encoding) ? 2 : 1;
    if (unit == 2) start &= ~1ULL;
    auto bytes = model->Read(start, WindowBytes + LookBehind + 8);
    size_t first = 0;
    for (size_t i = 0; i + unit <= bytes.size() && start + i < offset; i += unit) {
        const auto c = Unit(bytes, i, encoding);
        if (c == '\n' || (c == '\r' && (i + 2 * unit > bytes.size() || Unit(bytes, i + unit, encoding) != '\n'))) first = i + unit;
    }
    // An oversized line is represented by bounded fragments, never scanned to EOF.
    if (!first && start) first = static_cast<size_t>(offset - start) / unit * unit;
    if (Utf8(encoding)) while (first < bytes.size() && (static_cast<unsigned char>(bytes[first]) & 0xC0) == 0x80) ++first;
    if (unit == 2 && first + 2 <= bytes.size()) {
        const auto c = Unit(bytes, first, encoding); if (c >= 0xDC00 && c <= 0xDFFF) first += 2;
    }
    auto count = (std::min)(WindowBytes, bytes.size() - first);
    count -= count % unit;
    size_t lastBreak = 0;
    for (size_t i = first; i + unit <= first + count; i += unit) {
        auto c = Unit(bytes, i, encoding);
        if (c == '\n' || (c == '\r' && (i + 2 * unit > bytes.size() || Unit(bytes, i + unit, encoding) != '\n'))) lastBreak = i + unit - first;
    }
    if (start + first + count < Size()) {
        if (lastBreak) count = lastBreak;
        else if (Utf8(encoding)) {
            while (count && (static_cast<unsigned char>(bytes[first + count]) & 0xC0) == 0x80) --count;
        } else if (unit == 2 && count) {
            auto c = Unit(bytes, first + count - 2, encoding); if (c >= 0xD800 && c <= 0xDBFF) count -= 2;
        }
    }
    if (!Utf8(encoding) && unit == 1) {
        const auto page = files::CodePage(encoding); CPINFOEXW info{};
        if (!GetCPInfoExW(page, 0, &info)) throw std::runtime_error("Encoding is not installed.");
        if (info.MaxCharSize > 1) {
            bool lineBoundary = start + first == 0 || (first && (bytes[first - 1] == '\r' || bytes[first - 1] == '\n'));
            if (!lineBoundary) throw std::runtime_error("Load for editing to navigate inside a very long multibyte line.");
            for (size_t pos = 0; pos < count;) {
                const auto c = static_cast<BYTE>(bytes[first + pos]); size_t length = 1;
                if (page == 54936 && c >= 0x81 && c <= 0xFE && first + pos + 1 < bytes.size()) {
                    auto next = static_cast<BYTE>(bytes[first + pos + 1]); length = next >= 0x30 && next <= 0x39 ? 4 : 2;
                } else if (IsDBCSLeadByteEx(page, c)) length = 2;
                if (pos + length > count) { count = pos; break; }
                pos += length;
            }
        }
    }
    auto decoded = Decode(std::string_view(bytes).substr(first, count));
    const bool previousLoading = loading; loading = true;
    struct Reset { bool& value; bool previous; ~Reset() { value = previous; } } reset{loading, previousLoading};
    const auto encodingDirty = document.encodingDirty;
    document.Load({decoded, encoding}, SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE);
    document.encodingDirty = encodingDirty;
    SendMessageW(document.hwnd, SCI_SETUNDOCOLLECTION, FALSE, 0);
    SendMessageW(document.hwnd, SCI_SETREADONLY, readOnly, 0);
    SendMessageW(document.hwnd, SCI_SETMARGINWIDTHN, 0, 0);
    SendMessageW(document.hwnd, SCI_SETVSCROLLBAR, FALSE, 0);
    SendMessageW(document.hwnd, SCI_SETWRAPMODE, SC_WRAP_NONE, 0);
    viewText = std::move(decoded); begin = start + first; end = begin + count;
    if (bottom) document.Send(SCI_GOTOPOS, document.Send(SCI_GETLENGTH));
    UpdateBar(); PaintMarks();
    PostMessageW(GetAncestor(document.hwnd, GA_ROOT), WM_DOCUMENT_CHANGED, 0, 0);
}
void FastFileView::UpdateBar() {
    if (!window || !model) return;
    SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS};
    scroll.nMin = 0; scroll.nMax = ScrollRange; scroll.nPage = 1;
    scroll.nPos = Size() ? static_cast<int>(static_cast<long double>(begin) * ScrollRange / Size()) : 0;
    SetScrollInfo(window, SB_VERT, &scroll, TRUE);
}
void FastFileView::Layout() {
    if (!window || !IsWindow(window) || GetParent(document.hwnd) != window) return;
    RECT bounds{}; GetClientRect(window, &bounds);
    int height = Active() ? MulDiv(28, GetDpiForWindow(window), 96) : 0;
    int buttonWidth = MulDiv(150, GetDpiForWindow(window), 96);
    MoveWindow(label, 6, 0, (std::max)(0L, bounds.right - buttonWidth - 12), height, TRUE);
    MoveWindow(button, (std::max)(0L, bounds.right - buttonWidth), 0, buttonWidth, height, TRUE);
    MoveWindow(document.hwnd, 0, height, bounds.right, (std::max)(0L, bounds.bottom - height), TRUE);
}
void FastFileView::ShowSelection(large::Position start, large::Position length) {
    Seek(start);
    if (start > Size() || length > Size() - start) throw std::runtime_error("Selection is outside the document.");
    RestoreSelection({start + length, start});
}
void FastFileView::LoadForEditing() {
    if (!model) return;
    CancelTask(); const auto selection = Selection(); const bool modified = model->Modified();
    const auto snapshot = model->Capture();
    loading = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{loading};
    document.LoadStream(static_cast<size_t>(Size()), SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE,
        [&](const files::ChunkConsumer& consume) {
            for (large::Position offset = 0; offset < large::Document::Size(snapshot);) {
                auto bytes = large::Document::Read(snapshot, offset, 16 * 1024 * 1024 + 8);
                size_t count = bytes.size();
                if (offset + count < large::Document::Size(snapshot)) {
                    count -= 8;
                    if (Utf8(encoding)) while (count && (static_cast<unsigned char>(bytes[count]) & 0xC0) == 0x80) --count;
                    else if (Utf16(encoding)) { count &= ~size_t(1); auto c = Unit(bytes, count - 2, encoding); if (c >= 0xD800 && c <= 0xDBFF) count -= 2; }
                    else {
                        size_t position = 0; const auto page = files::CodePage(encoding);
                        while (position < count) { size_t width = IsDBCSLeadByteEx(page, static_cast<BYTE>(bytes[position])) ? 2 : 1;
                            if (page == 54936 && static_cast<BYTE>(bytes[position]) >= 0x81 && static_cast<BYTE>(bytes[position]) <= 0xFE) { auto next = static_cast<BYTE>(bytes[position + 1]); width = next >= 0x30 && next <= 0x39 ? 4 : 2; }
                            if (position + width > count) { count = position; break; } position += width;
                        }
                    }
                }
                consume(Decode(std::string_view(bytes).substr(0, count))); offset += count;
            }
            return encoding;
        });
    model.reset(); document.encodingDirty = modified;
    SendMessageW(document.hwnd, SCI_SETREADONLY, readOnly, 0);
    document.Send(SCI_SETVSCROLLBAR, TRUE);
    document.Send(SCI_SETMARGINWIDTHN, 0, MulDiv(54, GetDpiForWindow(document.hwnd), 96));
    document.Send(SCI_GOTOPOS, Utf8(encoding) ? selection.caret : 0);
    ShowWindow(label, SW_HIDE); ShowWindow(button, SW_HIDE); ShowScrollBar(window, SB_VERT, FALSE); Layout();
}
void FastFileView::SaveCopy(const std::filesystem::path& path) { Save(path, true); }
void FastFileView::Save(const std::filesystem::path& path, bool copy) {
    if (!model) throw std::runtime_error("The mapped document is no longer active.");
    model->Save(path, copy);
}
void FastFileView::SetReadOnly(bool value) {
    if (value) CancelTask();
    readOnly = value;
    const auto previous = loading; loading = true; SendMessageW(document.hwnd, SCI_SETREADONLY, value, 0); loading = previous;
    if (label) SetWindowTextW(label, value ? L"Large file · Read only" : L"Large file · Editable on demand");
}
void FastFileView::BeginEdit() {
    if (loading || !model || readOnly) return;
    if (!editDepth) CancelTask();
    model->Begin(Selection()); ++editDepth;
}
void FastFileView::EndEdit() {
    if (loading || !model || !editDepth) return;
    const auto selection = Selection(); model->End(selection); --editDepth;
    if (!editDepth && viewText.size() > WindowBytes * 4) { Seek((std::min)(selection.caret, selection.anchor)); RestoreSelection(selection); }
}
void FastFileView::OnModified(const SCNotification& n) {
    if (loading || !model || !(n.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) return;
    try {
        const auto position = static_cast<size_t>(n.position), length = static_cast<size_t>(n.length);
        if (position > viewText.size()) throw std::runtime_error("Invalid viewport edit position.");
        const auto offset = begin + NativePrefix(viewText, position);
        if (n.modificationType & SC_MOD_DELETETEXT) {
            if (extendedSelection) {
                const auto selection = *extendedSelection; extendedSelection.reset();
                const auto first = (std::min)(selection.caret, selection.anchor), last = (std::max)(selection.caret, selection.anchor);
                if (length > viewText.size() - position) throw std::runtime_error("Invalid viewport deletion.");
                model->Replace(first, last - first, {}); viewText.erase(position, length); end = begin + Encode(viewText).size();
                UpdateBar(); return;
            }
            if (length > viewText.size() - position) throw std::runtime_error("Invalid viewport deletion.");
            const auto removed = NativePrefix(std::string_view(viewText).substr(position), length);
            model->Replace(offset, removed, {}); viewText.erase(position, length); end -= removed;
        } else {
            if (!n.text) throw std::runtime_error("Missing viewport insertion bytes.");
            auto raw = Encode(std::string_view(n.text, length));
            model->Replace(offset, 0, raw); viewText.insert(position, n.text, length); end += raw.size();
        }
        UpdateBar(); PostMessageW(GetAncestor(document.hwnd, GA_ROOT), WM_DOCUMENT_CHANGED, 0, 0);
    } catch (...) {
        model->Abort(); editDepth = 0;
        Seek((std::min)(begin, Size())); throw;
    }
}
void FastFileView::Undo() {
    if (!model || readOnly) return; CancelTask();
    if (auto selection = model->Undo()) { Seek((std::min)(selection->caret, selection->anchor)); RestoreSelection(*selection); }
}
void FastFileView::Redo() {
    if (!model || readOnly) return; CancelTask();
    if (auto selection = model->Redo()) { Seek((std::min)(selection->caret, selection->anchor)); RestoreSelection(*selection); }
}
void FastFileView::CancelTask() {
    cancel.store(true, std::memory_order_relaxed);
    worker.Reset();
    { std::lock_guard guard(resultMutex); result = {}; }
    resultStatus = "Search cancelled.";
    if (label && IsWindow(label)) SetWindowTextW(label, readOnly ? L"Large file · Read only" : L"Large file · Editable on demand");
}
void FastFileView::Launch(const std::function<std::optional<large::Document::Match>(const large::Document::Snapshot&, const std::atomic<bool>&)>& action) {
    if (!model) return; CancelTask(); cancel.store(false, std::memory_order_relaxed);
    { std::lock_guard guard(resultMutex); result = {}; }
    auto snapshot = model->Capture();
    if (label) SetWindowTextW(label, L"Working… Press Esc to cancel");
    worker = work::Submit([this, snapshot, action] {
        TaskResult completed; completed.snapshot = snapshot;
        try { completed.match = action(snapshot, cancel); } catch (const std::exception& error) { completed.error = error.what(); }
        completed.completed = true;
        if (!cancel.load(std::memory_order_relaxed)) {
            { std::lock_guard guard(resultMutex); completed.apply = std::move(result.apply); result = std::move(completed); }
            PostMessageW(window, WM_APP + 1, 0, 0);
        }
    });
}
void FastFileView::CompleteTask() {
    if (!model) return;
    TaskResult completed;
    { std::lock_guard guard(resultMutex); if (!result.completed) return; completed = std::move(result); result = {}; }
    worker.Reset();
    if (completed.snapshot.root != model->Capture().root) return;
    if (!completed.error.empty()) { resultStatus = completed.error; SetWindowTextW(label, files::FromUtf8(completed.error).c_str()); return; }
    if (completed.apply) { completed.apply(); return; }
    if (!completed.match) { resultStatus = "No match found."; SetWindowTextW(label, L"No match found"); return; }
    ShowSelection(completed.match->position, completed.match->length);
    SetWindowTextW(label, readOnly ? L"Large file · Read only" : L"Large file · Editable on demand");
}
void FastFileView::StartGoToLine(large::Position line) {
    Launch([line](const auto& snapshot, const auto& stop) -> std::optional<large::Document::Match> { return large::Document::Match{large::Document::PositionFromLine(snapshot, line, &stop), 0}; });
}
void FastFileView::StartExternalMatch(large::Document::Snapshot utf8, large::Position line, large::Position lineStart, large::Position start, large::Position end) {
    Launch([this, utf8, line, lineStart, start, end](const auto& snapshot, const auto& stop) -> std::optional<large::Document::Match> {
        auto encodedSize = [&](large::Position first, large::Position last) {
            large::Position length = 0;
            while (first < last) {
                if (stop.load()) throw std::runtime_error("Search cancelled.");
                auto bytes = large::Document::Read(utf8, first, static_cast<size_t>((std::min)(last - first, large::Position(64 * 1024 + 4))));
                size_t count = bytes.size();
                if (first + count < last) { count -= 4; while (count && (static_cast<unsigned char>(bytes[count]) & 0xC0) == 0x80) --count; bytes.resize(count); }
                length += Encode(bytes).size(); first += count;
            }
            return length;
        };
        auto native = large::Document::PositionFromLine(snapshot, line, &stop) + encodedSize(lineStart, start);
        return large::Document::Match{native, static_cast<size_t>(encodedSize(start, end))};
    });
}
namespace {
bool SameSearch(const large::SearchOptions& a, const large::SearchOptions& b) {
    return a.pattern == b.pattern && a.regex == b.regex && a.matchCase == b.matchCase && a.wholeWord == b.wholeWord && a.wordStart == b.wordStart && a.dotAll == b.dotAll;
}
struct ReplacementResult {
    std::shared_ptr<large::Document> branch;
    large::Selection after;
    size_t count = 0;
};
ReplacementResult ReplaceSnapshot(std::shared_ptr<large::Document> branch, const large::Document::Snapshot& snapshot,
    files::Encoding encoding, const large::SearchOptions& options, std::string_view replacement, bool captures,
    large::Position start, large::Position limit, large::Selection selection, const std::atomic<bool>* cancel) {
    large::Search search(snapshot, encoding, options, cancel);
    branch->Begin(selection); size_t count = 0;
    try {
        auto position = start;
        const bool independent = !options.regex && !options.wholeWord && !options.wordStart;
        int64_t delta = 0;
        // Discover independent literal replacements in parallel, then edit one
        // private branch in global order. Context-sensitive replacements retain
        // their sequential search-after-each-edit semantics.
        std::unique_ptr<large::HitArchive> discovered;
        std::vector<large::SearchHit> page; size_t pageAt = 0, archiveAt = 0;
        if (independent && options.workers != 1 && limit - start >= 64 * 1024 * 1024) {
            discovered = std::make_unique<large::HitArchive>(); search.Enumerate(start, limit, *discovered);
        }
        while (position <= limit) {
            std::optional<large::SearchHit> hit;
            if (discovered) {
                if (pageAt == page.size()) { page = discovered->Page(archiveAt, 4096); archiveAt += page.size(); pageAt = 0; }
                if (!page.empty()) hit = page[pageAt++];
                if (cancel && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Search cancelled.");
            } else hit = search.Next(position, limit);
            if (!hit) break;
            auto text = search.Native(search.Substitute(*hit, replacement, captures));
            auto oldLength = hit->end - hit->start;
            auto target = independent ? static_cast<large::Position>(static_cast<int64_t>(hit->start) + delta) : hit->start;
            auto move = [&](large::Position value) {
                if (value <= target) return value;
                if (value >= target + oldLength) return value - oldLength + text.size();
                return target + text.size();
            };
            selection = {move(selection.caret), move(selection.anchor)};
            branch->Replace(target, oldLength, text); ++count;
            if (independent) { delta += static_cast<int64_t>(text.size()) - static_cast<int64_t>(oldLength); position = hit->end; }
            else {
                limit = limit - oldLength + text.size(); position = hit->start + text.size();
                search.Rebase(branch->Capture());
                if (!oldLength) { if (position >= limit) break; position = search.Advance(position); }
            }
        }
        if (independent) position = static_cast<large::Position>(static_cast<int64_t>(position) + delta);
        branch->End(selection); return {std::move(branch), selection, count};
    } catch (...) { branch->Abort(); throw; }
}
}
void FastFileView::StartFind(const std::wstring& needle, bool backwards, bool matchCase, bool wholeWord, bool wrap) {
    StartSearch({needle, false, matchCase, wholeWord}, backwards, wrap);
}
void FastFileView::StartSearch(large::SearchOptions options, bool backwards, bool wrap, bool selectionOnly) {
    if (options.pattern.empty()) return;
    const auto selection = Selection();
    const auto snapshot = model->Capture();
    auto lower = selectionOnly ? (std::min)(selection.anchor, selection.caret) : 0;
    auto upper = selectionOnly ? (std::max)(selection.anchor, selection.caret) : Size();
    if (selectionOnly) {
        const bool currentHit = lastHit && (std::min)(selection.anchor, selection.caret) == lastHit->start && (std::max)(selection.anchor, selection.caret) == lastHit->end;
        if (searchScope && currentHit && searchScope->snapshot.root == snapshot.root && SameSearch(searchScope->options, options)) { lower = searchScope->first; upper = searchScope->last; }
        else searchScope = Scope{snapshot, lower, upper, options};
    } else searchScope.reset();
    auto start = backwards ? (std::min)(selection.anchor, selection.caret) : (std::max)(selection.anchor, selection.caret);
    const bool skipZero = repeatZero && lastHit && lastHit->start == lastHit->end && lastHit->start == start && matchSnapshot.root == snapshot.root && SameSearch(lastOptions, options);
    auto hit = std::make_shared<std::optional<large::SearchHit>>();
    Launch([this, options, lower, upper, start, backwards, wrap, skipZero, hit](const auto& source, const auto& stop) mutable -> std::optional<large::Document::Match> {
        large::Search search(source, encoding, options, &stop);
        const auto original = start;
        bool exhausted = false;
        if (skipZero) { if (start == (backwards ? lower : upper)) exhausted = true; else start = search.Advance(start, backwards); }
        if (!exhausted) *hit = search.Next(start, backwards ? lower : upper, backwards);
        if (!*hit && wrap && (backwards ? original < upper : original > lower)) *hit = search.Next(backwards ? upper : lower, original, backwards);
        {
            std::lock_guard guard(resultMutex);
            result.apply = [this, source, options, hit, skipZero] {
                lastOptions = options; matchSnapshot = source;
                if (*hit || !skipZero) lastHit = *hit;
                if (*hit) {
                    repeatZero = (*hit)->start == (*hit)->end;
                    ShowSelection((*hit)->start, (*hit)->end - (*hit)->start);
                    resultStatus = "Match found.";
                } else resultStatus = "No further match found.";
                SetWindowTextW(label, files::FromUtf8(resultStatus).c_str());
            };
        }
        return {};
    });
}
bool FastFileView::ReplaceMatch(const large::SearchOptions& options, std::string_view replacement, bool captures) {
    if (readOnly || options.pattern.empty()) return false;
    CancelTask(); auto selection = Selection(); const auto source = model->Capture();
    const auto first = (std::min)(selection.anchor, selection.caret), last = (std::max)(selection.anchor, selection.caret);
    if (first == last && !(lastHit && repeatZero && lastHit->start == first && matchSnapshot.root == source.root && SameSearch(lastOptions, options))) return false;
    large::Search search(source, encoding, options);
    auto hit = search.Next(first, Size());
    if (!hit || hit->start != first || hit->end != last) return false;
    auto text = search.Native(search.Substitute(*hit, replacement, captures));
    const auto position = first + text.size();
    model->Begin(selection);
    try { model->Replace(first, last - first, text); model->End({position, position}); }
    catch (...) { model->Abort(); throw; }
    if (searchScope && searchScope->snapshot.root == source.root && SameSearch(searchScope->options, options)) {
        searchScope->last = searchScope->last - (last - first) + text.size(); searchScope->snapshot = model->Capture();
    }
    Seek(first); RestoreSelection({position, position});
    lastOptions = options; lastHit = large::SearchHit{position, position, {}}; matchSnapshot = model->Capture(); repeatZero = first == last;
    resultStatus = "Replaced."; return true;
}
void FastFileView::ApplyReplacement(const large::Document::Snapshot& expected, const std::shared_ptr<large::Document>& branch,
    large::Selection before, large::Selection after, size_t count) {
    if (count) { model->Commit(expected, branch->Capture(), before, after); Seek((std::min)(before.caret, Size())); RestoreSelection(after); }
    repeatZero = false; lastHit.reset();
    document.search.matches = count; resultStatus = std::to_string(count) + " occurrences replaced.";
    SetWindowTextW(label, files::FromUtf8(resultStatus).c_str());
    PostMessageW(GetAncestor(document.hwnd, GA_ROOT), WM_DOCUMENT_CHANGED, 0, 0);
}
size_t FastFileView::ReplaceEvery(const large::SearchOptions& options, std::string_view replacement, bool captures, bool selectionOnly) {
    if (readOnly || options.pattern.empty()) return 0;
    CancelTask(); const auto snapshot = model->Capture(); const auto selection = Selection();
    auto first = selectionOnly ? (std::min)(selection.anchor, selection.caret) : 0, limit = selectionOnly ? (std::max)(selection.anchor, selection.caret) : Size();
    auto changed = ReplaceSnapshot(model->Branch(snapshot), snapshot, encoding, options, replacement, captures, first, limit, selection, nullptr);
    ApplyReplacement(snapshot, changed.branch, selection, changed.after, changed.count); return changed.count;
}
void FastFileView::StartReplaceEvery(large::SearchOptions options, std::string replacement, bool captures, bool selectionOnly) {
    if (readOnly) { document.search.matches = 0; resultStatus = "Document is read only."; return; }
    if (options.pattern.empty()) return;
    auto selection = Selection(); auto branch = model->Branch(model->Capture());
    auto first = selectionOnly ? (std::min)(selection.anchor, selection.caret) : 0, limit = selectionOnly ? (std::max)(selection.anchor, selection.caret) : Size();
    Launch([this, options, replacement, captures, first, limit, selection, branch](const auto& snapshot, const auto& stop) -> std::optional<large::Document::Match> {
        auto changed = ReplaceSnapshot(branch, snapshot, encoding, options, replacement, captures, first, limit, selection, &stop);
        { std::lock_guard guard(resultMutex); result.apply = [this, snapshot, changed, selection] { ApplyReplacement(snapshot, changed.branch, selection, changed.after, changed.count); }; }
        return {};
    });
}
std::shared_ptr<large::HitArchive> FastFileView::ArchiveMatches(const large::SearchOptions& options, bool selectionOnly) {
    CancelTask(); auto snapshot = model->Capture(); auto selection = Selection();
    auto first = selectionOnly ? (std::min)(selection.anchor, selection.caret) : 0, limit = selectionOnly ? (std::max)(selection.anchor, selection.caret) : Size();
    large::Search search(snapshot, encoding, options); auto matches = std::make_shared<large::HitArchive>();
    if (options.pattern.empty()) return matches;
    search.Enumerate(first, limit, *matches);
    return matches;
}
std::vector<large::SearchHit> FastFileView::Matches(const large::SearchOptions& options, bool selectionOnly) {
    CancelTask(); auto snapshot = model->Capture(); auto selection = Selection();
    auto first = selectionOnly ? (std::min)(selection.anchor, selection.caret) : 0, limit = selectionOnly ? (std::max)(selection.anchor, selection.caret) : Size();
    large::Search search(snapshot, encoding, options); std::vector<large::SearchHit> matches;
    if (options.pattern.empty()) return matches;
    while (first <= limit) {
        auto hit = search.Next(first, limit); if (!hit) break; matches.push_back(*hit); first = hit->end;
        if (first == hit->start) { if (first == limit) break; first = search.Advance(first); }
    }
    return matches;
}
void FastFileView::StartEnumerate(large::SearchOptions options, bool selectionOnly,
    const std::function<void(const large::Document::Snapshot&, const std::shared_ptr<large::HitArchive>&)>& completed, bool retainOffsets, bool indexLines) {
    auto selection = Selection(); auto first = selectionOnly ? (std::min)(selection.anchor, selection.caret) : 0;
    auto limit = selectionOnly ? (std::max)(selection.anchor, selection.caret) : Size();
    auto hits = std::make_shared<large::HitArchive>(retainOffsets);
    Launch([this, options, first, limit, completed, hits, indexLines](const auto& snapshot, const auto& stop) mutable -> std::optional<large::Document::Match> {
        large::Search search(snapshot, encoding, options, &stop);
        search.Enumerate(first, limit, *hits);
        // Result rows require exact line numbers; build their shared index on
        // this cancellable worker, never as a multi-gigabyte UI callback.
        if (indexLines && hits->Count()) large::Document::LineCount(snapshot, &stop);
        { std::lock_guard guard(resultMutex); result.apply = [this, snapshot, completed, hits] {
            document.search.matches = hits->Count(); resultStatus = std::to_string(hits->Count()) + " matches.";
            completed(snapshot, hits); SetWindowTextW(label, files::FromUtf8(resultStatus).c_str());
        }; }
        return {};
    });
}
void FastFileView::SetMarks(large::Document::Snapshot snapshot, std::vector<large::SearchHit> matches, bool bookmarks) {
    auto archive = std::make_shared<large::HitArchive>(); for (const auto& hit : matches) archive->Append(hit);
    SetMarks(std::move(snapshot), archive, bookmarks);
}
void FastFileView::SetMarks(large::Document::Snapshot snapshot, std::shared_ptr<large::HitArchive> matches, bool bookmarks, bool purge) {
    if (purge) marks.clear();
    std::erase_if(marks, [&](const auto& group) { return group.snapshot.root != snapshot.root; });
    if (matches->Count()) marks.push_back({std::move(snapshot), std::move(matches), bookmarks});
    PaintMarks();
}
void FastFileView::PaintMarks() {
    if (!model) return;
    document.Send(SCI_SETINDICATORCURRENT, 12); document.Send(SCI_INDICATORCLEARRANGE, 0, document.Send(SCI_GETLENGTH));
    document.Send(SCI_INDICSETSTYLE, 12, INDIC_ROUNDBOX); document.Send(SCI_INDICSETFORE, 12, RGB(235, 160, 0));
    document.Send(SCI_MARKERDELETEALL, 0);
    auto position = [&](large::Position value) { return Utf8(encoding) ? static_cast<size_t>(value - begin) : Decode(model->Read(begin, static_cast<size_t>(value - begin))).size(); };
    for (const auto& group : marks) {
        if (group.snapshot.root != model->Capture().root) continue;
        const auto& marked = group.hits;
        bool done = false;
        for (size_t firstHit = marked->FirstEndingAtOrAfter(begin); !done && firstHit < marked->Count();) {
            auto page = marked->Page(firstHit, (std::min)(size_t(4096), marked->Count() - firstHit));
            for (const auto& hit : page) {
                if (hit.start > end) { done = true; break; }
                auto first = position((std::max)(begin, hit.start)), last = position((std::min)(end, hit.end));
                document.Send(SCI_INDICATORFILLRANGE, first, last - first);
                if (group.bookmarks) document.Send(SCI_MARKERADD, document.Send(SCI_LINEFROMPOSITION, first), 0);
            }
            firstHit += page.size();
        }
    }
}
LRESULT CALLBACK FastFileView::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<FastFileView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<FastFileView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(window, message, w, l);
    try {
        if (message == WM_APP + 1) { self->CompleteTask(); return 0; }
        if (message == WM_SIZE) { self->Layout(); return 0; }
        if (message == WM_SETFOCUS) { SetFocus(self->document.hwnd); return 0; }
        if (message == WM_NOTIFY) {
            const auto* notification = reinterpret_cast<SCNotification*>(l);
            if (notification && notification->nmhdr.hwndFrom == self->document.hwnd) self->OnModified(*notification);
            return SendMessageW(GetParent(window), message, w, l);
        }
        if (message == WM_COMMAND && LOWORD(w) == ID_LOAD_FOR_EDITING) {
            PostMessageW(GetAncestor(window, GA_ROOT), WM_COMMAND, ID_LOAD_FOR_EDITING, 0); return 0;
        }
        if (message == WM_VSCROLL && self->Active()) {
            auto offset = self->begin;
            switch (LOWORD(w)) {
            case SB_THUMBTRACK: case SB_THUMBPOSITION: {
                SCROLLINFO scroll{sizeof(scroll), SIF_TRACKPOS}; GetScrollInfo(window, SB_VERT, &scroll);
                offset = static_cast<unsigned long long>(static_cast<long double>(scroll.nTrackPos) * self->Size() / ScrollRange); break;
            }
            case SB_TOP: offset = 0; break;
            case SB_BOTTOM: offset = self->Size(); break;
            case SB_LINEUP: case SB_PAGEUP: {
                if (self->document.Send(SCI_GETFIRSTVISIBLELINE) > 0) {
                    auto step = LOWORD(w) == SB_LINEUP ? 1 : self->document.Send(SCI_LINESONSCREEN);
                    self->document.Send(SCI_LINESCROLL, 0, -step); return 0;
                }
                offset = offset > WindowBytes / 2 ? offset - WindowBytes / 2 : 0; break;
            }
            case SB_LINEDOWN: case SB_PAGEDOWN: {
                auto step = LOWORD(w) == SB_LINEDOWN ? 1 : self->document.Send(SCI_LINESONSCREEN);
                if (self->document.Send(SCI_GETFIRSTVISIBLELINE) + self->document.Send(SCI_LINESONSCREEN) < self->document.Send(SCI_GETLINECOUNT) - 1) {
                    self->document.Send(SCI_LINESCROLL, 0, step); return 0;
                }
                offset = (std::min)(self->Size(), self->end); break;
            }
            default: return 0;
            }
            self->Seek(offset, LOWORD(w) == SB_BOTTOM); return 0;
        }
    } catch (const std::exception& error) { MessageBoxW(window, files::FromUtf8(error.what()).c_str(), L"Fast View", MB_OK | MB_ICONERROR); return 0; }
    return DefWindowProcW(window, message, w, l);
}
bool FastFileView::HandleMessage(UINT message, WPARAM w, LPARAM, LRESULT& output) {
    if (loading || !model) return false;
    const bool copyCommand = message == SCI_COPY || message == SCI_CUT || message == WM_COPY || message == WM_CUT ||
        (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (w == 'C' || w == 'X'));
    if (extendedSelection && copyCommand) {
        const auto selection = *extendedSelection;
        const auto first = (std::min)(selection.caret, selection.anchor), last = (std::max)(selection.caret, selection.anchor);
        large::Search reader(model->Capture(), encoding, {L""});
        auto wide = files::FromUtf8(reader.Utf8(first, last - first));
        auto memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (!memory) throw std::bad_alloc();
        auto* pointer = static_cast<wchar_t*>(GlobalLock(memory));
        if (!pointer) { GlobalFree(memory); throw std::runtime_error("Cannot prepare clipboard selection."); }
        memcpy(pointer, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t)); GlobalUnlock(memory);
        if (!OpenEditClipboard(document.hwnd)) { GlobalFree(memory); throw std::runtime_error("Cannot open clipboard."); }
        const auto ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory); CloseClipboard();
        if (!ok) { GlobalFree(memory); throw std::runtime_error("Cannot write clipboard selection."); }
        if (!readOnly && (message == SCI_CUT || message == WM_CUT || (message == WM_KEYDOWN && w == 'X'))) {
            model->Begin(selection); try { model->Replace(first, last - first, {}); model->End({first, first}); } catch (...) { model->Abort(); throw; }
            Seek(first); RestoreSelection({first, first});
        }
        output = 0; return true;
    }
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == SCI_SETSEL ||
        (message == WM_KEYDOWN && (w == VK_LEFT || w == VK_RIGHT || w == VK_UP || w == VK_DOWN || w == VK_HOME || w == VK_END || w == VK_PRIOR || w == VK_NEXT))) extendedSelection.reset();
    if (message == SCI_SETREADONLY) { SetReadOnly(w != 0); output = 0; return true; }
    if (message == SCI_UNDO || message == WM_UNDO) { Undo(); output = 0; return true; }
    if (message == SCI_REDO) { Redo(); output = 0; return true; }
    if (message == SCI_CANUNDO) { output = CanUndo(); return true; }
    if (message == SCI_CANREDO) { output = CanRedo(); return true; }
    if (message == SCI_GETMODIFY) { output = Modified(); return true; }
    if (message == WM_KEYDOWN && w == VK_ESCAPE) { CancelTask(); output = 0; return true; }
    if (message == SCI_SELECTALL || (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && w == 'A')) {
        MessageBoxW(document.hwnd, L"Use the full editor to select the entire file. Selected visible text can be copied directly.", L"Large file", MB_OK | MB_ICONINFORMATION); output = 0; return true;
    }
    if (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (w == 'Z' || w == 'Y') { if (w == 'Y' || (GetKeyState(VK_SHIFT) & 0x8000)) Redo(); else Undo(); output = 0; return true; }
        if (w == VK_HOME || w == VK_END) { Seek(w == VK_HOME ? 0 : Size(), w == VK_END); output = 0; return true; }
    }
    if (message == WM_KEYDOWN && (w == VK_DELETE || w == VK_BACK) && document.Send(SCI_GETSELECTIONEMPTY)) {
        auto position = document.Send(SCI_GETCURRENTPOS);
        if (w == VK_DELETE && position == document.Send(SCI_GETLENGTH) && end < Size()) { auto target = end; Seek(target); RestoreSelection({target, target}); }
        if (w == VK_BACK && position == 0 && begin) { auto target = begin; Seek(target > WindowBytes / 2 ? target - WindowBytes / 2 : 0); RestoreSelection({target, target}); }
    }
    if ((message == WM_KEYDOWN && (w == VK_NEXT || w == VK_DOWN)) || (message == WM_MOUSEWHEEL && GET_WHEEL_DELTA_WPARAM(w) < 0)) {
        auto first = document.Send(SCI_GETFIRSTVISIBLELINE), screen = document.Send(SCI_LINESONSCREEN), lines = document.Send(SCI_GETLINECOUNT);
        bool atEnd = first + screen >= lines - 1;
        if (message == WM_KEYDOWN && w == VK_DOWN) atEnd = document.Send(SCI_LINEFROMPOSITION, document.Send(SCI_GETCURRENTPOS)) >= lines - 1;
        if (atEnd && end < Size()) { Seek(end); output = 0; return true; }
    }
    if ((message == WM_KEYDOWN && (w == VK_PRIOR || w == VK_UP)) || (message == WM_MOUSEWHEEL && GET_WHEEL_DELTA_WPARAM(w) > 0)) {
        bool atStart = !document.Send(SCI_GETFIRSTVISIBLELINE);
        if (message == WM_KEYDOWN && w == VK_UP) atStart = document.Send(SCI_LINEFROMPOSITION, document.Send(SCI_GETCURRENTPOS)) == 0;
        if (atStart && begin) { auto target = begin; Seek(target > WindowBytes / 2 ? target - WindowBytes / 2 : 0); RestoreSelection({target, target}); output = 0; return true; }
    }
    return false;
}
LRESULT CALLBACK FastFileView::EditorProc(HWND editor, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto& self = *reinterpret_cast<FastFileView*>(data);
    if (message == WM_NCDESTROY) { RemoveWindowSubclass(editor, EditorProc, reinterpret_cast<UINT_PTR>(&self)); return DefSubclassProc(editor, message, w, l); }
    if (self.loading || !self.Active()) return DefSubclassProc(editor, message, w, l);
    try {
        LRESULT output = 0; if (self.HandleMessage(message, w, l, output)) return output;
        if (message == SCI_BEGINUNDOACTION) { self.BeginEdit(); return DefSubclassProc(editor, message, w, l); }
        if (message == SCI_ENDUNDOACTION) { auto value = DefSubclassProc(editor, message, w, l); self.EndEdit(); return value; }
        const bool editing = message == WM_CHAR || message == WM_UNICHAR || message == WM_KEYDOWN || message == WM_IME_COMPOSITION || message == WM_IME_CHAR ||
            message == WM_PASTE || message == WM_CUT || message == WM_CLEAR || message == SCI_REPLACESEL || message == SCI_REPLACETARGET || message == SCI_REPLACETARGETRE ||
            message == SCI_APPENDTEXT || message == SCI_ADDTEXT || message == SCI_INSERTTEXT || message == SCI_CLEARALL || message == SCI_CLEAR || message == SCI_CUT ||
            message == SCI_PASTE || message == SCI_NEWLINE || message == SCI_TAB || message == SCI_BACKTAB;
        if (editing) self.BeginEdit();
        const auto value = DefSubclassProc(editor, message, w, l);
        if (editing) self.EndEdit();
        return value;
    } catch (const std::exception& error) {
        if (self.model) { self.model->Abort(); self.editDepth = 0; }
        MessageBoxW(editor, files::FromUtf8(error.what()).c_str(), L"Large file", MB_OK | MB_ICONERROR); return 0;
    }
}

#include "FastFileView.h"
#include "EditorDocument.h"
#include "resource.h"
#include "SimdText.h"
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
    document(d), options(readOptions), forced(selected), file(std::make_unique<files::MappedFile>(path)) {
    auto sample = file->Read(0, 4096);
    if (forced) encoding = *forced;
    else if (sample.starts_with("\xFF\xFE")) encoding = files::Encoding::Utf16LE;
    else if (sample.starts_with("\xFE\xFF")) encoding = files::Encoding::Utf16BE;
    else if (sample.starts_with("\xEF\xBB\xBF")) encoding = files::Encoding::Utf8Bom;
    else {
        // Trim a partial UTF-8 codepoint before the bounded encoding probe.
        if (sample.size() == 4096 && !simd::DetectUtf16(sample)) {
            size_t n = sample.size() - 4;
            while (n && (static_cast<unsigned char>(sample[n]) & 0xC0) == 0x80) --n;
            sample.resize(n);
        }
        encoding = files::DetectSample(sample, options).encoding;
    }
    const auto page = files::CodePage(encoding);
    if (!Utf16(encoding) && (page == CP_UTF7 || page == 50220 || page == 50221 || page == 50222))
        throw std::runtime_error("Fast View does not support stateful encodings. Open a UTF-8 copy instead.");
}
FastFileView::~FastFileView() {
    if (::IsWindow(document.hwnd)) RemoveWindowSubclass(document.hwnd, EditorProc, reinterpret_cast<UINT_PTR>(this));
    if (::IsWindow(window)) DestroyWindow(window);
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
    label = CreateWindowExW(0, L"STATIC", L"Fast View · Read only · Scroll to browse the file", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        0, 0, 1, 1, window, nullptr, cls.hInstance, nullptr);
    button = CreateWindowExW(0, L"BUTTON", L"Load for editing...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 1, 1, window, reinterpret_cast<HMENU>(ID_LOAD_FOR_EDITING), cls.hInstance, nullptr);
    for (HWND control : {label, button}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    // Prepare the prefix before reparenting or replacing an existing viewport.
    Seek(0);
    SetParent(document.hwnd, window);
    SetWindowSubclass(document.hwnd, EditorProc, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this));
    ShowWindow(document.hwnd, SW_SHOW); Layout(); UpdateBar();
}
void FastFileView::Seek(unsigned long long offset, bool bottom) {
    if (!file) return;
    offset = (std::min)(offset, Size());
    if (offset == Size()) offset = Size() > WindowBytes / 2 ? Size() - WindowBytes / 2 : 0;
    auto start = offset > LookBehind ? offset - LookBehind : 0;
    const size_t unit = Utf16(encoding) ? 2 : 1;
    if (unit == 2) start &= ~1ULL;
    auto bytes = file->Read(start, WindowBytes + LookBehind + 8);
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
    auto decoded = files::Decode(bytes.substr(first, count), encoding, start + first == 0);
    // Paging bypasses public read-only protection only for the private viewport.
    document.Load(decoded, SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE);
    SendMessageW(document.hwnd, SCI_SETREADONLY, TRUE, 0);
    SendMessageW(document.hwnd, SCI_SETMARGINWIDTHN, 0, 0);
    SendMessageW(document.hwnd, SCI_SETVSCROLLBAR, FALSE, 0);
    SendMessageW(document.hwnd, SCI_SETWRAPMODE, SC_WRAP_NONE, 0);
    begin = start + first; end = begin + count;
    if (bottom) document.Send(SCI_GOTOPOS, document.Send(SCI_GETLENGTH));
    UpdateBar();
    PostMessageW(GetAncestor(document.hwnd, GA_ROOT), WM_DOCUMENT_CHANGED, 0, 0);
}
void FastFileView::UpdateBar() {
    if (!window || !file) return;
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
void FastFileView::LoadForEditing() {
    if (!file) return;
    const auto position = begin;
    // Keep the mapped backing store and viewport intact if full loading fails.
    document.LoadFile(document.path, options, forced ? forced : std::optional<files::Encoding>(encoding));
    file.reset();
    SendMessageW(document.hwnd, SCI_SETREADONLY, FALSE, 0);
    document.Send(SCI_SETVSCROLLBAR, TRUE);
    document.Send(SCI_SETMARGINWIDTHN, 0, MulDiv(54, GetDpiForWindow(document.hwnd), 96));
    document.Send(SCI_GOTOPOS, Utf8(encoding) ? position : 0);
    ShowWindow(label, SW_HIDE); ShowWindow(button, SW_HIDE); ShowScrollBar(window, SB_VERT, FALSE); Layout();
}
void FastFileView::SaveCopy(const std::filesystem::path& path) {
    if (!file) throw std::runtime_error("Fast View is no longer active.");
    files::WriteAtomicChunks(path, [&](const files::ChunkConsumer& consume) {
        for (unsigned long long offset = 0; offset < Size();) {
            auto bytes = file->Read(offset, 16 * 1024 * 1024); consume(bytes); offset += bytes.size();
        }
    });
}
LRESULT CALLBACK FastFileView::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<FastFileView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<FastFileView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(window, message, w, l);
    try {
        if (message == WM_SIZE) { self->Layout(); return 0; }
        if (message == WM_SETFOCUS) { SetFocus(self->document.hwnd); return 0; }
        if (message == WM_NOTIFY) return SendMessageW(GetParent(window), message, w, l);
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
LRESULT CALLBACK FastFileView::EditorProc(HWND editor, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto& self = *reinterpret_cast<FastFileView*>(data);
    if (!self.Active()) return DefSubclassProc(editor, message, w, l);
    try {
        if (message == SCI_SELECTALL || (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && w == 'A')) {
            MessageBoxW(editor, L"Use Load for editing to select the entire file. Fast View copies selected visible text only.", L"Fast View", MB_OK | MB_ICONINFORMATION); return 0;
        }
        if (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (w == VK_HOME || w == VK_END)) {
            self.Seek(w == VK_HOME ? 0 : self.Size(), w == VK_END); return 0;
        }
        if ((message == WM_KEYDOWN && (w == VK_NEXT || w == VK_DOWN)) || (message == WM_MOUSEWHEEL && GET_WHEEL_DELTA_WPARAM(w) < 0)) {
            auto first = self.document.Send(SCI_GETFIRSTVISIBLELINE), screen = self.document.Send(SCI_LINESONSCREEN), lines = self.document.Send(SCI_GETLINECOUNT);
            bool atEnd = first + screen >= lines - 1;
            if (message == WM_KEYDOWN && w == VK_DOWN) atEnd = self.document.Send(SCI_LINEFROMPOSITION, self.document.Send(SCI_GETCURRENTPOS)) >= lines - 1;
            if (atEnd && self.end < self.Size()) { self.Seek(self.end); return 0; }
        }
        if ((message == WM_KEYDOWN && (w == VK_PRIOR || w == VK_UP)) || (message == WM_MOUSEWHEEL && GET_WHEEL_DELTA_WPARAM(w) > 0)) {
            bool atStart = !self.document.Send(SCI_GETFIRSTVISIBLELINE);
            if (message == WM_KEYDOWN && w == VK_UP) atStart = self.document.Send(SCI_LINEFROMPOSITION, self.document.Send(SCI_GETCURRENTPOS)) == 0;
            if (atStart && self.begin) {
                self.Seek(self.begin > WindowBytes / 2 ? self.begin - WindowBytes / 2 : 0, true); return 0;
            }
        }
        // Read-only is an invariant, including calls made directly by plugins.
        if (message == SCI_SETREADONLY && !w) return 0;
        if (message == WM_NCDESTROY) RemoveWindowSubclass(editor, EditorProc, reinterpret_cast<UINT_PTR>(&self));
    } catch (const std::exception& error) { MessageBoxW(editor, files::FromUtf8(error.what()).c_str(), L"Fast View", MB_OK | MB_ICONERROR); return 0; }
    return DefSubclassProc(editor, message, w, l);
}

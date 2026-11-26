#pragma once
#include "../src/MainFrame.h"
#include "../src/FastFileView.h"
#include "QAArtifacts.h"
#include "MDILayoutTests.h"
#include <chrono>
#include <source_location>
#include <psapi.h>

namespace TabStripTest {
inline void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Tab strip regression at " + std::to_string(where.line()));
}
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    uint32_t* pixels = nullptr;
    int width = 0, height = 0;
    explicit Surface(HWND window) {
        RECT bounds{}; GetClientRect(window, &bounds); width = bounds.right; height = bounds.bottom;
        HDC screen = GetDC(window); dc = CreateCompatibleDC(screen);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        ReleaseDC(window, screen); Check(dc && bitmap && pixels); previous = SelectObject(dc, bitmap);
    }
    ~Surface() { if (previous) SelectObject(dc, previous); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
    std::vector<uint32_t> Image() const {
        std::vector<uint32_t> image(pixels, pixels + size_t(width) * height);
        for (auto& pixel : image) pixel &= 0x00ffffff;
        return image;
    }
    std::vector<uint32_t> Screen(HWND window) {
        HDC source = GetDC(window); Check(source != nullptr);
        Check(BitBlt(dc, 0, 0, width, height, source, 0, 0, SRCCOPY) != FALSE);
        ReleaseDC(window, source); return Image();
    }
    std::vector<uint32_t> Print(HWND window, bool baseline, HRGN clip = nullptr) {
        std::fill(pixels, pixels + size_t(width) * height, 0x00552b77);
        SelectClipRgn(dc, clip);
        HRGN before = CreateRectRgn(0, 0, 0, 0), after = CreateRectRgn(0, 0, 0, 0);
        const int clipped = GetClipRgn(dc, before); Check(clipped >= 0);
        POINT origin{}; GetViewportOrgEx(dc, &origin); const int mapping = GetMapMode(dc);
        tabTesting::baseline = baseline;
        SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
        tabTesting::baseline = false;
        Check(GetClipRgn(dc, after) == clipped && (!clipped || EqualRgn(before, after)));
        POINT restored{}; GetViewportOrgEx(dc, &restored);
        Check(restored.x == origin.x && restored.y == origin.y && mapping == GetMapMode(dc));
        DeleteObject(before); DeleteObject(after); return Image();
    }
};
struct Traffic {
    HWND window; bool watch = false; size_t items = 0;
    explicit Traffic(HWND value) : window(value) { Check(SetWindowSubclass(window, Procedure, 47, reinterpret_cast<DWORD_PTR>(this)) != FALSE); }
    ~Traffic() { if (IsWindow(window)) RemoveWindowSubclass(window, Procedure, 47); }
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR value) {
        auto& traffic = *reinterpret_cast<Traffic*>(value);
        if (traffic.watch && (message == TCM_GETITEMW || message == TCM_GETITEMA)) ++traffic.items;
        return DefSubclassProc(window, message, w, l);
    }
};
inline uint64_t Hash(const std::vector<uint32_t>& image) {
    uint64_t hash = 1469598103934665603ULL;
    for (const auto pixel : image) { hash ^= pixel; hash *= 1099511628211ULL; }
    return hash;
}
inline uint64_t State(MainFrame& frame) {
    uint64_t hash = frame.tabs.GetActivePage();
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i)
        hash = hash * 1099511628211ULL ^ static_cast<EditorDocument*>(frame.tabs.GetPageData(i))->searchIdentity;
    Check(frame.MDIGetActive() == frame.tabs.GetPageHWND(frame.tabs.GetActivePage())); return hash;
}
inline uint64_t DirtyPixels(HWND window) {
    HRGN region = CreateRectRgn(0, 0, 0, 0); Check(region && GetUpdateRgn(window, region, FALSE) != ERROR);
    const DWORD bytes = GetRegionData(region, 0, nullptr);
    std::vector<char> storage(bytes);
    uint64_t pixels = 0;
    if (bytes) {
        auto* data = reinterpret_cast<RGNDATA*>(storage.data()); Check(GetRegionData(region, bytes, data) != 0);
        const auto* rectangles = reinterpret_cast<const RECT*>(data->Buffer);
        for (DWORD i = 0; i < data->rdh.nCount; ++i)
            pixels += uint64_t(rectangles[i].right - rectangles[i].left) * (rectangles[i].bottom - rectangles[i].top);
    }
    DeleteObject(region); return pixels;
}
inline uint64_t ThreadCpu() {
    FILETIME created{}, exited{}, kernel{}, user{}; Check(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user));
    return (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
        (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
}
inline void NativeOrder(MainFrame& frame) {
    auto& tabs = frame.GetMDITabCtrl(); Check(TabCtrl_GetItemCount(HWND(tabs)) == tabs.GetItemCount());
    for (int i = 0; i < tabs.GetItemCount(); ++i) {
        TCITEMW item{}; item.mask = TCIF_PARAM; Check(TabCtrl_GetItem(HWND(tabs), i, &item));
        Check(tabs.ChildAt(i) == reinterpret_cast<HWND>(item.lParam) && tabs.IndexOf(tabs.ChildAt(i)) == i);
        auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(i));
        Check(frame.tabs.Position(document) == i && frame.plugins.Message(NPPM_GETPOSFROMBUFFERID, document->searchIdentity, 0) == i);
    }
    Check(tabs.IndexOf(nullptr) == -1 && !tabs.ChildAt(-1) && !tabs.ChildAt(tabs.GetItemCount()));
}
inline POINT Button(DocumentTabStrip& tabs, int index) {
    RECT item{}; Check(tabs.GetItemRect(index, &item));
    return {item.right - MulDiv(10, tabs.TitleDpi(), 96), (item.top + item.bottom) / 2};
}
inline void Move(DocumentTabStrip& tabs, POINT point) { tabs.SendMessage(WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y)); }
}

inline void RunTabStripTests(MainFrame& frame, const wchar_t* output) {
    using namespace TabStripTest;
    if (!output) {
        fprintf(stderr, "Tab strip: existing MDI/close regressions\n"); fflush(stderr);
        Check(frame.NewDocument() && frame.NewDocument()); frame.ShowWindow(SW_SHOWNORMAL);
        auto& control = frame.GetMDITabCtrl(); Surface surface(control);
        RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        const auto expected = surface.Screen(control);
        tabTesting::failUpdateRegion = true;
        RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        tabTesting::failUpdateRegion = false; Check(surface.Screen(control) == expected);
        ValidateRect(control, nullptr); tabTesting::draws = 0; tabTesting::measure = true;
        control.SendMessage(WM_PAINT); tabTesting::measure = false; Check(tabTesting::draws == 0);
        fprintf(stderr, "Tab strip: region failure fallback/empty region passed, dpi %u\n", control.TitleDpi()); fflush(stderr);
        RunMDILayoutTests(frame);
        while (frame.tabs.GetPageCount()) Check(frame.Close(frame.tabs.GetPageCount() - 1, false));
        const auto directory = std::filesystem::temp_directory_path() / (L"multipad-tab-close-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory); RunTabCloseTests(frame, directory); std::filesystem::remove_all(directory);
        fprintf(stderr, "Tab strip: existing MDI/close regressions passed\n"); fflush(stderr); return;
    }
    const auto directory = std::filesystem::absolute(output); std::filesystem::create_directories(directory);
    qaArtifactDirectory = directory;
    std::ofstream csv(directory / L"hover.csv"); Check(csv.good());
    csv << "tabs,variant,run,callback_ms,input_to_paint_ms,thread_cpu_ms,rect_queries,draws,dirty_pixels,private_bytes,state_hash,image_hash\n";
    auto& tabs = frame.GetMDITabCtrl();
    frame.SetWindowPos(nullptr, 50, 50, 1200, 700, SWP_NOZORDER | SWP_NOACTIVATE);
    tabs.SetFixedTabWidth(100);
    struct Reset { ~Reset() { tabTesting::measure = tabTesting::baseline = false; qaArtifactDirectory.clear(); } } reset;
    for (const int count : {4, 500}) {
        frame.ShowWindow(SW_HIDE); tabs.SetFixedTabWidth(100);
        fprintf(stderr, "Tab strip: creating %d documents at %llu\n", count, GetTickCount64()); fflush(stderr);
        while (frame.tabs.GetPageCount() < count) {
            Check(frame.NewDocument() != nullptr);
            if (frame.tabs.GetPageCount() % 100 == 0) { fprintf(stderr, "Created %d tabs\n", frame.tabs.GetPageCount()); fflush(stderr); }
        }
        fprintf(stderr, "Tab strip: order/pairs %d at %llu\n", count, GetTickCount64()); fflush(stderr);
        frame.ShowWindow(SW_SHOWNORMAL);
        frame.tabs.SetActivePage(0); tabs.SyncSelection(frame.MDIGetActive()); tabs.EnsureVisible(0, false, true); frame.Refresh();
        NativeOrder(frame);
        Traffic traffic(tabs); traffic.watch = true;
        auto* lastDocument = static_cast<EditorDocument*>(frame.tabs.GetPageData(count - 1));
        Check(frame.plugins.Message(NPPM_GETPOSFROMBUFFERID, lastDocument->searchIdentity, 0) == count - 1);
        tabs.SyncSelection(frame.MDIGetActive()); Check(traffic.items == 0);
        auto* lastChild = frame.tabs.Child(count - 1); tabs.SetIcon(lastChild->m_hWnd, lastChild->GetIcon(false));
        Check(traffic.items <= 2); traffic.watch = false;
        const auto original = tabs.ChildAt(0);
        Check(tabs.MoveItem(0, count - 1)); NativeOrder(frame); Check(tabs.IndexOf(original) == count - 1);
        Check(tabs.MoveItem(count - 1, 0)); NativeOrder(frame); Check(!tabs.MoveItem(-1, 0));
        tabs.Remove(-1); NativeOrder(frame);
        frame.tabs.SetActivePage(0); tabs.SyncSelection(frame.MDIGetActive()); tabs.EnsureVisible(0, false, true); frame.Refresh();
        Surface surface(tabs);
        const auto first = Button(tabs, 0), second = Button(tabs, 1);
        std::vector<std::vector<uint32_t>> expected(21, std::vector<uint32_t>(size_t(surface.width) * surface.height));
        std::vector<uint64_t> states(21), referenceDraws(21);
        for (const bool baseline : {true, false}) {
            tabTesting::baseline = baseline; tabs.SendMessage(WM_MOUSELEAVE);
            RedrawWindow(tabs, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            Move(tabs, first); UpdateWindow(tabs);
            for (int run = 0; run < 21; ++run) {
                const auto point = run % 2 ? first : second;
                tabTesting::rectangles = tabTesting::draws = 0; tabTesting::measure = true;
                const auto cpu = ThreadCpu(); const auto begin = std::chrono::steady_clock::now();
                Move(tabs, point); const auto callback = std::chrono::steady_clock::now();
                const auto dirty = DirtyPixels(tabs); UpdateWindow(tabs);
                const auto painted = std::chrono::steady_clock::now(); const auto cpuEnd = ThreadCpu();
                const auto queries = tabTesting::rectangles, draws = tabTesting::draws; tabTesting::measure = false;
                const auto image = surface.Screen(tabs); const auto state = State(frame);
                if (baseline) { expected[run] = image; states[run] = state; referenceDraws[run] = draws; }
                else { Check(image == expected[run] && state == states[run]); if (count == 500) Check(queries < 40 && draws <= referenceDraws[run]); }
                PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
                Check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)));
                csv << count << ',' << (baseline ? "before" : "after") << ',' << run << ','
                    << std::chrono::duration<double, std::milli>(callback - begin).count() << ','
                    << std::chrono::duration<double, std::milli>(painted - begin).count() << ',' << (cpuEnd - cpu) / 10000.0 << ','
                    << queries << ',' << draws << ',' << dirty << ',' << memory.PrivateUsage << ',' << state << ',' << Hash(image) << '\n';
                csv.flush();
            }
        }
        fprintf(stderr, "Tab strip: print/RTL %d at %llu\n", count, GetTickCount64()); fflush(stderr);
        tabTesting::baseline = false;
        CaptureQAWindow(count == 4 ? HWND(frame) : HWND(tabs), count == 4 ? L"tab-hover-few" : L"tab-hover-500-overflow");
        for (const bool rtl : {false, true}) {
            const auto originalStyle = GetWindowLongPtrW(tabs, GWL_EXSTYLE);
            if (rtl) SetWindowLongPtrW(tabs, GWL_EXSTYLE, originalStyle | WS_EX_LAYOUTRTL);
            SetWindowPos(tabs, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            RedrawWindow(tabs, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            Check(surface.Print(tabs, true) == surface.Print(tabs, false));
            const auto clipFirst = Button(tabs, 0), clipSecond = Button(tabs, 1);
            HRGN clip = CreateRectRgn(clipFirst.x - 3, clipFirst.y - 4, clipFirst.x + 3, clipFirst.y + 4);
            HRGN tail = CreateRectRgn(clipSecond.x - 3, clipSecond.y - 4, clipSecond.x + 3, clipSecond.y + 4);
            CombineRgn(clip, clip, tail, RGN_OR); DeleteObject(tail);
            const auto before = surface.Print(tabs, true, clip), after = surface.Print(tabs, false, clip); Check(before == after);
            for (int y = 0; y < surface.height; ++y) for (int x = 0; x < surface.width; ++x)
                if (!PtInRegion(clip, x, y)) Check(after[size_t(y) * surface.width + x] == 0x00552b77);
            DeleteObject(clip); SelectClipRgn(surface.dc, nullptr);
            if (rtl) CaptureQAWindow(count == 4 ? HWND(frame) : HWND(tabs), L"tab-hover-RTL");
            SetWindowLongPtrW(tabs, GWL_EXSTYLE, originalStyle);
            SetWindowPos(tabs, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        fprintf(stderr, "Tab strip: capture %d at %llu\n", count, GetTickCount64()); fflush(stderr);
        tabs.SendMessage(WM_MOUSELEAVE); UpdateWindow(tabs);
        tabs.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(first.x, first.y)); Check(GetCapture() == HWND(tabs));
        ReleaseCapture(); tabs.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(first.x, first.y));
        MSG request{}; Check(!PeekMessageW(&request, frame, WM_CLOSE_TAB, WM_CLOSE_TAB, PM_REMOVE)); NativeOrder(frame);
        if (count == 500) {
            fprintf(stderr, "Tab strip: drag %d at %llu\n", count, GetTickCount64()); fflush(stderr);
            RECT from{}, to{}; Check(tabs.GetItemRect(0, &from) && tabs.GetItemRect(1, &to));
            const HWND dragged = tabs.ChildAt(0);
            const auto left = MAKELPARAM(from.left + 6, (from.top + from.bottom) / 2);
            const auto right = MAKELPARAM(to.left + 6, (to.top + to.bottom) / 2);
            tabs.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, left);
            tabs.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, right); tabs.SendMessage(WM_LBUTTONUP, 0, right);
            Check(tabs.IndexOf(dragged) == 1); NativeOrder(frame); Check(tabs.MoveItem(1, 0));
            fprintf(stderr, "Tab strip: overflow scroll %d at %llu\n", count, GetTickCount64()); fflush(stderr);
            tabs.EnsureVisible(count - 1, false, true);
            Check(surface.Print(tabs, true) == surface.Print(tabs, false));
            tabs.EnsureVisible(0, false, true);
            fprintf(stderr, "Tab strip: mapped position %d at %llu\n", count, GetTickCount64()); fflush(stderr);
            const auto mappedPath = directory / L"mapped-position.txt"; files::WriteAtomic(mappedPath, {"mapped\n"});
            lastDocument->path = mappedPath; lastDocument->OpenFastFile(mappedPath, {}, files::Encoding::Utf8);
            lastChild->m_hWndClient = lastDocument->ViewWindow(); SetParent(lastDocument->ViewWindow(), lastChild->m_hWnd); lastChild->UpdateLayout();
            Check(frame.tabs.Position(lastDocument) == count - 1);
            lastDocument->fastView->LoadForEditing();
            Check(frame.plugins.Message(NPPM_GETPOSFROMBUFFERID, lastDocument->searchIdentity, 0) == count - 1);
        } else {
            fprintf(stderr, "Tab strip: DPI/font %d at %llu\n", count, GetTickCount64()); fflush(stderr);
            RECT frameBounds{}; GetWindowRect(frame, &frameBounds); const UINT dpi = GetDpiForWindow(frame);
            frame.SendMessage(WM_DPICHANGED, MAKELONG(dpi == 144 ? 96 : 144, dpi == 144 ? 96 : 144), reinterpret_cast<LPARAM>(&frameBounds));
            NativeOrder(frame);
            frame.SendMessage(WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&frameBounds));
            tabs.SetTitleFont(L"Consolas", 18); tabs.EnsureVisible(count - 1, false, true);
            Surface changed(tabs); Check(changed.Print(tabs, true) == changed.Print(tabs, false));
            tabs.SetTitleFont({}, 0); tabs.EnsureVisible(0, false, true);
        }
    }
    fprintf(stderr, "Tab strip: closing corpus at %llu\n", GetTickCount64()); fflush(stderr);
    while (frame.tabs.GetPageCount()) Check(frame.Close(frame.tabs.GetPageCount() - 1, false));
    Check(tabs.GetItemCount() == 0 && tabs.IndexOf(nullptr) == -1);
}

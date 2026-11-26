#pragma once
#include "MainFrame.h"
#include "MatepathPaneTests.h"
#include "OpenDocumentsPaneTests.h"
#include "QAArtifacts.h"
#include <source_location>
inline void RunDpiFontTests(MainFrame& frame) {
    auto require = [](bool value, std::source_location at = std::source_location::current()) {
        if (!value) throw std::runtime_error("DPI font regression at line " + std::to_string(at.line()));
    };
    auto fontHeight = [&](HWND control) {
        LOGFONTW value{}; const auto font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
        require(font && GetObjectW(font, sizeof(value), &value) == sizeof(value)); return abs(value.lfHeight);
    };
    auto* d = frame.NewDocument(); require(d != nullptr); d->Load({"DPI scaling: editor text and native controls\n中文文件浏览器\n"});
    auto* peer = frame.NewDocument(); require(peer != nullptr); peer->Load({"Independent document"});
    frame.tabs.SetActivePage(frame.tabs.GetPageCount() - 2); frame.ShowWindow(SW_SHOWNORMAL); frame.UpdateLayout();
    HWND pane = MatepathTestWindow(); require(pane != nullptr);
    HWND documents = OpenDocumentsTestWindow();
    if (!documents) { for (const auto& plugin : frame.plugins.plugins) if (plugin.module == L"OpenDocuments.dll") require(frame.plugins.Execute(plugin.items[0]._cmdID)); documents = OpenDocumentsTestWindow(); }
    require(documents != nullptr);
    const auto zoom = d->Send(SCI_GETZOOM), peerZoom = peer->Send(SCI_GETZOOM); const auto text = d->Text();
    const auto actualDpi = GetDpiForWindow(frame);
    qaArtifactDirectory = std::filesystem::current_path() / L"tmp/dpi-font-artifacts";
    std::filesystem::create_directories(qaArtifactDirectory);
    std::ofstream results(qaArtifactDirectory / L"metrics.csv"); results << "dpi,expected_gui_height,address_height,filter_height,list_height,documents_height,status_height,editor_width\n";
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
        MONITORINFO info{sizeof(info)}; if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<RECT>*>(context)->push_back(info.rcWork); return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    RECT originalFrame{}; GetWindowRect(frame, &originalFrame);
    auto pump = [&] { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } };
    std::ofstream native(qaArtifactDirectory / L"native-monitors.csv"); native << "monitor,dpi,gui_height,editor_width\n";
    LRESULT nativeWidth = 0; UINT nativeDpi = 0;
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& work = monitors[i]; SetWindowPos(frame, nullptr, work.left + 30, work.top + 30, (std::min)(1000L, work.right - work.left - 60), (std::min)(700L, work.bottom - work.top - 60), SWP_NOZORDER | SWP_NOACTIVATE); pump();
        const auto dpi = GetDpiForWindow(frame); DpiGuiFont expected; require(expected.Update(dpi)); LOGFONTW log{}; GetObjectW(expected.Get(), sizeof(log), &log);
        require(fontHeight(GetDlgItem(pane, 114)) == abs(log.lfHeight) && fontHeight(frame.m_hWndStatusBar) == abs(log.lfHeight));
        const auto width = d->Send(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("DPI scaling sample"));
        if (nativeWidth) require(abs(width - MulDiv(static_cast<int>(nativeWidth), dpi, nativeDpi)) <= 8);
        nativeWidth = width; nativeDpi = dpi; native << i << ',' << dpi << ',' << fontHeight(GetDlgItem(pane, 114)) << ',' << width << '\n';
        const auto label = L"native-monitor-" + std::to_wstring(i) + L"-dpi-" + std::to_wstring(dpi); CaptureQAWindow(pane, label.c_str());
    }
    SetWindowPos(frame, nullptr, originalFrame.left, originalFrame.top, originalFrame.right - originalFrame.left, originalFrame.bottom - originalFrame.top, SWP_NOZORDER | SWP_NOACTIVATE); pump();
    LRESULT previousWidth = 0;
    for (UINT dpi : {96u, 144u, 192u, 96u}) {
        DpiGuiFont expected; require(expected.Update(dpi)); LOGFONTW metrics{}; require(GetObjectW(expected.Get(), sizeof(metrics), &metrics) == sizeof(metrics));
        RECT bounds{}; GetWindowRect(pane, &bounds); SendMessageW(pane, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&bounds));
        GetWindowRect(documents, &bounds); SendMessageW(documents, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&bounds));
        frame.UpdateGuiFonts(dpi);
        for (HWND editor : {d->hwnd, peer->hwnd, frame.searchResults.editor}) SendMessageW(editor, WM_DPICHANGED, MAKELONG(dpi, dpi), 0);
        const int address = fontHeight(GetDlgItem(pane, 114)), filter = fontHeight(GetDlgItem(pane, 115)), list = fontHeight(GetDlgItem(pane, 200));
        const int opened = fontHeight(GetDlgItem(documents, 100)), status = fontHeight(frame.m_hWndStatusBar);
        require(address == abs(metrics.lfHeight) && filter == address && list == address && opened == address && status == address);
        const auto width = d->Send(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<LPARAM>("DPI scaling sample"));
        // Synthetic notifications do not change the monitor DPI queried by Scintilla surfaces.
        if (previousWidth) require(width == previousWidth);
        require(width > 0 && GetDpiForWindow(d->hwnd) == actualDpi); previousWidth = width;
        results << dpi << ',' << abs(metrics.lfHeight) << ',' << address << ',' << filter << ',' << list << ',' << opened << ',' << status << ',' << width << '\n';
        require(d->Send(SCI_GETZOOM) == zoom && peer->Send(SCI_GETZOOM) == peerZoom && d->Text() == text);
        if (dpi == 144) { CaptureQAWindow(pane, L"matepath-fonts-144"); }
    }
    // Repeated transitions replace owned fonts instead of leaking GDI objects.
    const auto before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 40; ++i) {
        const UINT dpi = i % 2 ? 144 : 96; RECT bounds{}; GetWindowRect(pane, &bounds);
        SendMessageW(pane, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&bounds)); frame.UpdateGuiFonts(dpi);
    }
    require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before + 4);
    frame.UpdateGuiFonts(actualDpi);
    for (HWND editor : {d->hwnd, peer->hwnd, frame.searchResults.editor}) SendMessageW(editor, WM_DPICHANGED, MAKELONG(actualDpi, actualDpi), 0);
    RECT bounds{}; GetWindowRect(pane, &bounds); SendMessageW(pane, WM_DPICHANGED, MAKELONG(actualDpi, actualDpi), reinterpret_cast<LPARAM>(&bounds));
    GetWindowRect(documents, &bounds); SendMessageW(documents, WM_DPICHANGED, MAKELONG(actualDpi, actualDpi), reinterpret_cast<LPARAM>(&bounds));
    qaArtifactDirectory.clear();
    for (auto* document : {d, peer}) { document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == document) { require(frame.Close(i)); break; }
    }
}

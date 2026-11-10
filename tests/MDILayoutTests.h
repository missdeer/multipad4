#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>

inline void RunMDILayoutTests(MainFrame& frame) {
    std::string diagnostic;
    auto require = [&](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("MDI layout assertion failed at line " + std::to_string(where.line()) + diagnostic);
    };
    require(frame.tabs.GetPageCount() == 2);
    const HWND first = frame.tabs.GetPageHWND(0), second = frame.tabs.GetPageHWND(1);
    const int savedSelection = frame.tabs.GetActivePage();
    const auto rectangle = [](HWND window) { RECT rect{}; ::GetWindowRect(window, &rect); return rect; };
    const auto clientRectangle = [](HWND window) {
        RECT rect{}; ::GetClientRect(window, &rect);
        ::MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&rect), 2); return rect;
    };
    const auto describeRect = [](const RECT& r) {
        return "[" + std::to_string(r.left) + "," + std::to_string(r.top) + "," +
            std::to_string(r.right) + "," + std::to_string(r.bottom) + "]";
    };
    const auto checkTabs = [&] {
        frame.UpdateLayout();
        HWND control = frame.GetMDITabCtrl();
        const RECT tabs = rectangle(control), owner = rectangle(::GetParent(control));
        const RECT client = rectangle(frame.m_hWndMDIClient);
        require((::GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0);
        require(tabs.right > tabs.left && tabs.bottom > tabs.top);
        require(owner.bottom > owner.top && owner.bottom <= client.top);
    };
    const auto checkMaximized = [&](HWND child) {
        frame.MDIMaximize(child); frame.UpdateLayout();
        BOOL maximized = FALSE;
        require(frame.MDIGetActive(&maximized) == child && maximized && ::IsZoomed(child));
        require(frame.tabs.GetPageHWND(frame.tabs.GetActivePage()) == child);
        const RECT mdi = clientRectangle(frame.m_hWndMDIClient), editor = clientRectangle(child);
        require(::EqualRect(&mdi, &editor) != FALSE);
        const auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetActivePage()));
        const RECT scintilla = rectangle(document->hwnd);
        require(::EqualRect(&editor, &scintilla) != FALSE);
        checkTabs();
    };
    for (HWND child : {first, second}) {
        const auto style = ::GetWindowLongPtrW(child, GWL_STYLE);
        require((style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
        checkMaximized(child);
    }
    // New documents must inherit a usable maximized MDI layout as well.
    auto* added = frame.NewDocument(); require(added != nullptr);
    checkMaximized(frame.tabs.GetPageHWND(frame.tabs.GetActivePage()));
    require(frame.Close(frame.tabs.GetActivePage()));
    frame.tabs.SetActivePage(0); frame.MDIRestore(first);
    require(!::IsZoomed(first));
    RECT restored = rectangle(first), content = clientRectangle(first);
    require(restored.right > restored.left && restored.bottom > restored.top);
    require(content.top > restored.top && content.left > restored.left);
    for (UINT command : {ID_WINDOW_CASCADE, ID_WINDOW_TILE_VERT, ID_WINDOW_TILE_HORZ}) {
        frame.SendMessage(WM_COMMAND, command);
        require(!::IsZoomed(first) && !::IsZoomed(second));
        const RECT a = rectangle(first), b = rectangle(second);
        RECT overlap{}; ::IntersectRect(&overlap, &a, &b);
        diagnostic = " command=" + std::to_string(command) + " windowA=" + describeRect(a) +
            " windowB=" + describeRect(b) + " overlap=" + describeRect(overlap) +
            " clientA=" + describeRect(clientRectangle(first)) + " clientB=" + describeRect(clientRectangle(second)) +
            " mdi=" + describeRect(clientRectangle(frame.m_hWndMDIClient)) +
            " styleA=" + std::to_string(::GetWindowLongPtrW(first, GWL_STYLE)) +
            " styleB=" + std::to_string(::GetWindowLongPtrW(second, GWL_STYLE));
        fprintf(stderr, "MDI layout:%s\n", diagnostic.c_str());
        CaptureQAWindow(frame, command == ID_WINDOW_CASCADE ? L"MDI-cascade" :
            command == ID_WINDOW_TILE_VERT ? L"MDI-tile-vertical" : L"MDI-tile-horizontal");
        require(a.right > a.left && a.bottom > a.top && b.right > b.left && b.bottom > b.top);
        require(!::EqualRect(&a, &b));
        if (command != ID_WINDOW_CASCADE) {
            require(!::IntersectRect(&overlap, &a, &b));
            RECT extent{}; ::UnionRect(&extent, &a, &b);
            const RECT mdi = clientRectangle(frame.m_hWndMDIClient);
            // Native MDI divides the split dimension into equal integer sizes.
            // An odd client width/height leaves one pixel at its trailing edge;
            // the other dimension must still cover the complete client.
            const LONG count = frame.tabs.GetPageCount();
            require(extent.left == mdi.left && extent.top == mdi.top);
            if (command == ID_WINDOW_TILE_VERT) {
                const LONG width = (mdi.right - mdi.left) / count;
                require(a.right - a.left == width && b.right - b.left == width);
                require(a.top == mdi.top && b.top == mdi.top && a.bottom == mdi.bottom && b.bottom == mdi.bottom);
                require(mdi.right - extent.right >= 0 && mdi.right - extent.right < count);
            } else {
                const LONG height = (mdi.bottom - mdi.top) / count;
                require(a.bottom - a.top == height && b.bottom - b.top == height);
                require(a.left == mdi.left && b.left == mdi.left && a.right == mdi.right && b.right == mdi.right);
                require(mdi.bottom - extent.bottom >= 0 && mdi.bottom - extent.bottom < count);
            }
            for (int i = 0; i < frame.tabs.GetPageCount(); ++i) {
                const RECT content = clientRectangle(frame.tabs.GetPageHWND(i));
                const auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(i));
                const RECT editor = rectangle(document->hwnd);
                require(::EqualRect(&content, &editor) != FALSE);
            }
        }
        frame.tabs.SetActivePage(1);
        require(frame.MDIGetActive() == second && frame.Active() == frame.tabs.GetPageData(1));
        checkTabs();
    }
    // Move a minimized child away from the icon row; Arrange Icons must route
    // to the MDI client and actually return it to the native arrangement.
    ::ShowWindow(first, SW_MINIMIZE);
    require(::IsIconic(first) && !::IsIconic(second));
    RECT mdiLocal{}; ::GetClientRect(frame.m_hWndMDIClient, &mdiLocal);
    ::SetWindowPos(first, nullptr, mdiLocal.right / 2, mdiLocal.bottom / 2, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT movedIcon = rectangle(first);
    frame.SendMessage(WM_COMMAND, ID_WINDOW_ARRANGE);
    const RECT arrangedIcon = rectangle(first), mdi = clientRectangle(frame.m_hWndMDIClient);
    diagnostic = " command=" + std::to_string(ID_WINDOW_ARRANGE) + " moved=" + describeRect(movedIcon) +
        " arranged=" + describeRect(arrangedIcon) + " mdi=" + describeRect(mdi);
    fprintf(stderr, "MDI layout:%s\n", diagnostic.c_str());
    require(::IsIconic(first) && !::EqualRect(&movedIcon, &arrangedIcon));
    require(arrangedIcon.left >= mdi.left && arrangedIcon.top >= mdi.top &&
        arrangedIcon.right <= mdi.right && arrangedIcon.bottom <= mdi.bottom);
    frame.MDIRestore(first); require(!::IsIconic(first));
    // Resize the actual frame and verify maximized children track its client.
    const RECT saved = rectangle(frame);
    checkMaximized(first);
    frame.SetWindowPos(nullptr, 0, 0, saved.right - saved.left + 73, saved.bottom - saved.top + 41,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    checkMaximized(first);
    frame.SetWindowPos(nullptr, &saved, SWP_NOZORDER | SWP_NOACTIVATE);
    frame.tabs.SetActivePage(savedSelection); frame.MDIMaximize(frame.tabs.GetPageHWND(savedSelection));
}

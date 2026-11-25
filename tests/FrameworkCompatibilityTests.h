#pragma once
#include "../src/PaneWindow.h"
#include "QAArtifacts.h"
#include <windowsx.h>

class FloatingMoveProbe : public PaneWindow<FloatingMoveProbe> {
public:
    static constexpr auto WindowClass = L"MultiPad4.TestDockPane";
    ~FloatingMoveProbe() { if (IsWindow()) DestroyWindow(); }
    LRESULT Dispatch(UINT, WPARAM, LPARAM, BOOL& handled) { handled = FALSE; return 0; }
};

inline void VerifyPaneLayoutMembership(MainFrame& frame, HWND pane, bool docked) {
    auto* layout = static_cast<const DockPlacement*>(GetPropW(pane, dockPlacementProperty));
    const bool child = (::GetWindowLongPtrW(pane, GWL_STYLE) & WS_CHILD) != 0;
    if (!layout || child != docked || (layout->side != PaneSide::Floating) != docked ||
        (docked && ::GetParent(pane) != frame.m_hWnd))
        throw std::runtime_error("Pane HWND has inconsistent dock layout membership");
}

inline void RunDockLayoutTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Dock layout regression at line " + std::to_string(where.line()));
    };
    FloatingMoveProbe pane, extra;
    DockPlacement floating{{180, 160, 520, 620}, PaneSide::Floating, 220, 220, 0, 0, 0, true};
    require(pane.Create(frame, floating.floating, L"Floating drag regression") != nullptr);
    require(pane.Place(floating));
    auto rect = [](HWND window) { RECT area{}; GetWindowRect(window, &area); return area; };
    auto original = rect(pane);
    auto start = [&] {
        POINT grab{original.left + 25, original.top + 14};
        pane.SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(grab.x, grab.y));
        require(GetCapture() == pane);
        return grab;
    };
    auto move = [&](POINT screen) {
        ::ScreenToClient(pane, &screen);
        pane.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(screen.x, screen.y));
    };
    const auto grab = start();
    for (auto delta : {POINT{37, 23}, POINT{83, 51}, POINT{-41, -27}}) {
        move({grab.x + delta.x, grab.y + delta.y});
        auto actual = rect(pane), expected = original;
        OffsetRect(&expected, delta.x, delta.y);
        require(EqualRect(&actual, &expected) != FALSE);
    }
    CaptureQAWindow(pane, L"Dock-drag-before-cancel");
    pane.SendMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    auto restored = rect(pane);
    require(EqualRect(&restored, &original) != FALSE && GetCapture() != pane);
    require(extra.Create(frame, floating.floating, L"Additional docking bar") != nullptr);
    auto placement = floating;
    placement.side = PaneSide::Right;
    placement.bar = 1;
    require(extra.Place(placement));
    for (int round = 0; round < 3; ++round) {
        const auto before = rect(extra);
        start();
        POINT target{4, 180};
        ::ClientToScreen(frame, &target);
        move(target);
        pane.SendMessage(WM_LBUTTONUP, 0, 0);
        VerifyPaneLayoutMembership(frame, pane, true);
        require(pane.Placement().side == PaneSide::Left);
        const auto after = rect(extra);
        require(EqualRect(&before, &after) != FALSE);
        require(pane.Place(floating));
        VerifyPaneLayoutMembership(frame, pane, false);
    }
    const auto right = rect(extra);
    start();
    move({right.left + 1, right.top + 120});
    pane.SendMessage(WM_LBUTTONUP, 0, 0);
    require(pane.Placement().side == PaneSide::Right);
    require(pane.Placement().bar == extra.Placement().bar + 1);
    VerifyPaneLayoutMembership(frame, pane, true);
    auto inner = rect(pane);
    require(inner.right == right.left);
    POINT caption{inner.left + 30, inner.top + 12};
    pane.SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(caption.x, caption.y));
    move({caption.x - 70, caption.y + 60});
    VerifyPaneLayoutMembership(frame, pane, false);
    pane.SendMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    VerifyPaneLayoutMembership(frame, pane, true);
    auto reattached = rect(pane);
    require(EqualRect(&inner, &reattached) != FALSE);
    auto docked = floating;
    docked.side = PaneSide::Right;
    require(pane.Place(docked));
    const auto dockedRect = rect(pane);
    POINT splitter{dockedRect.left + 1, dockedRect.top + 80};
    const auto hit = pane.SendMessage(WM_NCHITTEST, 0, MAKELPARAM(splitter.x, splitter.y));
    pane.SendMessage(WM_NCLBUTTONDOWN, hit, MAKELPARAM(splitter.x, splitter.y));
    move({splitter.x - 36, splitter.y});
    auto resized = rect(pane);
    require(resized.right - resized.left > dockedRect.right - dockedRect.left);
    pane.SendMessage(WM_CANCELMODE, 0, 0);
    resized = rect(pane);
    require(EqualRect(&dockedRect, &resized) != FALSE);

    const UINT dpi = GetDpiForWindow(pane);
    for (auto side : {PaneSide::Left, PaneSide::Right, PaneSide::Top, PaneSide::Bottom}) {
        docked.side = side;
        require(pane.Place(docked));
        const auto bounds = rect(pane);
        const int splitter = MulDiv(5, dpi, 96);
        const LPARAM close = MAKELPARAM(bounds.right - (side == PaneSide::Left ? splitter : 0) - MulDiv(11, dpi, 96),
            bounds.top + (side == PaneSide::Bottom ? splitter : 0) + MulDiv(11, dpi, 96));
        const auto closeHit = pane.SendMessage(WM_NCHITTEST, 0, close);
        pane.SendMessage(WM_NCLBUTTONDOWN, closeHit, close);
        MSG message{};
        require(PeekMessageW(&message, pane, WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE);
    }
}

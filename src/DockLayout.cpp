#include "DockLayout.h"
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {
constexpr DWORD childStyle = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
constexpr DWORD floatStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN;
int Scale(HWND window, int value) { return MulDiv(value, GetDpiForWindow(window), 96); }
int Width(const RECT& rect) { return rect.right - rect.left; }
int Height(const RECT& rect) { return rect.bottom - rect.top; }
RECT ScreenRect(HWND window) {
    RECT rect{};
    GetWindowRect(window, &rect);
    return rect;
}

RECT CaptionRect(HWND pane, PaneSide side, RECT bounds) {
    const int splitter = Scale(pane, 5);
    if (side == PaneSide::Left) bounds.right -= splitter;
    if (side == PaneSide::Right) bounds.left += splitter;
    if (side == PaneSide::Bottom) bounds.top += splitter;
    const LONG bottom = bounds.bottom - (side == PaneSide::Top ? splitter : 0);
    bounds.bottom = std::min<LONG>(bottom, bounds.top + Scale(pane, 23));
    return bounds;
}

RECT CloseButtonRect(HWND pane, RECT caption) {
    InflateRect(&caption, -Scale(pane, 2), -Scale(pane, 2));
    caption.left = std::max<LONG>(caption.left, caption.right - Height(caption));
    return caption;
}

int Thickness(const DockPlacement& placement) { return IsHorizontal(placement.side) ? placement.height : placement.width; }
DockPlacement* GetPlacement(HWND pane) {
    DWORD process = 0;
    GetWindowThreadProcessId(pane, &process);
    return process == GetCurrentProcessId() ? static_cast<DockPlacement*>(GetPropW(pane, dockPlacementProperty)) : nullptr;
}
}

bool IsHorizontal(PaneSide side) { return side == PaneSide::Top || side == PaneSide::Bottom; }

void LayoutDockPanes(HWND frame, RECT& area) {
    struct Pane { HWND window; const DockPlacement* placement; };
    std::array<std::vector<Pane>, 4> sides;
    for (HWND window = GetWindow(frame, GW_CHILD); window; window = GetWindow(window, GW_HWNDNEXT)) {
        auto* placement = GetPlacement(window);
        if (placement && placement->visible && placement->side != PaneSide::Floating)
            sides[static_cast<unsigned>(placement->side)].push_back({window, placement});
    }
    const int minimumEditor = Scale(frame, 80);
    const int minimumPane = Scale(frame, 28);
    for (auto side : {PaneSide::Left, PaneSide::Right, PaneSide::Top, PaneSide::Bottom}) {
        auto& panes = sides[static_cast<unsigned>(side)];
        std::stable_sort(panes.begin(), panes.end(), [](const Pane& a, const Pane& b) {
            const auto& left = *a.placement;
            const auto& right = *b.placement;
            if (left.bar != right.bar) return left.bar < right.bar;
            if (left.position != right.position) return left.position < right.position;
            return left.index < right.index;
        });
        const bool horizontal = IsHorizontal(side);
        const bool trailing = side == PaneSide::Right || side == PaneSide::Bottom;
        for (auto first = panes.begin(); first != panes.end();) {
            const auto bar = first->placement->bar;
            const auto end = std::find_if(first, panes.end(), [bar](const Pane& pane) { return pane.placement->bar != bar; });
            int thickness = minimumPane;
            for (auto entry = first; entry != end; ++entry) thickness = std::max(thickness, Thickness(*entry->placement));
            const auto available = horizontal ? Height(area) : Width(area);
            thickness = std::min(thickness, std::max(0, available - minimumEditor));
            RECT strip = area;
            LONG& stripStart = horizontal ? strip.top : strip.left;
            LONG& stripEnd = horizontal ? strip.bottom : strip.right;
            LONG& areaNear = horizontal ? area.top : area.left;
            LONG& areaFar = horizontal ? area.bottom : area.right;
            if (trailing) areaFar = stripStart = stripEnd - thickness;
            else areaNear = stripEnd = stripStart + thickness;

            LONG& start = horizontal ? strip.left : strip.top;
            LONG& finish = horizontal ? strip.right : strip.bottom;
            const LONG fullStart = start, fullFinish = finish;
            const LONG length = fullFinish - fullStart;
            const LONG minimumSegment = std::min<LONG>(minimumPane, length / static_cast<LONG>(end - first));
            for (auto entry = first; entry != end; ++entry) {
                const auto next = entry + 1;
                if (next == end) finish = fullFinish;
                else {
                    const float position = next->placement->position;
                    const LONG desired = position > 0 ? fullStart + static_cast<LONG>(length * position)
                        : start + (fullFinish - start) / static_cast<LONG>(end - entry);
                    finish = std::clamp(desired, start + minimumSegment,
                        fullFinish - minimumSegment * static_cast<LONG>(end - next));
                }
                SetWindowPos(entry->window, nullptr, strip.left, strip.top, Width(strip), Height(strip),
                    SWP_NOZORDER | SWP_NOACTIVATE);
                start = finish;
            }
            first = end;
        }
    }
}

static PaneSide DockTarget(HWND frame, POINT screen) {
    RECT area{};
    GetClientRect(frame, &area);
    ScreenToClient(frame, &screen);
    const int edge = Scale(frame, 54);
    if (!PtInRect(&area, screen)) return PaneSide::Floating;
    const std::array<int, 4> distances{screen.x, screen.y, area.right - screen.x, area.bottom - screen.y};
    auto closest = std::min_element(distances.begin(), distances.end());
    return *closest <= edge ? static_cast<PaneSide>(closest - distances.begin()) : PaneSide::Floating;
}

void PaneInteraction::Attach(HWND window, HWND owner) {
    pane = window;
    frame = owner;
    SetPropW(pane, dockPlacementProperty, &placement);
}

DockPlacement PaneInteraction::Placement() const {
    auto result = placement;
    if (placement.side == PaneSide::Floating && IsWindow(pane)) result.floating = ScreenRect(pane);
    return result;
}

bool PaneInteraction::Apply(const DockPlacement& requested) {
    if (!IsWindow(pane)) return false;
    const bool wasDocked = (GetWindowLongPtrW(pane, GWL_STYLE) & WS_CHILD) != 0;
    const bool docked = requested.visible && requested.side != PaneSide::Floating;
    const bool changingParent = wasDocked != docked;
    closeHovered = false;
    if (changingParent || !requested.visible) ShowWindow(pane, SW_HIDE);
    placement = requested;
    if (changingParent) {
        if (docked) {
            SetWindowLongPtrW(pane, GWL_STYLE, childStyle);
            SetParent(pane, frame);
        } else {
            SetParent(pane, nullptr);
            SetWindowLongPtrW(pane, GWL_STYLE, floatStyle);
            SetWindowLongPtrW(pane, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(frame));
        }
    }
    if (docked) {
        SetWindowPos(pane, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        const auto& rect = placement.floating;
        SetWindowPos(pane, nullptr, rect.left, rect.top, Width(rect), Height(rect),
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    SendMessageW(frame, WM_DOCK_LAYOUT, 0, 0);
    if (requested.visible) ShowWindow(pane, SW_SHOWNOACTIVATE);
    return true;
}

void PaneInteraction::StartDrag(POINT screen, bool resize) {
    beforeDrag = Placement();
    initialRect = ScreenRect(pane);
    last = grab = screen;
    moving = true;
    resizing = resize;
    resizedBar.clear();
    if (resize) {
        for (HWND window = GetWindow(frame, GW_CHILD); window; window = GetWindow(window, GW_HWNDNEXT)) {
            const auto* other = GetPlacement(window);
            if (other && other->side == placement.side && other->bar == placement.bar)
                resizedBar.push_back({window, other->width, other->height});
        }
    }
    SetCapture(pane);
    SetFocus(pane);
}

void PaneInteraction::MoveDrag(POINT screen) {
    last = screen;
    const int dx = screen.x - grab.x;
    const int dy = screen.y - grab.y;
    if (resizing) {
        const auto side = placement.side;
        const bool horizontal = IsHorizontal(side);
        const int desired = horizontal ? Height(initialRect) + (side == PaneSide::Top ? dy : -dy)
            : Width(initialRect) + (side == PaneSide::Left ? dx : -dx);
        for (const auto& member : resizedBar) {
            if (auto* target = GetPlacement(member.window)) {
                int& thickness = horizontal ? target->height : target->width;
                thickness = std::max(Scale(pane, 36), desired);
            }
        }
        SendMessageW(frame, WM_DOCK_LAYOUT, 0, 0);
        return;
    }
    if (placement.side != PaneSide::Floating) {
        if (abs(dx) < GetSystemMetrics(SM_CXDRAG) && abs(dy) < GetSystemMetrics(SM_CYDRAG)) return;
        auto next = placement;
        next.side = PaneSide::Floating;
        const int left = screen.x - std::min<int>(grab.x - initialRect.left, Width(next.floating) / 2);
        const int top = screen.y - std::min<int>(grab.y - initialRect.top, Scale(pane, 14));
        OffsetRect(&next.floating, left - next.floating.left, top - next.floating.top);
        moving = false;
        Apply(next);
        moving = true;
        if (GetCapture() != pane) SetCapture(pane);
        initialRect = next.floating;
        grab = screen;
        return;
    }
    SetWindowPos(pane, nullptr, initialRect.left + dx, initialRect.top + dy, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PaneInteraction::EndDrag(bool cancel) {
    moving = false;
    if (GetCapture() == pane) ReleaseCapture();
    if (cancel) {
        for (const auto& member : resizedBar) {
            if (auto* target = GetPlacement(member.window)) {
                target->width = member.width;
                target->height = member.height;
            }
        }
        Apply(beforeDrag);
    }
    else if (!resizing && placement.side == PaneSide::Floating) DockAt(last);
}

void PaneInteraction::DockAt(POINT screen) {
    auto next = Placement();
    next.side = DockTarget(frame, screen);
    next.bar = 0;
    next.position = 0;
    next.index = 0;
    for (HWND window = GetWindow(frame, GW_CHILD); window; window = GetWindow(window, GW_HWNDNEXT)) {
        auto* target = GetPlacement(window);
        if (!target || !target->visible || target->side == PaneSide::Floating) continue;
        RECT rect = ScreenRect(window);
        if (!PtInRect(&rect, screen)) continue;
        next.side = target->side;
        next.bar = target->bar;
        const bool horizontal = IsHorizontal(next.side);
        const bool trailing = next.side == PaneSide::Right || next.side == PaneSide::Bottom;
        const int perpendicular = horizontal ? screen.y : screen.x;
        const int distance = trailing ? perpendicular - (horizontal ? rect.top : rect.left)
            : (horizontal ? rect.bottom : rect.right) - perpendicular;
        if (distance < Scale(frame, 5)) {
            ++next.bar;
            for (HWND peer = GetWindow(frame, GW_CHILD); peer; peer = GetWindow(peer, GW_HWNDNEXT)) {
                if (auto* other = GetPlacement(peer); other && other->side == next.side && other->bar >= next.bar)
                    ++other->bar;
            }
        } else {
            for (HWND peer = GetWindow(frame, GW_CHILD); peer; peer = GetWindow(peer, GW_HWNDNEXT)) {
                if (const auto* other = GetPlacement(peer); other && other->visible &&
                    other->side == next.side && other->bar == next.bar) {
                    const auto bounds = ScreenRect(peer);
                    UnionRect(&rect, &rect, &bounds);
                }
            }
            const int offset = horizontal ? screen.x - rect.left : screen.y - rect.top;
            const int length = horizontal ? Width(rect) : Height(rect);
            if (length > 0) next.position = static_cast<float>(offset) / static_cast<float>(length);
            if (horizontal) next.height = target->height;
            else next.width = target->width;
        }
        break;
    }
    if (next.side != PaneSide::Floating) Apply(next);
}

void PaneInteraction::PaintCaption(HDC dc) {
    if (!dc) return;
    const auto saved = SaveDC(dc);
    if (!saved) return;
    const RECT screen = ScreenRect(pane);
    RECT title = CaptionRect(pane, placement.side, {0, 0, Width(screen), Height(screen)});
    FillRect(dc, &title, GetSysColorBrush(COLOR_BTNFACE));
    const RECT close = CloseButtonRect(pane, title);
    if (!IsRectEmpty(&close)) {
        if (closeHovered) FillRect(dc, &close, GetSysColorBrush(COLOR_HIGHLIGHT));
        const int radius = std::min({Scale(pane, 4), (Width(close) - 1) / 2, (Height(close) - 1) / 2});
        const int x = close.left + Width(close) / 2;
        const int y = close.top + Height(close) / 2;
        const POINT down[]{{x - radius, y - radius}, {x + radius, y + radius}};
        const POINT up[]{{x - radius, y + radius}, {x + radius, y - radius}};
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, GetSysColor(closeHovered ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT));
        Polyline(dc, down, 2);
        Polyline(dc, up, 2);
        // GDI excludes each line's final pixel.
        const COLORREF color = GetDCPenColor(dc);
        SetPixelV(dc, down[1].x, down[1].y, color);
        SetPixelV(dc, up[1].x, up[1].y, color);
    }
    title.left += Scale(pane, 6);
    title.right = close.left - Scale(pane, 2);
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(pane)) + 1, L'\0');
    const int length = GetWindowTextW(pane, text.data(), static_cast<int>(text.size()));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    DrawTextW(dc, text.data(), length, &title, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER | DT_NOPREFIX);
    const int edge = Scale(pane, 5);
    RECT splitter{0, 0, Width(screen), Height(screen)};
    switch (placement.side) {
    case PaneSide::Left: splitter.left = splitter.right - edge; break;
    case PaneSide::Top: splitter.top = splitter.bottom - edge; break;
    case PaneSide::Right: splitter.right = edge; break;
    case PaneSide::Bottom: splitter.bottom = edge; break;
    default: break;
    }
    FillRect(dc, &splitter, GetSysColorBrush(COLOR_3DFACE));
    RestoreDC(dc, saved);
}

bool PaneInteraction::Dispatch(UINT message, WPARAM w, LPARAM l, LRESULT& result) {
    const bool docked = placement.side != PaneSide::Floating;
    POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    result = 0;
    switch (message) {
    case WM_NCCALCSIZE:
        if (docked) {
            auto& rect = *reinterpret_cast<RECT*>(l);
            const int edge = Scale(pane, 5);
            rect.top += Scale(pane, 23);
            if (placement.side == PaneSide::Left) rect.right -= edge;
            if (placement.side == PaneSide::Right) rect.left += edge;
            if (placement.side == PaneSide::Top) rect.bottom -= edge;
            if (placement.side == PaneSide::Bottom) rect.top += edge;
            rect.right = std::max(rect.left, rect.right);
            rect.bottom = std::max(rect.top, rect.bottom);
            return true;
        }
        break;
    case WM_NCHITTEST:
        if (docked) {
            const RECT rect = ScreenRect(pane);
            const RECT caption = CaptionRect(pane, placement.side, rect);
            const RECT close = CloseButtonRect(pane, caption);
            const int edge = Scale(pane, 5);
            if ((placement.side == PaneSide::Left && point.x >= rect.right - edge) ||
                (placement.side == PaneSide::Right && point.x < rect.left + edge) ||
                (placement.side == PaneSide::Top && point.y >= rect.bottom - edge) ||
                (placement.side == PaneSide::Bottom && point.y < rect.top + edge)) result = Splitter;
            else if (PtInRect(&caption, point)) result = PtInRect(&close, point) ? Close : HTCAPTION;
            else result = HTCLIENT;
            return true;
        }
        break;
    case WM_NCMOUSEMOVE:
    case WM_NCMOUSELEAVE:
        if (docked) {
            const bool hover = message == WM_NCMOUSEMOVE && w == Close;
            if (hover != closeHovered) {
                closeHovered = hover;
                if (hover) {
                    TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE | TME_NONCLIENT, pane, 0};
                    TrackMouseEvent(&event);
                }
                RedrawWindow(pane, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_NOCHILDREN);
            }
            return true;
        }
        break;
    case WM_NCPAINT:
        if (docked) {
            const HDC dc = GetWindowDC(pane);
            if (dc) {
                PaintCaption(dc);
                ReleaseDC(pane, dc);
            }
            return true;
        }
        break;
    case WM_PRINT:
        if (docked && (l & PRF_NONCLIENT)) {
            result = DefWindowProcW(pane, message, w, l & ~PRF_NONCLIENT);
            PaintCaption(reinterpret_cast<HDC>(w));
            return true;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(l) == Splitter) {
            SetCursor(LoadCursorW(nullptr, IsHorizontal(placement.side) ? IDC_SIZENS : IDC_SIZEWE));
            result = TRUE;
            return true;
        }
        break;
    case WM_NCLBUTTONDOWN:
        if (w == Close) { PostMessageW(pane, WM_CLOSE, 0, 0); return true; }
        if (w == HTCAPTION || w == Splitter) { StartDrag(point, w == Splitter); return true; }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (docked && w == HTCAPTION) {
            if (moving) EndDrag(false);
            auto floating = Placement();
            floating.side = PaneSide::Floating;
            Apply(floating);
            return true;
        }
        break;
    case WM_MOUSEMOVE:
        if (moving) { ClientToScreen(pane, &point); MoveDrag(point); return true; }
        break;
    case WM_LBUTTONUP:
        if (moving) { EndDrag(false); return true; }
        break;
    case WM_KEYDOWN:
        if (moving && w == VK_ESCAPE) { EndDrag(true); return true; }
        break;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (moving) { EndDrag(true); return true; }
        break;
    case WM_NCDESTROY:
        RemovePropW(pane, dockPlacementProperty);
        if (docked && placement.visible) SendMessageW(frame, WM_DOCK_LAYOUT, 0, 0);
        pane = nullptr;
        break;
    }
    return false;
}

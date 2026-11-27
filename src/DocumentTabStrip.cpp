#include "DocumentTabStrip.h"
#include "App.h"
#include <windowsx.h>
#include <algorithm>

namespace {
constexpr auto layoutClass = L"MultiPad4.DocumentLayout";
constexpr int closeId = 1;
constexpr UINT_PTR scrollButtons = 0, closeButton = 1;

std::wstring WindowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    title.resize(GetWindowTextW(window, title.data(), length + 1));
    return title;
}

void PaintTabButton(HWND window, HDC dc, RECT bounds, int direction, bool hot, bool enabled, bool flat = false) {
    const bool pressed = hot && GetCapture() == window && (GetKeyState(VK_LBUTTON) < 0);
    const auto background = enabled && hot ? (pressed ? COLOR_3DSHADOW : COLOR_WINDOW) : COLOR_BTNFACE;
    if (!flat || hot) FillRect(dc, &bounds, GetSysColorBrush(background));
    const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
    const UINT dpi = GetDpiForWindow(window);
    const int radius = MulDiv(flat ? 3 : 4, dpi, 96);
    const auto oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    const auto oldColor = SetDCPenColor(dc, GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
    if (direction) {
        const int halfWidth = MulDiv(2, dpi, 96);
        const POINT chevron[]{{x - direction * halfWidth, y - radius}, {x + direction * halfWidth, y},
            {x - direction * halfWidth, y + radius}};
        Polyline(dc, chevron, 3);
    } else {
        const POINT down[]{{x - radius, y - radius}, {x + radius, y + radius}};
        const POINT up[]{{x - radius, y + radius}, {x + radius, y - radius}};
        Polyline(dc, down, 2);
        Polyline(dc, up, 2);
        // GDI excludes each line's final pixel.
        const COLORREF color = GetDCPenColor(dc);
        SetPixelV(dc, down[1].x, down[1].y, color);
        SetPixelV(dc, up[1].x, up[1].y, color);
        if (!flat && GetFocus() == window && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            InflateRect(&bounds, -1, -1);
            DrawFocusRect(dc, &bounds);
        }
    }
    SetDCPenColor(dc, oldColor);
    SelectObject(dc, oldPen);
}

LRESULT CALLBACK TabButtonProcedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR kind, DWORD_PTR) {
    if (message == WM_PAINT || message == WM_PRINTCLIENT || message == WM_PRINT) {
        PAINTSTRUCT paint{};
        const HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(w);
        RECT bounds{};
        GetClientRect(window, &bounds);
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        const bool enabled = IsWindowEnabled(window) != FALSE;
        const bool hovered = message == WM_PAINT && PtInRect(&bounds, cursor);
        if (kind == scrollButtons) {
            int first = 0, last = 0;
            SendMessageW(window, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
            const int position = static_cast<int>(SendMessageW(window, UDM_GETPOS32, 0, 0));
            const LONG middle = (bounds.left + bounds.right) / 2;
            PaintTabButton(window, dc, {bounds.left, bounds.top, middle, bounds.bottom},
                -1, hovered && cursor.x < middle, enabled && position > first);
            PaintTabButton(window, dc, {middle, bounds.top, bounds.right, bounds.bottom},
                1, hovered && cursor.x >= middle, enabled && position < last);
        } else {
            PaintTabButton(window, dc, bounds, 0, hovered, enabled);
        }
        if (message == WM_PAINT) EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
    }
    const auto result = DefSubclassProc(window, message, w, l);
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_UPDATEUISTATE:
    case WM_ENABLE:
    case BM_SETSTATE:
    case UDM_SETPOS:
    case UDM_SETPOS32:
    case UDM_SETRANGE:
    case UDM_SETRANGE32:
        InvalidateRect(window, nullptr, FALSE);
        break;
    }
    return result;
}
}

bool DocumentTabStrip::Create(HWND owner, HWND client) {
    frame = owner;
    mdi = client;
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProcedure;
    type.hInstance = instance;
    type.lpszClassName = layoutClass;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    layoutWindow = CreateWindowExW(0, layoutClass, nullptr, WS_CHILD, 0, 0, 0, 0,
        frame, nullptr, instance, this);
    bar = CreateWindowExW(0, layoutClass, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, frame, nullptr, instance, this);
    if (!layoutWindow || !bar) return false;
    control = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | TCS_TOOLTIPS,
        0, 0, 0, 0, bar, nullptr, instance, nullptr);
    const auto closeLabel = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE ? L"关闭当前标签" : L"Close current tab";
    close = CreateWindowExW(0, WC_BUTTONW, closeLabel, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, bar, reinterpret_cast<HMENU>(closeId), instance, nullptr);
    if (!control || !close || !SetWindowSubclass(control, TabProcedure, 0, reinterpret_cast<DWORD_PTR>(this)) ||
        !SetWindowSubclass(close, TabButtonProcedure, closeButton, 0))
        return false;
    SetTitleFont({}, 0);
    UpdateImages();
    return true;
}

DocumentTabStrip::~DocumentTabStrip() {
    if (titleFont) DeleteObject(titleFont);
    if (icons) ImageList_Destroy(icons);
}

int DocumentTabStrip::GetItemCount() const { return static_cast<int>(order.size()); }
int DocumentTabStrip::GetCurSel() const { return control ? TabCtrl_GetCurSel(control) : -1; }
HWND DocumentTabStrip::ChildAt(int index) const { return index >= 0 && index < GetItemCount() ? order[index] : nullptr; }
int DocumentTabStrip::IndexOf(HWND child) const {
    const auto found = positions.find(child); return found == positions.end() ? -1 : found->second;
}

bool DocumentTabStrip::Insert(HWND child, const wchar_t* title) {
    auto label = NativeTitle(title);
    TCITEMW item{};
    item.mask = TCIF_TEXT | TCIF_PARAM | TCIF_IMAGE;
    item.pszText = label.data();
    item.lParam = reinterpret_cast<LPARAM>(child);
    item.iImage = -1;
    if (order.size() == order.capacity()) order.reserve((std::max)(size_t(8), order.size() * 2));
    auto [position, added] = positions.emplace(child, GetItemCount());
    if (!added) return false;
    const int inserted = TabCtrl_InsertItem(control, GetItemCount(), &item);
    if (inserted < 0) { positions.erase(position); return false; }
    order.insert(order.begin() + inserted, child);
    for (int i = inserted; i < GetItemCount(); ++i) positions.at(order[i]) = i;
    ++structureRevision;
    SyncSelection();
    ApplyLayout();
    return true;
}

void DocumentTabStrip::Remove(int index) {
    if (index < 0 || index >= GetItemCount()) return;
    TCITEMW item{}; item.mask = TCIF_IMAGE;
    if (!TabCtrl_GetItem(control, index, &item) || !TabCtrl_DeleteItem(control, index)) return;
    positions.erase(order[index]); order.erase(order.begin() + index);
    for (int i = index; i < GetItemCount(); ++i) positions.at(order[i]) = i;
    if (item.iImage >= 0) TabCtrl_RemoveImage(control, item.iImage);
    ++structureRevision;
    SyncSelection(); ApplyLayout();
}

void DocumentTabStrip::SetTitle(int index, const wchar_t* title) {
    auto label = NativeTitle(title);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = label.data();
    TabCtrl_SetItem(control, index, &item);
}

void DocumentTabStrip::SetIcon(HWND child, HICON icon) {
    const int index = IndexOf(child);
    if (!icons || !icon || index < 0) return;
    TCITEMW item{}; item.mask = TCIF_IMAGE;
    if (!TabCtrl_GetItem(control, index, &item)) return;
    const auto image = ImageList_ReplaceIcon(icons, item.iImage, icon);
    if (image < 0) return;
    item.iImage = image; TabCtrl_SetItem(control, index, &item);
}

void DocumentTabStrip::UpdateImages() {
    const auto dpi = TitleDpi();
    auto replacement = ImageList_Create(GetSystemMetricsForDpi(SM_CXSMICON, dpi),
        GetSystemMetricsForDpi(SM_CYSMICON, dpi), ILC_COLOR32 | ILC_MASK, 8, 8);
    if (!replacement) return;
    if (icons) ImageList_Destroy(TabCtrl_SetImageList(control, replacement));
    else TabCtrl_SetImageList(control, replacement);
    icons = replacement;
    for (int index = 0; index < GetItemCount(); ++index) {
        TCITEMW item{};
        item.mask = TCIF_IMAGE;
        item.iImage = -1;
        const HWND child = ChildAt(index);
        if (const auto icon = reinterpret_cast<HICON>(::SendMessageW(child, WM_GETICON, ICON_SMALL, 0)))
            item.iImage = ImageList_AddIcon(icons, icon);
        TabCtrl_SetItem(control, index, &item);
    }
}

void DocumentTabStrip::SyncSelection(HWND active) {
    if (!active) active = reinterpret_cast<HWND>(::SendMessageW(mdi, WM_MDIGETACTIVE, 0, 0));
    const int index = IndexOf(active);
    if (index != GetCurSel()) TabCtrl_SetCurSel(control, index);
}

bool DocumentTabStrip::MoveItem(int from, int to) {
    if (from < 0 || to < 0 || from >= GetItemCount() || to >= GetItemCount() || from == to) return false;
    const HWND child = ChildAt(from);
    auto label = NativeTitle(WindowTitle(child));
    TCITEMW item{};
    item.mask = TCIF_IMAGE;
    if (!TabCtrl_GetItem(control, from, &item)) return false;
    item.mask |= TCIF_TEXT | TCIF_PARAM;
    item.pszText = label.data();
    item.lParam = reinterpret_cast<LPARAM>(child);
    const int inserted = TabCtrl_InsertItem(control, to + (from < to), &item);
    if (inserted < 0) return false;
    if (!TabCtrl_DeleteItem(control, from + (inserted <= from))) {
        TabCtrl_DeleteItem(control, inserted); return false;
    }
    if (from < to) std::rotate(order.begin() + from, order.begin() + from + 1, order.begin() + to + 1);
    else std::rotate(order.begin() + to, order.begin() + from, order.begin() + from + 1);
    for (int i = (std::min)(from, to); i <= (std::max)(from, to); ++i) positions.at(order[i]) = i;
    ++structureRevision;
    SyncSelection();
    ::PostMessageW(frame, WM_DOCUMENT_CHANGED, 0, 0);
    return true;
}

BOOL DocumentTabStrip::GetItemRect(int index, RECT* rect) const {
#ifdef MULTIPAD4_TAB_STRIP_TEST_HOOKS
#include "../tests/hooks/DocumentTabStrip.cpp.01.inc"
#endif
    return TabCtrl_GetItemRect(control, index, rect);
}
void DocumentTabStrip::EnsureVisible(int index, bool, bool) {
    // The common control scrolls its single row when the focus item changes.
    SendMessage(TCM_SETCURFOCUS, index, 0);
}

void DocumentTabStrip::SetTitleFont(const std::wstring& face, long points) {
    LOGFONTW font{};
    if (face.empty() || face.size() >= LF_FACESIZE || face.find(L'\0') != std::wstring::npos ||
        points < titleMinPoints || points > titleMaxPoints) {
        points = 0;
        SystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(font), &font, 0, TitleDpi());
    } else {
        wcscpy_s(font.lfFaceName, face.c_str());
        font.lfHeight = -MulDiv(points, TitleDpi(), 72);
        font.lfWeight = FW_NORMAL;
        font.lfCharSet = DEFAULT_CHARSET;
    }
    const HFONT replacement = CreateFontIndirectW(&font);
    if (!replacement) return;
    SendMessage(WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    if (titleFont) DeleteObject(titleFont);
    titleFont = replacement;
    titlePoints = static_cast<int>(points);
    SetFixedTabWidth(fixedTabWidth);
}

int DocumentTabStrip::TitleFontHeight() const {
    HDC dc = GetDC(control);
    if (!dc) return 0;
    auto old = SelectObject(dc, titleFont);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    if (old) SelectObject(dc, old);
    ReleaseDC(control, dc);
    return metrics.tmHeight;
}

std::wstring DocumentTabStrip::NativeTitle(std::wstring title) const {
    const HDC dc = GetDC(control);
    if (!dc) return title;
    const auto oldFont = titleFont ? SelectObject(dc, titleFont) : nullptr;
    const UINT dpi = TitleDpi();
    SIZE space{};
    GetTextExtentPoint32W(dc, L"\u00a0", 1, &space);
    const int spaceWidth = std::max(1L, space.cx);
    const int padding = MulDiv(13, dpi, 96);
    const int spaces = (padding + spaceWidth - 1) / spaceWidth;
    if (fixedTabWidth) {
        const int available = MulDiv(fixedTabWidth - 17, dpi, 96) - spaces * spaceWidth -
            GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        const int length = static_cast<int>(title.size());
        SIZE extent{};
        GetTextExtentPoint32W(dc, title.data(), length, &extent);
        if (extent.cx > available) {
            GetTextExtentPoint32W(dc, L"\u2026", 1, &extent);
            int fit = 0;
            if (available >= extent.cx) {
                GetTextExtentExPointW(dc, title.data(), length, available - extent.cx, &fit, nullptr, &extent);
                if (fit > 0 && IS_HIGH_SURROGATE(title[fit - 1])) --fit;
                title.resize(fit);
                title += L'\u2026';
            } else {
                title.clear();
            }
        }
    }
    if (oldFont) SelectObject(dc, oldFont);
    ReleaseDC(control, dc);
    // Native tabs center the icon and label together. Trailing space makes room for the close button.
    title.append(spaces, L'\u00a0');
    return title;
}

void DocumentTabStrip::SetFixedTabWidth(int width) {
    fixedTabWidth = width;
    auto style = GetWindowLongPtrW(control, GWL_STYLE);
    if (width) style |= TCS_FIXEDWIDTH;
    else style &= ~TCS_FIXEDWIDTH;
    SetWindowLongPtrW(control, GWL_STYLE, style);
    TabCtrl_SetItemSize(control, MulDiv(width, TitleDpi(), 96), TitleFontHeight() + MulDiv(5, TitleDpi(), 96));
    for (int index = 0; index < GetItemCount(); ++index)
        SetTitle(index, WindowTitle(ChildAt(index)).c_str());
    InvalidateRect(control, nullptr, TRUE);
    ApplyLayout();
}

void DocumentTabStrip::ApplyLayout() {
    if (!control) return;
    RECT area{};
    GetWindowRect(layoutWindow, &area);
    MapWindowPoints(nullptr, frame, reinterpret_cast<POINT*>(&area), 2);
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    const int tabHeight = GetItemCount() ? std::min(height, std::max(MulDiv(25, TitleDpi(), 96),
        TitleFontHeight() + MulDiv(10, TitleDpi(), 96))) : 0;
    const int closeWidth = std::min(width, MulDiv(22, TitleDpi(), 96));
    SetWindowPos(bar, nullptr, area.left, area.top, width, tabHeight,
        SWP_NOZORDER | SWP_NOACTIVATE | (tabHeight ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    SetWindowPos(control, nullptr, 0, 0, width - closeWidth, tabHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    LayoutCloseButton(width, tabHeight);
    SetWindowPos(mdi, nullptr, area.left, area.top + tabHeight, width, height - tabHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

void DocumentTabStrip::LayoutCloseButton(int width, int height) {
    const int closeWidth = std::min(width, MulDiv(22, TitleDpi(), 96));
    RECT item{}, client{};
    if (GetItemRect(0, &item) && GetClientRect(control, &client) && !IsRectEmpty(&client)) {
        // The MDI client provides the page. Clip the native control to its tab row.
        RECT tabRow{0, 0, client.right, std::min(client.bottom, item.bottom)};
        RECT current{};
        if (GetWindowRgnBox(control, &current) == ERROR || !EqualRect(&current, &tabRow)) {
            if (HRGN region = CreateRectRgnIndirect(&tabRow); region && !SetWindowRgn(control, region, TRUE))
                DeleteObject(region);
        }
    }
    int buttonHeight = std::min(height, closeWidth);
    int buttonTop = (height - buttonHeight) / 2;
    if (const HWND scroll = FindWindowExW(control, nullptr, UPDOWN_CLASSW, nullptr)) {
        DWORD_PTR data{};
        if (!GetWindowSubclass(scroll, TabButtonProcedure, scrollButtons, &data))
            SetWindowSubclass(scroll, TabButtonProcedure, scrollButtons, 0);
        RECT buttons{};
        if ((GetWindowLongPtrW(scroll, GWL_STYLE) & WS_VISIBLE) && GetWindowRect(scroll, &buttons)) {
            MapWindowPoints(nullptr, bar, reinterpret_cast<POINT*>(&buttons), 2);
            buttonTop = buttons.top;
            buttonHeight = buttons.bottom - buttons.top;
        }
    }
    SetWindowPos(close, nullptr, width - closeWidth, buttonTop, closeWidth, buttonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

int DocumentTabStrip::HitTest(POINT point) const {
    TCHITTESTINFO hit{};
    hit.pt = point;
    return TabCtrl_HitTest(control, &hit);
}

RECT DocumentTabStrip::TabViewport() const {
    RECT visible{};
    GetClientRect(control, &visible);
    RECT region{};
    const auto kind = GetWindowRgnBox(control, &region);
    if (kind == NULLREGION) return {};
    if (kind != ERROR) IntersectRect(&visible, &visible, &region);
    if (const HWND scroll = FindWindowExW(control, nullptr, UPDOWN_CLASSW, nullptr);
        scroll && (GetWindowLongPtrW(scroll, GWL_STYLE) & WS_VISIBLE)) {
        RECT buttons{};
        GetWindowRect(scroll, &buttons);
        MapWindowPoints(nullptr, control, reinterpret_cast<POINT*>(&buttons), 2);
        RECT overlap{};
        if (IntersectRect(&overlap, &visible, &buttons)) {
            if (buttons.left + buttons.right < visible.left + visible.right) visible.left = (std::max)(visible.left, buttons.right);
            else visible.right = (std::min)(visible.right, buttons.left);
        }
    }
    return visible;
}

RECT DocumentTabStrip::TabCloseRect(int index, const RECT& viewport) const {
    RECT tab{};
    if (index < 0 || !GetItemRect(index, &tab)) return {};
    return TabCloseRect(tab, viewport);
}

RECT DocumentTabStrip::TabCloseRect(const RECT& tab, const RECT& viewport) const {
    RECT clipped{};
    if (!IntersectRect(&clipped, &tab, &viewport)) return {};
    const int size = std::min<LONG>(MulDiv(14, TitleDpi(), 96), clipped.bottom - clipped.top);
    const LONG right = tab.right - MulDiv(3, TitleDpi(), 96);
    if (right > clipped.right || right - size < clipped.left || size < MulDiv(10, TitleDpi(), 96)) return {};
    const LONG top = (clipped.top + clipped.bottom - size) / 2;
    return {right - size, top, right, top + size};
}

HWND DocumentTabStrip::TabCloseAt(POINT point) const {
    const int index = HitTest(point);
    const RECT bounds = TabCloseRect(index, TabViewport());
    return PtInRect(&bounds, point) ? ChildAt(index) : nullptr;
}

void DocumentTabStrip::InvalidateCloses(HWND oldChild, HWND newChild) const {
    if (!oldChild && !newChild) return;
#ifdef MULTIPAD4_TAB_STRIP_TEST_HOOKS
#include "../tests/hooks/DocumentTabStrip.cpp.02.inc"
#endif
    const RECT viewport = TabViewport();
    auto invalidate = [&](HWND child) {
        if (!child) return;
        const RECT bounds = TabCloseRect(IndexOf(child), viewport);
        if (!IsRectEmpty(&bounds)) InvalidateRect(control, &bounds, FALSE);
    };
    invalidate(oldChild); if (newChild != oldChild) invalidate(newChild);
}

void DocumentTabStrip::PaintTabCloses(HDC dc) const {
#ifdef MULTIPAD4_TAB_STRIP_TEST_HOOKS
#include "../tests/hooks/DocumentTabStrip.cpp.03.inc"
#endif
    const int count = GetItemCount();
    const RECT viewport = TabViewport();
    RECT clip{}, dirty{};
    if (GetClipBox(dc, &clip) == ERROR || !IntersectRect(&dirty, &clip, &viewport)) return;
    auto paint = [&](int index, const RECT& tab) {
        const RECT button = TabCloseRect(tab, viewport);
        if (IsRectEmpty(&button) || !RectVisible(dc, &button)) return;
        const HWND child = ChildAt(index);
        const bool hot = hotClose == child && dragIndex < 0 && (!pressedClose || pressedClose == child);
#ifdef MULTIPAD4_TAB_STRIP_TEST_HOOKS
#include "../tests/hooks/DocumentTabStrip.cpp.04.inc"
#endif
        PaintTabButton(control, dc, button, 0, hot, true, true);
    };
    if (count <= 8) {
        for (int index = 0; index < count; ++index) { RECT tab{}; if (GetItemRect(index, &tab)) paint(index, tab); }
        return;
    }
    RECT first{}, last{};
    if (!GetItemRect(0, &first) || !GetItemRect(count - 1, &last)) return;
    const bool forward = first.left <= last.left;
    auto native = [&](int sorted) { return forward ? sorted : count - sorted - 1; };
    int lower = 0, upper = count;
    while (lower < upper) {
        const int middle = lower + (upper - lower) / 2; RECT tab{};
        if (!GetItemRect(native(middle), &tab)) return;
        if (tab.right <= dirty.left) lower = middle + 1; else upper = middle;
    }
    for (int sorted = lower; sorted < count; ++sorted) {
        RECT tab{}; const int index = native(sorted);
        if (!GetItemRect(index, &tab) || tab.left >= dirty.right) break;
        paint(index, tab);
    }
}

void DocumentTabStrip::Activate(int index) {
    const HWND child = ChildAt(index);
    if (!child) return;
    BOOL maximized = FALSE;
    const auto active = reinterpret_cast<HWND>(::SendMessageW(mdi, WM_MDIGETACTIVE, 0, reinterpret_cast<LPARAM>(&maximized)));
    // Native MDI briefly restores the outgoing child before maximizing the incoming one.
    const bool pauseRedraw = maximized && active != child && (GetWindowLongPtrW(mdi, GWL_STYLE) & WS_VISIBLE);
    if (pauseRedraw) ::SendMessageW(mdi, WM_SETREDRAW, FALSE, 0);
    ::SendMessageW(mdi, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(child), 0);
    if (pauseRedraw) {
        ::SendMessageW(mdi, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(mdi, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
}

LRESULT CALLBACK DocumentTabStrip::WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* tabs = reinterpret_cast<DocumentTabStrip*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (tabs && window == tabs->layoutWindow && message == WM_WINDOWPOSCHANGED) tabs->ApplyLayout();
    if (tabs && window == tabs->bar) {
        if (message == WM_NOTIFY) {
            const auto& notification = *reinterpret_cast<const NMHDR*>(l);
            if (notification.code == TCN_SELCHANGE) tabs->Activate(tabs->GetCurSel());
            if (notification.code == TTN_GETDISPINFOW && notification.hwndFrom == TabCtrl_GetToolTips(tabs->control)) {
                const HWND child = tabs->ChildAt(static_cast<int>(notification.idFrom));
                tabs->tooltip = WindowTitle(child);
                reinterpret_cast<NMTTDISPINFOW*>(l)->lpszText = tabs->tooltip.data();
                return 0;
            }
        }
        if (message == WM_COMMAND && LOWORD(w) == closeId)
            ::PostMessageW(tabs->frame, WM_CLOSE_TAB, reinterpret_cast<WPARAM>(tabs->ChildAt(tabs->GetCurSel())), 0);
    }
    return DefWindowProcW(window, message, w, l);
}

LRESULT CALLBACK DocumentTabStrip::TabProcedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto& tabs = *reinterpret_cast<DocumentTabStrip*>(data);
    POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    switch (message) {
    case WM_PAINT: {
#ifdef MULTIPAD4_TAB_STRIP_TEST_HOOKS
#include "../tests/hooks/DocumentTabStrip.cpp.05.inc"
#endif
        HRGN update = CreateRectRgn(0, 0, 0, 0);
        int kind = update ? GetUpdateRgn(window, update, FALSE) : ERROR;
#ifdef MULTIPAD4_TAB_STRIP_TEST_HOOKS
#include "../tests/hooks/DocumentTabStrip.cpp.06.inc"
#endif
        const auto result = DefSubclassProc(window, message, w, l);
        if (HDC dc = GetDC(window)) {
            const int saved = SaveDC(dc);
            if (saved) {
                if (kind == ERROR) tabs.PaintTabCloses(dc);
                else if (kind != NULLREGION) {
                    ExtSelectClipRgn(dc, update, RGN_AND); tabs.PaintTabCloses(dc);
                }
                RestoreDC(dc, saved);
            } else if (kind != NULLREGION) tabs.PaintTabCloses(dc);
            ReleaseDC(window, dc);
        }
        if (update) DeleteObject(update);
        return result;
    }
    case WM_PRINT:
    case WM_PRINTCLIENT: {
        const auto dc = reinterpret_cast<HDC>(w);
        const int saved = SaveDC(dc); if (!saved) break;
        RECT row{};
        if (GetWindowRgnBox(window, &row) == ERROR) GetClientRect(window, &row);
        IntersectClipRect(dc, row.left, row.top, row.right, row.bottom);
        const int nativeState = SaveDC(dc);
        const auto result = DefSubclassProc(window, message, w, l);
        if (nativeState) RestoreDC(dc, nativeState);
        tabs.PaintTabCloses(dc);
        RestoreDC(dc, saved);
        return result;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        if (const HWND child = tabs.TabCloseAt(point)) {
            const HWND old = tabs.hotClose;
            tabs.pressedClose = tabs.hotClose = child;
            SetCapture(window);
            tabs.InvalidateCloses(old, child);
            return 0;
        }
        if (message == WM_LBUTTONDBLCLK) break;
        const auto result = DefSubclassProc(window, message, w, l);
        tabs.dragIndex = tabs.HitTest(point);
        tabs.dragOrigin = point;
        if (tabs.dragIndex >= 0) { SetCapture(window); tabs.InvalidateCloses(tabs.hotClose); }
        return result;
    }
    case WM_MOUSEMOVE: {
        const auto hot = tabs.TabCloseAt(point);
        if (hot != tabs.hotClose) {
            const HWND old = tabs.hotClose; tabs.hotClose = hot;
            tabs.InvalidateCloses(old, hot);
        }
        if (hot) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
        }
        if (tabs.pressedClose) return 0;
        if (tabs.dragIndex >= 0 && GetCapture() == window && (w & MK_LBUTTON)) {
            if (abs(point.x - tabs.dragOrigin.x) >= GetSystemMetrics(SM_CXDRAG) ||
                abs(point.y - tabs.dragOrigin.y) >= GetSystemMetrics(SM_CYDRAG)) tabs.dragging = true;
            const int target = tabs.HitTest(point);
            if (tabs.dragging && tabs.MoveItem(tabs.dragIndex, target)) tabs.dragIndex = target;
        }
        break;
    }
    case WM_MOUSELEAVE: {
        const HWND old = tabs.hotClose; tabs.hotClose = nullptr;
        tabs.InvalidateCloses(old); break;
    }
    case WM_LBUTTONUP:
        if (const HWND child = tabs.pressedClose) {
            const HWND old = tabs.hotClose;
            tabs.hotClose = tabs.TabCloseAt(point); tabs.pressedClose = nullptr;
            tabs.InvalidateCloses(old, tabs.hotClose); tabs.InvalidateCloses(child);
            ReleaseCapture();
            if (child == tabs.hotClose) ::PostMessageW(tabs.frame, WM_CLOSE_TAB, reinterpret_cast<WPARAM>(child), 0);
            return 0;
        }
        if (GetCapture() == window) ReleaseCapture();
        break;
    case WM_CAPTURECHANGED: {
        const HWND old = tabs.pressedClose;
        const bool wasDragging = tabs.dragIndex >= 0;
        tabs.pressedClose = nullptr; tabs.dragIndex = -1; tabs.dragging = false;
        tabs.InvalidateCloses(old, wasDragging ? tabs.hotClose : nullptr);
        break;
    }
    case WM_MBUTTONUP:
        if (auto child = tabs.ChildAt(tabs.HitTest(point)))
            ::PostMessageW(tabs.frame, WM_CLOSE_TAB, reinterpret_cast<WPARAM>(child), 0);
        return 0;
    case WM_RBUTTONUP:
        if (auto child = tabs.ChildAt(tabs.HitTest(point))) {
            ClientToScreen(window, &point);
            ::PostMessageW(tabs.frame, WM_TAB_CONTEXT,
                reinterpret_cast<WPARAM>(child), MAKELPARAM(point.x, point.y));
        }
        return 0;
    }
    const auto result = DefSubclassProc(window, message, w, l);
    switch (message) {
    case WM_SIZE:
    case WM_SETFONT:
    case WM_STYLECHANGED:
    case WM_HSCROLL:
    case TCM_SETCURFOCUS:
    case TCM_SETCURSEL:
        InvalidateRect(window, nullptr, FALSE); break;
    case TCM_INSERTITEMW:
    case TCM_SETITEMW:
    case TCM_DELETEITEM:
    case TCM_SETITEMSIZE: {
        InvalidateRect(window, nullptr, FALSE);
        RECT area{};
        GetClientRect(tabs.bar, &area);
        tabs.LayoutCloseButton(area.right, area.bottom);
        break;
    }
    }
    return result;
}

#include "DocumentTabStrip.h"
#include "App.h"
#include <windowsx.h>
#include <algorithm>

namespace {
constexpr auto layoutClass = L"MultiPad4.DocumentLayout";
constexpr int closeId = 1;
constexpr UINT_PTR scrollButtons = 0, closeButton = 1;

void PaintTabButton(HWND window, HDC dc, RECT bounds, int direction, bool hot, bool enabled) {
    const bool pressed = hot && GetCapture() == window && (GetKeyState(VK_LBUTTON) < 0);
    const auto background = enabled && hot ? (pressed ? COLOR_3DSHADOW : COLOR_WINDOW) : COLOR_BTNFACE;
    FillRect(dc, &bounds, GetSysColorBrush(background));
    const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
    const UINT dpi = GetDpiForWindow(window);
    const int radius = MulDiv(4, dpi, 96);
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
        if (GetFocus() == window && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
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

int DocumentTabStrip::GetItemCount() const { return control ? TabCtrl_GetItemCount(control) : 0; }
int DocumentTabStrip::GetCurSel() const { return control ? TabCtrl_GetCurSel(control) : -1; }
HWND DocumentTabStrip::ChildAt(int index) const {
    TCITEMW item{};
    item.mask = TCIF_PARAM;
    return TabCtrl_GetItem(control, index, &item) ? reinterpret_cast<HWND>(item.lParam) : nullptr;
}

bool DocumentTabStrip::Insert(HWND child, const wchar_t* title) {
    TCITEMW item{};
    item.mask = TCIF_TEXT | TCIF_PARAM | TCIF_IMAGE;
    item.pszText = const_cast<wchar_t*>(title);
    item.lParam = reinterpret_cast<LPARAM>(child);
    item.iImage = -1;
    if (TabCtrl_InsertItem(control, GetItemCount(), &item) < 0) return false;
    SyncSelection();
    ApplyLayout();
    return true;
}

void DocumentTabStrip::Remove(int index) {
    TCITEMW item{};
    item.mask = TCIF_IMAGE;
    if (TabCtrl_GetItem(control, index, &item) && item.iImage >= 0) TabCtrl_RemoveImage(control, item.iImage);
    TabCtrl_DeleteItem(control, index);
    SyncSelection();
    ApplyLayout();
}

void DocumentTabStrip::SetTitle(int index, const wchar_t* title) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(title);
    TabCtrl_SetItem(control, index, &item);
}

void DocumentTabStrip::SetIcon(HWND child, HICON icon) {
    if (!icons || !icon) return;
    for (int index = 0; index < GetItemCount(); ++index) {
        if (ChildAt(index) != child) continue;
        TCITEMW item{};
        item.mask = TCIF_IMAGE;
        if (!TabCtrl_GetItem(control, index, &item)) return;
        const auto image = ImageList_ReplaceIcon(icons, item.iImage, icon);
        if (image < 0) return;
        item.iImage = image;
        TabCtrl_SetItem(control, index, &item);
        return;
    }
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
    for (int i = 0; i < GetItemCount(); ++i) {
        if (ChildAt(i) == active) {
            if (i != GetCurSel()) TabCtrl_SetCurSel(control, i);
            return;
        }
    }
    TabCtrl_SetCurSel(control, -1);
}

bool DocumentTabStrip::MoveItem(int from, int to) {
    if (from < 0 || to < 0 || from >= GetItemCount() || to >= GetItemCount() || from == to) return false;
    const HWND child = ChildAt(from);
    const int length = GetWindowTextLengthW(child);
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(child, title.data(), length + 1);
    TCITEMW item{};
    item.mask = TCIF_IMAGE;
    if (!TabCtrl_GetItem(control, from, &item)) return false;
    item.mask |= TCIF_TEXT | TCIF_PARAM;
    item.pszText = title.data();
    item.lParam = reinterpret_cast<LPARAM>(child);
    const int inserted = TabCtrl_InsertItem(control, to + (from < to), &item);
    if (inserted < 0) return false;
    TabCtrl_DeleteItem(control, from + (inserted <= from));
    SyncSelection();
    ::PostMessageW(frame, WM_DOCUMENT_CHANGED, 0, 0);
    return true;
}

BOOL DocumentTabStrip::GetItemRect(int index, RECT* rect) const { return TabCtrl_GetItemRect(control, index, rect); }
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

void DocumentTabStrip::SetFixedTabWidth(int width) {
    fixedTabWidth = width;
    auto style = GetWindowLongPtrW(control, GWL_STYLE);
    if (width) style |= TCS_FIXEDWIDTH;
    else style &= ~TCS_FIXEDWIDTH;
    SetWindowLongPtrW(control, GWL_STYLE, style);
    TabCtrl_SetItemSize(control, MulDiv(width, TitleDpi(), 96), TitleFontHeight() + MulDiv(5, TitleDpi(), 96));
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
                const int length = GetWindowTextLengthW(child);
                tabs->tooltip.resize(static_cast<size_t>(length) + 1);
                tabs->tooltip.resize(GetWindowTextW(child, tabs->tooltip.data(), length + 1));
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
    case WM_PRINT:
    case WM_PRINTCLIENT: {
        // PrintWindow supplies a DC without the window's screen clipping region.
        const auto dc = reinterpret_cast<HDC>(w);
        RECT row{};
        if (GetWindowRgnBox(window, &row) == ERROR) break;
        const int saved = SaveDC(dc);
        if (!saved) break;
        IntersectClipRect(dc, row.left, row.top, row.right, row.bottom);
        const auto result = DefSubclassProc(window, message, w, l);
        RestoreDC(dc, saved);
        return result;
    }
    case WM_LBUTTONDOWN: {
        const auto result = DefSubclassProc(window, message, w, l);
        tabs.dragIndex = tabs.HitTest(point);
        tabs.dragOrigin = point;
        if (tabs.dragIndex >= 0) SetCapture(window);
        return result;
    }
    case WM_MOUSEMOVE:
        if (tabs.dragIndex >= 0 && GetCapture() == window && (w & MK_LBUTTON)) {
            if (abs(point.x - tabs.dragOrigin.x) >= GetSystemMetrics(SM_CXDRAG) ||
                abs(point.y - tabs.dragOrigin.y) >= GetSystemMetrics(SM_CYDRAG)) tabs.dragging = true;
            const int target = tabs.HitTest(point);
            if (tabs.dragging && tabs.MoveItem(tabs.dragIndex, target)) tabs.dragIndex = target;
        }
        break;
    case WM_LBUTTONUP:
        if (GetCapture() == window) ReleaseCapture();
        break;
    case WM_CAPTURECHANGED:
        tabs.dragIndex = -1;
        tabs.dragging = false;
        break;
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
    case TCM_INSERTITEMW:
    case TCM_SETITEMW:
    case TCM_DELETEITEM:
    case TCM_SETITEMSIZE: {
        RECT area{};
        GetClientRect(tabs.bar, &area);
        tabs.LayoutCloseButton(area.right, area.bottom);
        break;
    }
    }
    return result;
}

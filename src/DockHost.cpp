#include "DockHost.h"
LRESULT DockPane::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
    list.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE);
    list.SetFont(static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))); handled = FALSE; return 0;
}
LRESULT DockPane::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) {
    RECT r{}; GetClientRect(&r); if (list) list.MoveWindow(&r); floating = !IsDocking(); handled = FALSE; return 0;
}
LRESULT DockPane::OnClose(UINT, WPARAM, LPARAM, BOOL&) { ::PostMessageW(owner, WM_PANE_ACTION, 0, 0); return 0; }
LRESULT DockPane::OnContext(UINT, WPARAM, LPARAM l, BOOL&) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"停靠到左侧"); AppendMenuW(menu, MF_STRING, 2, L"停靠到右侧");
    AppendMenuW(menu, MF_STRING, 3, L"停靠到底部"); AppendMenuW(menu, MF_STRING, 4, L"浮动");
    AppendMenuW(menu, MF_STRING, 5, L"隐藏");
    POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    if (p.x == -1 && p.y == -1) { p = {10, 10}; ClientToScreen(&p); }
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, m_hWnd, nullptr);
    DestroyMenu(menu);
    if (selected) ::PostMessageW(owner, WM_PANE_ACTION, selected == 5 ? 0 : selected, 0);
    return 0;
}
LRESULT DockPane::OnSelect(WORD, WORD, HWND, BOOL&) {
    int i = list.GetCurSel(); if (i >= 0) ::PostMessageW(owner, WM_PANE_ACTION, 10, i); return 0;
}
void DockHost::Create(HWND frame) {
    owner = frame; pane.owner = frame; RECT rect{100, 100, 420, 520};
    pane.Create(frame, rect, L"打开的文档"); SetSide(DockSide::Left);
}
void DockHost::SetSide(DockSide value) {
    if (!visible) { pane.Show(); visible = true; }
    if (value == DockSide::Floating) {
        RECT rect{}; ::GetWindowRect(owner, &rect);
        rect = {rect.left + 50, rect.top + 90, rect.left + 370, rect.top + 510};
        if (pane.IsDocking()) pane.Float(&rect);
        else pane.SetWindowPos(HWND_TOP, &rect, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else {
        dockwins::DFDOCKPOS pos{};
        pos.hdr.hWnd = pane; pos.hdr.hBar = owner;
        pos.dwDockSide = value == DockSide::Left ? dockwins::CDockingSide::sLeft
            : value == DockSide::Right ? dockwins::CDockingSide::sRight : dockwins::CDockingSide::sBottom;
        pos.nWidth = MulDiv(extent, GetDpiForWindow(owner), 96); pos.nHeight = pos.nWidth;
        pos.fPctPos = 0.0F;
        pane.SetDockingPosition(&pos);
    }
    side = value; pane.floating = !pane.IsDocking(); visible = true;
}
void DockHost::Toggle() {
    if (visible) pane.Hide(); else pane.Show(); visible = !visible;
}

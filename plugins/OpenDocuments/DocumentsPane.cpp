#include "DocumentsPane.h"
LRESULT DockPane::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
    list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(100), reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
    if (!list) return -1;
    ::SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    handled = FALSE; return 0;
}
LRESULT DockPane::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) {
    RECT r{}; GetClientRect(&r); if (list) ::MoveWindow(list, 0, 0, r.right, r.bottom, TRUE); floating = !IsDocking(); handled = FALSE; return 0;
}
LRESULT DockPane::OnClose(UINT, WPARAM, LPARAM, BOOL&) { PostMessage(WM_APP + 0x102, 0, 0); return 0; }
LRESULT DockPane::OnContext(UINT, WPARAM, LPARAM l, BOOL&) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"停靠到左侧"); AppendMenuW(menu, MF_STRING, 2, L"停靠到右侧");
    AppendMenuW(menu, MF_STRING, 3, L"停靠到底部"); AppendMenuW(menu, MF_STRING, 4, L"浮动");
    AppendMenuW(menu, MF_STRING, 5, L"隐藏");
    POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    if (p.x == -1 && p.y == -1) { p = {10, 10}; ClientToScreen(&p); }
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, m_hWnd, nullptr);
    DestroyMenu(menu);
    if (selected) PostMessage(WM_APP + 0x102, selected == 5 ? 0 : selected, 0);
    return 0;
}
LRESULT DockPane::OnSelect(WORD, WORD, HWND, BOOL&) {
    ActivateSelection(); return 0;
}
void DockHost::Create(HWND frame, HINSTANCE instance) {
    owner = frame; pane.owner = frame;
    pane.host = this;
    DockPlacement placement{};
    placement.floating = {100, 100, 420, 520};
    placement.side = PaneSide::Left;
    placement.width = placement.height = MulDiv(extent, GetDpiForWindow(frame), 96);
    if (!pane.Create(frame, placement.floating, L"打开的文档", instance)) return;
    pane.Place(placement);
    pane.Refresh();
}
void DockHost::SetSide(DockSide value) {
    auto placement = pane.Placement();
    constexpr PaneSide sides[]{PaneSide::Left, PaneSide::Right, PaneSide::Bottom, PaneSide::Floating};
    placement.side = sides[static_cast<int>(value)];
    placement.visible = true;
    if (value == DockSide::Floating) {
        RECT rect{}; ::GetWindowRect(owner, &rect);
        placement.floating = {rect.left + 50, rect.top + 90, rect.left + 370, rect.top + 510};
    } else {
        placement.width = placement.height = MulDiv(extent, GetDpiForWindow(owner), 96);
    }
    pane.Place(placement);
    side = value; pane.floating = !pane.IsDocking(); visible = true;
}
void DockHost::Toggle() {
    if (visible) pane.Hide(); else pane.Show(); visible = !visible;
}

LRESULT DockPane::OnAction(UINT, WPARAM action, LPARAM, BOOL&) {
    if (!host) return 0;
    if (action == 0) host->Toggle();
    else if (action >= 1 && action <= 4) host->SetSide(static_cast<DockSide>(action - 1));
    return 0;
}
void DockPane::ActivateSelection() {
    int index = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (index < 0) return;
    auto id = static_cast<UINT_PTR>(::SendMessageW(list, LB_GETITEMDATA, index, 0));
    auto position = ::SendMessageW(owner, NPPM_GETPOSFROMBUFFERID, id, 0);
    if (position >= 0) ::SendMessageW(owner, NPPM_ACTIVATEDOC, 0, position);
}
void DockPane::Refresh() {
    int count = static_cast<int>(::SendMessageW(owner, NPPM_GETNBOPENFILES, 0, 1));
    std::vector<std::wstring> labels;
    std::vector<UINT_PTR> buffers;
    for (int i = 0; i < count; ++i) {
        auto id = static_cast<UINT_PTR>(::SendMessageW(owner, NPPM_GETBUFFERIDFROMPOS, i, 0));
        MultiPadBufferTitle request{};
        auto length = ::SendMessageW(owner, MPM_GETBUFFERTITLE, id, reinterpret_cast<LPARAM>(&request));
        if (length < 0) continue;
        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        request = {title.data(), title.size()};
        ::SendMessageW(owner, MPM_GETBUFFERTITLE, id, reinterpret_cast<LPARAM>(&request));
        title.resize(static_cast<size_t>(length));
        labels.push_back(std::move(title)); buffers.push_back(id);
    }
    bool changed = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0)) != static_cast<int>(labels.size());
    for (int i = 0; !changed && i < static_cast<int>(labels.size()); ++i) {
        int length = static_cast<int>(::SendMessageW(list, LB_GETTEXTLEN, i, 0));
        std::wstring old(static_cast<size_t>(length) + 1, L'\0');
        ::SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(old.data())); old.resize(length);
        changed = old != labels[i] || ::SendMessageW(list, LB_GETITEMDATA, i, 0) != buffers[i];
    }
    if (changed) {
        ::SendMessageW(list, WM_SETREDRAW, FALSE, 0); ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < labels.size(); ++i) {
            int index = static_cast<int>(::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[i].c_str())));
            if (index >= 0) ::SendMessageW(list, LB_SETITEMDATA, index, buffers[i]);
        }
        ::SendMessageW(list, WM_SETREDRAW, TRUE, 0); ::InvalidateRect(list, nullptr, TRUE);
    }
    UpdateSelection();
}
void DockPane::UpdateSelection() {
    ::SendMessageW(list, LB_SETCURSEL, ::SendMessageW(owner, NPPM_GETCURRENTDOCINDEX, 0, 0), 0);
}

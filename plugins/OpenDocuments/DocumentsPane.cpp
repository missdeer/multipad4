#include "DocumentsPane.h"
LRESULT DockPane::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
    list.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, WS_EX_CLIENTEDGE, 100);
    list.SetFont(static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))); handled = FALSE; return 0;
}
LRESULT DockPane::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) {
    RECT r{}; GetClientRect(&r); if (list) list.MoveWindow(&r); floating = !IsDocking(); handled = FALSE; return 0;
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
void DockHost::Create(HWND frame) {
    owner = frame; pane.owner = frame; RECT rect{100, 100, 420, 520};
    pane.Create(frame, rect, L"打开的文档");
    if (!pane.IsWindow()) return;
    SetWindowLongPtrW(pane, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetSide(DockSide::Left); pane.Refresh();
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

LRESULT DockPane::OnAction(UINT, WPARAM action, LPARAM, BOOL&) {
    auto* host = reinterpret_cast<DockHost*>(::GetWindowLongPtrW(m_hWnd, GWLP_USERDATA));
    if (!host) return 0;
    if (action == 0) host->Toggle();
    else if (action >= 1 && action <= 4) host->SetSide(static_cast<DockSide>(action - 1));
    return 0;
}
void DockPane::ActivateSelection() {
    int index = list.GetCurSel();
    if (index < 0) return;
    auto id = static_cast<UINT_PTR>(list.GetItemData(index));
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
    bool changed = list.GetCount() != static_cast<int>(labels.size());
    for (int i = 0; !changed && i < static_cast<int>(labels.size()); ++i) {
        int length = list.GetTextLen(i);
        std::wstring old(static_cast<size_t>(length) + 1, L'\0');
        list.GetText(i, old.data()); old.resize(length);
        changed = old != labels[i] || list.GetItemData(i) != buffers[i];
    }
    if (changed) {
        list.SetRedraw(FALSE); list.ResetContent();
        for (size_t i = 0; i < labels.size(); ++i) {
            int index = list.AddString(labels[i].c_str());
            if (index >= 0) list.SetItemData(index, buffers[i]);
        }
        list.SetRedraw(TRUE); list.Invalidate();
    }
    list.SetCurSel(static_cast<int>(::SendMessageW(owner, NPPM_GETCURRENTDOCINDEX, 0, 0)));
}

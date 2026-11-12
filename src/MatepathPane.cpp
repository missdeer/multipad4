#include "MatepathPane.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include "Dlapi.h"

namespace {
std::wstring ControlText(HWND window) {
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), static_cast<int>(text.size())));
    return text;
}
}
void MatepathPane::CreatePane(HWND frame) {
    owner = frame;
    RECT rect{100, 100, 460, 650};
    Create(frame, rect, L"Matepath — 文件浏览器");
    SetSide(DockSide::Right);
}
void MatepathPane::DestroyPane() {
    if (IsWindow()) { if (IsDocking()) Undock(); DestroyWindow(); }
}
void MatepathPane::SetSide(DockSide value) {
    if (!visible) Show();
    if (value == DockSide::Floating) {
        RECT rect{}; ::GetWindowRect(owner, &rect);
        rect = {rect.left + 80, rect.top + 110, rect.left + 480, rect.top + 660};
        if (IsDocking()) Float(&rect);
        else SetWindowPos(HWND_TOP, &rect, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else {
        dockwins::DFDOCKPOS pos{};
        pos.hdr.hWnd = m_hWnd; pos.hdr.hBar = owner;
        pos.dwDockSide = value == DockSide::Left ? dockwins::CDockingSide::sLeft
            : value == DockSide::Right ? dockwins::CDockingSide::sRight : dockwins::CDockingSide::sBottom;
        pos.nWidth = MulDiv(300, GetDpiForWindow(owner), 96); pos.nHeight = pos.nWidth;
        SetDockingPosition(&pos);
    }
    side = value; visible = true; Layout();
}
void MatepathPane::Toggle() {
    if (visible) { side = CurrentSide(); Hide(); } else Show();
    visible = !visible;
}
DockSide MatepathPane::CurrentSide() const {
    if (!visible) return side;
    if (!IsDocking()) return DockSide::Floating;
    dockwins::DFDOCKPOS position{};
    if (GetDockingPosition(&position)) {
        switch (position.dwDockSide & 3) {
        case dockwins::CDockingSide::sLeft: return DockSide::Left;
        case dockwins::CDockingSide::sRight: return DockSide::Right;
        default: return DockSide::Bottom;
        }
    }
    return side;
}
LRESULT MatepathPane::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
    tooltips.Create(m_hWnd, rcDefault, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX);
    auto button = [&](CButton& control, int id, LPCWSTR text) {
        // Keep the descriptive caption for accessibility; draw only the icon.
        control.Create(m_hWnd, rcDefault, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, id);
        CToolInfo info(TTF_IDISHWND | TTF_SUBCLASS, m_hWnd, reinterpret_cast<UINT_PTR>(control.m_hWnd), nullptr, const_cast<LPWSTR>(text));
        tooltips.AddTool(&info);
    };
    button(back, Back, L"后退"); button(forward, Forward, L"前进"); button(up, Up, L"上一级"); button(reload, Reload, L"刷新");
    button(go, Go, L"转到"); button(apply, Filter, L"筛选");
    address.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, Address);
    pattern.Create(m_hWnd, rcDefault, L"*.*", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, Pattern);
    pattern.SetLimitText(DL_FILTER_BUFSIZE - 2);
    list.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHAREIMAGELISTS | LVS_NOCOLUMNHEADER, WS_EX_CLIENTEDGE, 200);
    list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    list.InsertColumn(0, L"名称", LVCFMT_LEFT, 280);
    for (HWND child = GetWindow(GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
        ::SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    DirList_Init(list);
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH)) Navigate(std::filesystem::path(path).parent_path());
    handled = FALSE; return 0;
}
LRESULT MatepathPane::OnEraseBackground(UINT, WPARAM dc, LPARAM, BOOL&) {
    // The docking base suppresses erasure for panes covered by a single child.
    // Our controls leave margins and gaps that need their own background.
    RECT client{}; GetClientRect(&client);
    FillRect(reinterpret_cast<HDC>(dc), &client, GetSysColorBrush(COLOR_BTNFACE));
    return 1;
}
LRESULT MatepathPane::OnDrawButton(UINT, WPARAM, LPARAM l, BOOL& handled) {
    const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(l);
    if (item->CtlType != ODT_BUTTON || item->CtlID < Back || item->CtlID > Filter) {
        handled = FALSE; return 0;
    }
    // Segoe MDL2 Assets: Back, Forward, Up, Refresh, Forward, Filter.
    constexpr wchar_t glyphs[]{0xE72B, 0xE72A, 0xE74A, 0xE72C, 0xE72A, 0xE71C};
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    RECT rect = item->rcItem;
    const int saved = SaveDC(item->hDC);
    HTHEME theme = OpenThemeData(item->hwndItem, L"Button");
    if (theme) {
        const int state = disabled ? PBS_DISABLED : pressed ? PBS_PRESSED
            : (item->itemState & ODS_HOTLIGHT) ? PBS_HOT
            : (item->itemState & ODS_FOCUS) ? PBS_DEFAULTED : PBS_NORMAL;
        DrawThemeParentBackground(item->hwndItem, item->hDC, &rect);
        DrawThemeBackground(theme, item->hDC, BP_PUSHBUTTON, state, &rect, nullptr);
        CloseThemeData(theme);
    } else {
        DrawFrameControl(item->hDC, &rect, DFC_BUTTON, DFCS_BUTTONPUSH
            | (disabled ? DFCS_INACTIVE : 0) | (pressed ? DFCS_PUSHED : 0));
        if (pressed) OffsetRect(&rect, 1, 1);
    }
    SelectObject(item->hDC, iconFont);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
    DrawTextW(item->hDC, &glyphs[item->CtlID - Back], 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((item->itemState & (ODS_FOCUS | ODS_NOFOCUSRECT)) == ODS_FOCUS) {
        RECT focus = item->rcItem; InflateRect(&focus, -3, -3);
        DrawFocusRect(item->hDC, &focus);
    }
    RestoreDC(item->hDC, saved);
    return TRUE;
}
void MatepathPane::Layout() {
    if (!list) return;
    RECT r{}; GetClientRect(&r);
    const int dpi = GetDpiForWindow(m_hWnd);
    auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };
    if (iconDpi != dpi) {
        if (iconFont) iconFont.DeleteObject();
        iconFont.CreateFontW(-px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
        iconDpi = dpi;
        for (HWND control : {back.m_hWnd, forward.m_hWnd, up.m_hWnd, reload.m_hWnd, go.m_hWnd, apply.m_hWnd})
            ::InvalidateRect(control, nullptr, TRUE);
    }
    const int gap = px(4), row = px(25), width = max(0, static_cast<int>(r.right) - gap * 2), button = px(32);
    back.MoveWindow(gap, gap, px(32), row); forward.MoveWindow(gap + px(36), gap, px(32), row);
    up.MoveWindow(gap + px(72), gap, px(32), row); reload.MoveWindow(gap + px(108), gap, button, row);
    const int editWidth = max(0, width - button - gap);
    address.MoveWindow(gap, row + gap * 2, editWidth, row);
    go.MoveWindow(gap * 2 + editWidth, row + gap * 2, button, row);
    pattern.MoveWindow(gap, row * 2 + gap * 3, editWidth, row);
    apply.MoveWindow(gap * 2 + editWidth, row * 2 + gap * 3, button, row);
    const int top = row * 3 + gap * 4;
    list.MoveWindow(gap, top, width, max(0, static_cast<int>(r.bottom) - top - gap));
    list.SetColumnWidth(0, LVSCW_AUTOSIZE_USEHEADER);
}
bool MatepathPane::Navigate(const std::filesystem::path& input, bool remember) {
    std::error_code ec;
    auto path = input.is_absolute() ? input : directory / input;
    path = std::filesystem::weakly_canonical(path, ec);
    // The upstream Shell API uses MAX_PATH buffers. Reject unsupported paths
    // before calling it, and leave the current list/history intact on failure.
    if (ec || path.native().size() >= MAX_PATH || !std::filesystem::is_directory(path, ec) || ec) return false;
    PIDLIST_ABSOLUTE pidl = nullptr;
    IShellFolder* folder = nullptr;
    HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
    if (SUCCEEDED(hr)) hr = SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(&folder));
    IEnumIDList* items = nullptr;
    if (SUCCEEDED(hr)) hr = folder->EnumObjects(m_hWnd, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &items);
    if (items) items->Release();
    if (folder) folder->Release();
    CoTaskMemFree(pidl);
    if (FAILED(hr)) return false;
    if (remember && (history.empty() || path != directory)) {
        if (!history.empty()) history.resize(historyIndex + 1);
        history.push_back(path); historyIndex = history.size() - 1;
    }
    directory = path;
    address.SetWindowTextW(directory.c_str());
    RefreshItems();
    return true;
}
void MatepathPane::RefreshItems() {
    if (directory.empty()) return;
    DirListItem selected{}; selected.mask = DLI_FILENAME;
    DirList_GetItem(list, -1, &selected);
    DirList_Fill(list, directory.c_str(), DL_FOLDERS | DL_NONFOLDERS | (showHidden ? DL_INCLHIDDEN : 0), fileSpec.c_str(), false, false, sort, reverse);
    DirList_StartIconThread(list); // Host adaptation resolves icons on this STA.
    if (*selected.szFileName) DirList_SelectItem(list, nullptr, selected.szFileName);
    back.EnableWindow(!history.empty() && historyIndex > 0);
    forward.EnableWindow(historyIndex + 1 < history.size());
    up.EnableWindow(directory.has_parent_path() && directory != directory.parent_path());
}
void MatepathPane::ActivateSelection() {
    DirListItem item{}; item.mask = DLI_ALL;
    if (DirList_GetItem(list, -1, &item) < 0) return;
    if (item.ntype == DLE_DIR) {
        if (!Navigate(item.szFileName)) MessageBeep(MB_ICONWARNING);
    } else if (item.ntype == DLE_FILE) {
        // Synchronous notification: the frame consumes the path before return.
        ::SendMessageW(owner, WM_MATEPATH_OPEN, 0, reinterpret_cast<LPARAM>(item.szFileName));
    }
}
void MatepathPane::Reveal(const std::filesystem::path& path) {
    if (Navigate(path.parent_path())) DirList_SelectItem(list, nullptr, path.c_str());
}
BOOL MatepathPane::PreTranslateMessage(MSG* msg) {
    if (msg->hwnd != m_hWnd && !IsChild(msg->hwnd)) return FALSE;
    if (msg->message == WM_KEYDOWN) {
        if (msg->wParam == VK_RETURN) {
            if (msg->hwnd == address) SendMessage(WM_COMMAND, Go);
            else if (msg->hwnd == pattern) SendMessage(WM_COMMAND, Filter);
            else if (msg->hwnd == list) ActivateSelection();
            else if (const int id = ::GetDlgCtrlID(msg->hwnd); id >= Back && id <= Filter)
                ::SendMessageW(msg->hwnd, BM_CLICK, 0, 0);
            else return FALSE;
            return TRUE;
        }
        if (msg->wParam == VK_F5) { RefreshItems(); return TRUE; }
        if (msg->hwnd == list && msg->wParam == VK_BACK) { SendMessage(WM_COMMAND, Up); return TRUE; }
    }
    if (msg->message == WM_SYSKEYDOWN && (msg->wParam == VK_LEFT || msg->wParam == VK_RIGHT || msg->wParam == VK_UP)) {
        SendMessage(WM_COMMAND, msg->wParam == VK_LEFT ? Back : msg->wParam == VK_RIGHT ? Forward : Up); return TRUE;
    }
    // This is a docking window, not a dialog. IsDialogMessage sends
    // DM_GETDEFID (WM_USER), which the framework interprets as an undock
    // notification. That clears its owner without changing WS_CHILD/parent.
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_TAB
        && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0) {
        if (HWND next = ::GetNextDlgTabItem(m_hWnd, msg->hwnd, GetKeyState(VK_SHIFT) < 0))
            ::SetFocus(next);
        return TRUE;
    }
    return FALSE;
}
LRESULT MatepathPane::OnCommand(WORD, WORD id, HWND, BOOL&) {
    switch (id) {
    case Back: case Forward: {
        if (history.empty() || (id == Back ? historyIndex == 0 : historyIndex + 1 >= history.size())) break;
        const size_t next = id == Back ? historyIndex - 1 : historyIndex + 1;
        if (Navigate(history[next], false)) { historyIndex = next; RefreshItems(); }
        break;
    }
    case Up: if (!Navigate(directory.parent_path())) MessageBeep(MB_ICONWARNING); break;
    case Reload: RefreshItems(); break;
    case Go: if (!Navigate(ControlText(address))) MessageBox(L"无法打开此目录（路径不存在、无法访问或过长）。", L"Matepath", MB_ICONWARNING); break;
    case Filter: fileSpec = ControlText(pattern); if (fileSpec.empty()) fileSpec = L"*.*"; RefreshItems(); break;
    case Hidden: showHidden = !showHidden; RefreshItems(); break;
    case Properties: DirList_PropertyDlg(list, -1); break;
    case SortName: case SortSize: case SortType: case SortDate: sort = id - SortName; RefreshItems(); break;
    case Reverse: reverse = !reverse; RefreshItems(); break;
    case Follow: ::SendMessageW(owner, WM_MATEPATH_ACTION, 10, 0); break;
    }
    return 0;
}
LRESULT MatepathPane::OnNotify(UINT, WPARAM, LPARAM l, BOOL& handled) {
    auto* notification = reinterpret_cast<NMHDR*>(l);
    if (notification->hwndFrom != list) { handled = FALSE; return 0; }
    switch (notification->code) {
    case LVN_GETDISPINFOW: DirList_GetDispInfo(list, l); break;
    case LVN_DELETEITEM: DirList_DeleteItem(list, l); break;
    case NM_DBLCLK: ActivateSelection(); break;
    case LVN_BEGINDRAG: case LVN_BEGINRDRAG: DirList_DoDragDrop(list, l); RefreshItems(); break;
    default: handled = FALSE;
    }
    return 0;
}
LRESULT MatepathPane::OnContext(UINT, WPARAM source, LPARAM l, BOOL&) {
    HMENU menu = CreatePopupMenu();
    if (reinterpret_cast<HWND>(source) == list) {
        AppendMenuW(menu, MF_STRING, Properties, L"属性");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, Follow, L"定位活动文档");
    AppendMenuW(menu, MF_STRING | (showHidden ? MF_CHECKED : 0), Hidden, L"显示隐藏文件");
    const wchar_t* names[]{L"按名称排序", L"按大小排序", L"按类型排序", L"按修改时间排序"};
    for (int i = 0; i < 4; ++i) AppendMenuW(menu, MF_STRING | (sort == i ? MF_CHECKED : 0), SortName + i, names[i]);
    AppendMenuW(menu, MF_STRING | (reverse ? MF_CHECKED : 0), Reverse, L"倒序");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1001, L"停靠到左侧"); AppendMenuW(menu, MF_STRING, 1002, L"停靠到右侧");
    AppendMenuW(menu, MF_STRING, 1003, L"停靠到底部"); AppendMenuW(menu, MF_STRING, 1004, L"浮动");
    AppendMenuW(menu, MF_STRING, 1000, L"隐藏");
    POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    if (p.x == -1 && p.y == -1) { p = {10, 10}; ClientToScreen(&p); }
    const int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, m_hWnd, nullptr);
    DestroyMenu(menu);
    if (id >= 1000) ::PostMessageW(owner, WM_MATEPATH_ACTION, id - 1000, 0);
    else if (id) SendMessage(WM_COMMAND, id);
    return 0;
}
LRESULT MatepathPane::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) { Layout(); handled = FALSE; return 0; }
LRESULT MatepathPane::OnDpi(UINT message, WPARAM, LPARAM l, BOOL& handled) {
    if (message == WM_DPICHANGED && !IsDocking()) { auto* r = reinterpret_cast<RECT*>(l); SetWindowPos(nullptr, r, SWP_NOZORDER | SWP_NOACTIVATE); }
    Layout(); handled = FALSE; return 0;
}
LRESULT MatepathPane::OnClose(UINT, WPARAM, LPARAM, BOOL&) { ::PostMessageW(owner, WM_MATEPATH_ACTION, 0, 0); return 0; }
LRESULT MatepathPane::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
    if (tooltips.IsWindow()) tooltips.DestroyWindow();
    list.DeleteAllItems(); DirList_Destroy(list); handled = FALSE; return 0;
}

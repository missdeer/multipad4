#include "SimdGraphics.h"
#include "MainFrame.h"
#include "UpstreamUI.h"
#include "EditorDialogs.h"
#include "FileCommands.h"
#include "FrameCommands.h"
#include "StyleManager.h"
#include "FileDialogs.h"
#include "EditCommandRequirements.h"
#include "EditLexer.h"
#include <array>
namespace {
// Order and bitmap indices from Notepad4.cpp::tbbMainWnd.
constexpr UINT toolbarCommands[] = {
    IDM_FILE_NEW, IDM_FILE_OPEN, IDM_FILE_BROWSE, IDM_FILE_SAVE,
    IDM_EDIT_UNDO, IDM_EDIT_REDO, IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE,
    IDM_EDIT_FIND, IDM_EDIT_REPLACE, IDM_VIEW_WORDWRAP, IDM_VIEW_ZOOMIN, IDM_VIEW_ZOOMOUT,
    IDM_VIEW_SCHEME, IDM_VIEW_SCHEME_CONFIG, IDM_FILE_EXIT, IDM_FILE_SAVEAS, IDM_FILE_SAVECOPY,
    IDM_EDIT_DELETE, IDM_FILE_PRINT, IDM_FILE_OPENFAV, IDM_FILE_ADDTOFAV,
    IDM_VIEW_FOLD_ALL, IDM_FILE_LAUNCH, IDM_VIEW_ALWAYSONTOP, IDM_FILE_NEWWINDOW
};
constexpr int defaultToolbar[] = {22, 3, 0, 1, 27, 2, 0, 4, 18, 19, 0, 5, 6, 0, 7, 8, 9, 20, 0, 10, 11, 0, 12, 0, 24, 0, 13, 14, 0, 15, 16, 0, 17};
std::wstring StatusNumber(LRESULT value) {
    auto text = std::to_wstring(value);
    if (value < 1000) return text;
    wchar_t separator[4]{};
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator, _countof(separator));
    for (size_t position = text.size(); position > 3; position -= 3)
        text.insert(position - 3, 1, separator[0] ? separator[0] : L',');
    return text;
}
}
TBBUTTON UpstreamToolbarButton(int index) {
    TBBUTTON button{};
    if (index == 0) { button.fsStyle = BTNS_SEP; return button; }
    button.iBitmap = index - 1; button.idCommand = IDT_FILE_NEW + index - 1;
    button.fsState = TBSTATE_ENABLED; button.fsStyle = BTNS_BUTTON;
    if (button.idCommand == IDT_FILE_OPEN || button.idCommand == IDT_VIEW_TOGGLEFOLDS) button.fsStyle |= BTNS_DROPDOWN;
    return button;
}
namespace {
void ResetToolbar(HWND toolbar) {
    while (SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) > 0) SendMessageW(toolbar, TB_DELETEBUTTON, 0, 0);
    for (int index : defaultToolbar) {
        auto button = UpstreamToolbarButton(index);
        SendMessageW(toolbar, TB_ADDBUTTONS, 1, reinterpret_cast<LPARAM>(&button));
    }
    SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
}
void UpdateMenu(MainFrame& frame, HMENU menu) {
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        if (auto sub = GetSubMenu(menu, i)) { UpdateMenu(frame, sub); continue; }
        UINT id = GetMenuItemID(menu, i);
        if (id < 40000 || id >= 46000) continue;
        const auto state = frame.State(id);
        EnableMenuItem(menu, id, MF_BYCOMMAND | (state.enabled ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu, id, MF_BYCOMMAND | (state.checked ? MF_CHECKED : MF_UNCHECKED));
    }
}
}
void RestoreUpstreamToolbar(HWND toolbar, const std::wstring& buttons) {
    if (!buttons.empty()) {
        std::vector<TBBUTTON> restored; const wchar_t* p = buttons.c_str();
        while (*p) {
            wchar_t* end = nullptr; auto id = wcstoul(p, &end, 10); if (end == p) break; p = end;
            if (*p == L',') ++p;
            if (id && (id < IDT_FILE_NEW || id > IDT_FILE_NEWWINDOW)) continue;
            restored.push_back(UpstreamToolbarButton(id ? static_cast<int>(id - IDT_FILE_NEW + 1) : 0));
        }
        if (!restored.empty()) {
            while (::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0)) ::SendMessageW(toolbar, TB_DELETEBUTTON, 0, 0);
            ::SendMessageW(toolbar, TB_ADDBUTTONS, restored.size(), reinterpret_cast<LPARAM>(restored.data()));
        }
    }
}
UINT NormalizeCommand(UINT id) {
    if (id >= IDT_FILE_NEW && id <= IDT_FILE_NEWWINDOW) return toolbarCommands[id - IDT_FILE_NEW];
    return id;
}
void UpdateMenuCommands(MainFrame& frame, HMENU menu) { UpdateMenu(frame, menu); }
void MainFrame::CreateUpstreamUI() {
    CreateSimpleStatusBar();
    CToolBarCtrl toolbar;
    m_hWndToolBar = toolbar.Create(m_hWnd, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | CCS_TOP | CCS_NODIVIDER | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_ADJUSTABLE);
    toolbar.SetButtonStructSize();
    // BTNS_DROPDOWN needs DRAWDDARROWS to keep the main button a command
    // target and reserve TBN_DROPDOWN for its arrow, as in Notepad4.
    toolbar.SetExtendedStyle(toolbar.GetExtendedStyle() | TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS);
    int dpi = GetDpiForWindow(m_hWnd);
    int size = dpi >= 192 ? 32 : dpi >= 144 ? 24 : 16;
    UINT bitmap = size == 32 ? IDB_TOOLBAR32 : size == 24 ? IDB_TOOLBAR24 : IDB_TOOLBAR16;
    toolbarImages = ImageList_LoadImageW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(bitmap), size, 0, CLR_DEFAULT, IMAGE_BITMAP, LR_CREATEDIBSECTION);
    auto disabledBitmap = static_cast<HBITMAP>(LoadImageW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(bitmap), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (disabledBitmap) {
        if (simd::BitmapAlphaBlend(disabledBitmap, GetSysColor(COLOR_3DFACE), 0x60)) {
            toolbarDisabledImages = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 0, 0);
            if (toolbarDisabledImages) {
                ImageList_AddMasked(toolbarDisabledImages, disabledBitmap, CLR_DEFAULT);
                toolbar.SetDisabledImageList(toolbarDisabledImages);
            }
        }
        DeleteObject(disabledBitmap);
    }
    toolbar.SetImageList(toolbarImages);
    toolbar.SetBitmapSize(size, size);
    ResetToolbar(toolbar);
    HMENU file = GetSubMenu(GetMenu(), 0);
    InsertMenuW(file, 4, MF_BYPOSITION | MF_STRING, ID_SAVEALL, L"Save All\tCtrl+Alt+S");
    InsertMenuW(file, GetMenuItemCount(file) - 1, MF_BYPOSITION | MF_STRING, ID_LOAD_FOR_EDITING, L"Load for editing...");
    InsertMenuW(file, GetMenuItemCount(file) - 1, MF_BYPOSITION | MF_STRING, ID_CLOSE, L"Close Document\tCtrl+W");
    InsertMenuW(file, GetMenuItemCount(file) - 1, MF_BYPOSITION | MF_STRING, ID_CLOSEALL, L"Close All Documents");
    windowMenu = CreatePopupMenu();
    AppendMenuW(windowMenu, MF_STRING, ID_NEXTTAB, L"Next Tab\tCtrl+Tab");
    AppendMenuW(windowMenu, MF_STRING, ID_PREVTAB, L"Previous Tab\tCtrl+Shift+Tab");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, ID_WINDOW_CASCADE, L"Cascade");
    AppendMenuW(windowMenu, MF_STRING, ID_WINDOW_TILE_HORZ, L"Tile Horizontally");
    AppendMenuW(windowMenu, MF_STRING, ID_WINDOW_TILE_VERT, L"Tile Vertically");
    AppendMenuW(windowMenu, MF_STRING, ID_WINDOW_ARRANGE, L"Arrange Icons");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, ID_DOCUMENTS, L"Document Panel");
    AppendMenuW(windowMenu, MF_STRING, ID_MATEPATH, L"Matepath 文件浏览器");
    AppendMenuW(windowMenu, MF_STRING, ID_MATEPATH_FOLLOW, L"Matepath 定位活动文档");
    AppendMenuW(windowMenu, MF_STRING, ID_DOCKLEFT, L"Dock Left");
    AppendMenuW(windowMenu, MF_STRING, ID_DOCKRIGHT, L"Dock Right");
    AppendMenuW(windowMenu, MF_STRING, ID_DOCKBOTTOM, L"Dock Bottom");
    AppendMenuW(windowMenu, MF_STRING, ID_FLOAT, L"Float Panel");
    InsertMenuW(GetMenu(), GetMenuItemCount(GetMenu()) - 1, MF_BYPOSITION | MF_POPUP,
        reinterpret_cast<UINT_PTR>(windowMenu), L"&Window");
    mdiAccelerators = LoadAcceleratorsW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDR_MDIACCEL));
}
CommandState MainFrame::State(UINT input) {
    return ConstrainUpstreamEditorState(Active(), NormalizeCommand(input), QueryState(input));
}
CommandState MainFrame::QueryState(UINT input) {
    const UINT id = NormalizeCommand(input);
    auto* d = Active();
    if (id == ID_LOAD_FOR_EDITING) return {true, d && d->FastView(), false};
    if (d && d->FastView()) {
        if (id == IDM_FILE_READONLY_MODE) return {true, false, true};
        if (EditorCommandState(d, id).supported || StyleCommandState(d, id).supported) {
            if (id != IDM_EDIT_COPY && id != IDM_VIEW_ZOOMIN && id != IDM_VIEW_ZOOMOUT && id != IDM_VIEW_RESETZOOM)
                return {true, false, false};
        }
        switch (id) {
        case ID_SAVE: case ID_SAVEAS: case ID_FIND: case ID_FINDNEXT: case IDM_EDIT_FINDPREV:
        case IDM_EDIT_REPLACE: case IDM_EDIT_GOTOLINE: case ID_SEARCH_MARK:
        case IDM_FILE_PRINT: case IDM_FILE_SAVEBACKUP: case IDM_FILE_LARGE_FILE_MODE: case IDM_FILE_LARGE_FILE_MODE_RELOAD:
            return {true, false, false};
        }
    }
    const auto file = FileCommandState(*this, id);
    if (file.supported) return file;
    const auto frame = FrameCommandState(*this, id);
    if (frame.supported) return frame;
    const auto style = StyleCommandState(d, id);
    if (style.supported) return style;
    const auto editor = EditorCommandState(d, id);
    if (editor.supported) return editor;
    switch (id) {
    case ID_NEW: case ID_OPEN: case ID_EXIT: case ID_ABOUT: case IDM_FILE_NEWWINDOW:
    case ID_DOCKLEFT: case ID_DOCKRIGHT: case ID_DOCKBOTTOM: case ID_FLOAT:
    case IDM_VIEW_CUSTOMIZE_TOOLBAR: return {true, true, false};
    case ID_SEARCH_RESULTS: return {true, true, searchResults.visible};
    case ID_FIND: case IDM_EDIT_REPLACE: return {true, true, false};
    case ID_SEARCH_FILES: case ID_SEARCH_MARK: return {true, d != nullptr, false};
    case ID_DOCUMENTS: return {true, true, dock.visible};
    case ID_MATEPATH: return {true, true, matepath.visible};
    case ID_MATEPATH_FOLLOW: return {true, Active() && !Active()->path.empty(), false};
    case IDM_VIEW_TOOLBAR: return {true, true, toolbarVisible};
    case IDM_VIEW_STATUSBAR: return {true, true, statusVisible};
    case IDM_VIEW_ALWAYSONTOP: return {true, true, (GetExStyle() & WS_EX_TOPMOST) != 0};
    case ID_SAVEALL: case ID_CLOSEALL: case ID_NEXTTAB: case ID_PREVTAB: return {true, !documents.empty(), false};
    case IDM_FILE_REVERT: return {true, d && !d->path.empty(), false};
    case ID_SAVE: case ID_SAVEAS: case IDM_FILE_SAVECOPY: case ID_CLOSE: case ID_FINDNEXT:
    case IDM_EDIT_FINDPREV: case IDM_EDIT_GOTOLINE:
    case IDM_VIEW_ZOOM_LEVEL: case IDM_VIEW_LONGLINESETTINGS:
        return {true, d != nullptr, false};
    default: return {};
    }
}
void MainFrame::UpdateCommandUI() {
    UpdateMenu(*this, runtime.mainMenu ? runtime.mainMenu : GetMenu());
    for (UINT id = IDT_FILE_NEW; id <= IDT_FILE_NEWWINDOW; ++id) {
        auto state = State(id);
        ::SendMessageW(m_hWndToolBar, TB_ENABLEBUTTON, id, state.enabled);
        ::SendMessageW(m_hWndToolBar, TB_CHECKBUTTON, id, state.checked);
    }
}
void MainFrame::UpdateStatus() {
    if (!m_hWndStatusBar) return;
    // Stable upstream StatusItem order, including the flexible empty part.
    std::array<std::wstring, 13> text{};
    auto* d = Active();
    if (d) {
        if (d->Send(SCI_GETMARGINWIDTHN, 0)) {
            const auto number = "__" + std::to_string(d->Send(SCI_GETLINECOUNT));
            d->Send(SCI_SETMARGINWIDTHN, 0, d->Send(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<LPARAM>(number.c_str())));
        }
        auto pos = d->Send(SCI_GETCURRENTPOS), line = d->Send(SCI_LINEFROMPOSITION, pos);
        auto lineStart = d->Send(SCI_POSITIONFROMLINE, line), lineEnd = d->Send(SCI_GETLINEENDPOSITION, line);
        text[0] = L"Ln " + std::to_wstring(line + 1) + L" / " + std::to_wstring(d->Send(SCI_GETLINECOUNT));
        text[1] = L"Col " + std::to_wstring(d->Send(SCI_GETCOLUMN, pos) + 1) + L" / " + std::to_wstring(d->Send(SCI_GETCOLUMN, lineEnd));
        text[2] = L"Ch " + std::to_wstring(d->Send(SCI_COUNTCHARACTERS, lineStart, pos) + 1)
            + L" / " + std::to_wstring(d->Send(SCI_COUNTCHARACTERS, lineStart, lineEnd));
        LRESULT selectedCharacters = 0, selectedBytes = 0, selectedLines = 0;
        for (LRESULT i = 0; i < d->Send(SCI_GETSELECTIONS); ++i) {
            auto a = d->Send(SCI_GETSELECTIONNSTART, i), b = d->Send(SCI_GETSELECTIONNEND, i);
            selectedCharacters += d->Send(SCI_COUNTCHARACTERS, a, b); selectedBytes += b - a;
            if (b > a) selectedLines += d->Send(SCI_LINEFROMPOSITION, d->Send(SCI_POSITIONBEFORE, b)) - d->Send(SCI_LINEFROMPOSITION, a) + 1;
        }
        const bool rectangular = d->Send(SCI_SELECTIONISRECTANGLE) != 0;
        const auto selectionCharacters = rectangular ? L"--" : StatusNumber(selectedCharacters);
        const auto selectionBytes = rectangular ? L"--" : StatusNumber(selectedBytes);
        text[3] = L"Sel " + selectionCharacters + L" / " + selectionBytes;
        text[4] = L"SelLn " + std::to_wstring(selectedLines);
        text[5] = L"Fnd " + std::to_wstring(d->style.markOccurrences ? d->style.occurrences : d->search.matches)
            + (d->style.markOccurrences && d->style.occurrencePending ? L" ..." : L"");
        // Reuse the upstream localized six-field format instead of hard-coding
        // English prefixes when the rest of the interface uses another locale.
        wchar_t format[512]{}, formatted[1024]{};
        std::array<std::wstring, 10> values{
            StatusNumber(line + 1), StatusNumber(d->Send(SCI_GETLINECOUNT)),
            StatusNumber(d->Send(SCI_GETCOLUMN, pos) + 1), StatusNumber(d->Send(SCI_GETCOLUMN, lineEnd)),
            StatusNumber(d->Send(SCI_COUNTCHARACTERS, lineStart, pos) + 1), StatusNumber(d->Send(SCI_COUNTCHARACTERS, lineStart, lineEnd)),
            selectionCharacters, selectionBytes, StatusNumber(selectedLines),
            StatusNumber(d->style.markOccurrences ? d->style.occurrences : d->search.matches) + (d->style.markOccurrences && d->style.occurrencePending ? L" ..." : L"")};
        if (LoadStringW(_Module.GetModuleInstance(), IDS_STATUSITEM_FORMAT, format, _countof(format)) &&
            _snwprintf_s(formatted, _countof(formatted), _TRUNCATE, format, values[0].c_str(), values[1].c_str(), values[2].c_str(), values[3].c_str(),
                values[4].c_str(), values[5].c_str(), values[6].c_str(), values[7].c_str(), values[8].c_str(), values[9].c_str()) >= 0) {
            const wchar_t* start = formatted;
            for (size_t field = 0; field < 6; ++field) {
                auto end = wcschr(start, L'\n');
                text[field] = end ? std::wstring(start, end) : std::wstring(start);
                if (!end) break; start = end + 1;
            }
        }
        text[7] = d->LargeFile() ? L"Large file (AutoSave off)" : d->language; text[8] = files::EncodingName(d->encoding);
        auto eol = d->Send(SCI_GETEOLMODE); text[9] = eol == SC_EOL_CRLF ? L"CR+LF" : eol == SC_EOL_LF ? L"LF" : L"CR";
        text[10] = d->Send(SCI_GETOVERTYPE) ? L"OVR" : L"INS";
        text[11] = std::to_wstring(d->Send(SCI_GETZOOM)) + L"%";
        wchar_t size[64]{};
        StrFormatByteSizeW(d->Send(SCI_GETLENGTH), size, _countof(size));
        text[12] = size;
        if (d->FastView()) {
            auto& view = *d->fastView;
            for (size_t i = 0; i < 6; ++i) text[i].clear();
            text[0] = L"Bytes " + std::to_wstring(view.Begin()) + L"–" + std::to_wstring(view.End());
            text[7] = L"Fast View"; text[10] = L"Read only";
            text[12] = std::to_wstring(view.Size() / (1024 * 1024)) + L" MiB";
        }
    }
    RECT client{}; GetClientRect(&client);
    std::array<int, 13> widths{}, edges{};
    HDC dc = ::GetDC(m_hWndStatusBar);
    HFONT font = reinterpret_cast<HFONT>(::SendMessageW(m_hWndStatusBar, WM_GETFONT, 0, 0));
    auto oldFont = font ? SelectObject(dc, font) : nullptr;
    int total = 0, padding = MulDiv(16, GetDpiForWindow(m_hWnd), 96);
    for (size_t i = 0; i < text.size(); ++i) {
        SIZE size{}; GetTextExtentPoint32W(dc, text[i].c_str(), static_cast<int>(text[i].size()), &size);
        widths[i] = size.cx + padding; if (i != 6) total += widths[i];
    }
    if (oldFont) SelectObject(dc, oldFont); ::ReleaseDC(m_hWndStatusBar, dc);
    widths[6] = max(0L, client.right - total - padding);
    for (size_t i = 0; i < edges.size(); ++i) edges[i] = widths[i] + (i ? edges[i - 1] : 0);
    edges.back() = -1;
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
    for (size_t i = 0; i < text.size(); ++i)
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, i, reinterpret_cast<LPARAM>(text[i].c_str()));
}
LRESULT MainFrame::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) {
    if (IsIconic() && settings.Get(IDM_VIEW_MINTOTRAY)) { SetTrayVisible(*this, true); ShowWindow(SW_HIDE); }
    UpdateStatus(); handled = FALSE; return 0;
}
LRESULT MainFrame::OnNotify(UINT, WPARAM, LPARAM l, BOOL& handled) {
    FrameOperation operation(runtime, m_hWnd);
    auto* notification = reinterpret_cast<NMHDR*>(l);
    if (notification->hwndFrom == m_hWndToolBar) {
        auto* info = reinterpret_cast<NMTOOLBARW*>(l);
        switch (notification->code) {
        case NM_CUSTOMDRAW: {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(l);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                RECT client{}; ::GetClientRect(m_hWndToolBar, &client);
                FillRect(draw->hdc, &client, GetSysColorBrush(COLOR_BTNFACE));
            }
            return CDRF_DODEFAULT;
        }
        case TBN_QUERYINSERT: case TBN_QUERYDELETE: return TRUE;
        case TBN_GETBUTTONINFOW:
            if (info->iItem < 0 || info->iItem >= 28) return FALSE;
            info->tbButton = UpstreamToolbarButton(info->iItem);
            LoadStringW(_Module.GetModuleInstance(), info->tbButton.idCommand, info->pszText, info->cchText);
            return TRUE;
        case TBN_RESET: ResetToolbar(m_hWndToolBar); UpdateCommandUI(); UpdateLayout(); return 0;
        case TBN_ENDADJUST: UpdateCommandUI(); UpdateLayout(); SaveFramePreferences(*this); return 0;
        case TBN_DROPDOWN: {
            HMENU root = nullptr, menu = nullptr;
            if (info->iItem == IDT_FILE_OPEN) {
                root = menu = CreatePopupMenu();
                for (UINT i = 0; i < 32; ++i) {
                    auto path = settings.ReadString(L"Recent Files", std::to_wstring(i).c_str());
                    if (!path.empty()) AppendMenuW(menu, MF_STRING, IDM_RECENT_HISTORY_START + i, path.c_str());
                }
                if (!GetMenuItemCount(menu)) { DestroyMenu(root); return TBDDRET_TREATPRESSED; }
            } else {
                root = LoadMenuW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDR_POPUPMENU)); menu = GetSubMenu(root, 3);
            }
            for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                UINT id = GetMenuItemID(menu, i); auto state = State(id);
                if (id && id != static_cast<UINT>(-1)) EnableMenuItem(menu, id, MF_BYCOMMAND | (state.enabled ? MF_ENABLED : MF_GRAYED));
            }
            RECT button{}; ::SendMessageW(m_hWndToolBar, TB_GETRECT, info->iItem, reinterpret_cast<LPARAM>(&button));
            ::MapWindowPoints(m_hWndToolBar, nullptr, reinterpret_cast<POINT*>(&button), 2);
            UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, button.left, button.bottom, 0, m_hWnd, nullptr);
            DestroyMenu(root); if (command) SendMessage(WM_COMMAND, command); return TBDDRET_DEFAULT;
        }
        }
    }
    if (notification->code == TTN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMTTDISPINFOW*>(l);
        LoadStringW(_Module.GetModuleInstance(), static_cast<UINT>(info->hdr.idFrom), info->szText, _countof(info->szText)); return 0;
    }
    if (notification->hwndFrom == m_hWndStatusBar && notification->code == NM_CLICK) {
        auto* mouse = reinterpret_cast<NMMOUSE*>(l); auto* d = Active(); if (!d) return 0;
        if (mouse->dwItemSpec == 11) { ShowEditorDialog(m_hWnd, *d, IDD_ZOOMLEVEL, true); Refresh(); return 0; }
        if (mouse->dwItemSpec == 9 && !d->Send(SCI_GETREADONLY)) {
            d->Send(SCI_BEGINUNDOACTION); d->Send(SCI_CONVERTEOLS, d->Send(SCI_GETEOLMODE)); d->Send(SCI_ENDUNDOACTION); Refresh(); return 0;
        }
    }
    if (notification->hwndFrom == m_hWndStatusBar && notification->code == NM_DBLCLK) {
        auto* mouse = reinterpret_cast<NMMOUSE*>(l); auto* d = Active(); if (!d) return 0;
        switch (mouse->dwItemSpec) {
        case 0: ShowEditorDialog(m_hWnd, *d, IDD_LINENUM); break;
        case 5: ShowSearch(false); break;
        case 7: SendMessage(WM_COMMAND, GetDocumentLexer(*d)->iLexer == SCLEX_CSV ? IDM_LEXER_CSV : IDM_VIEW_SCHEME); break;
        case 8: SendMessage(WM_COMMAND, IDM_ENCODING_SELECT); break;
        case 9: {
            int eol = static_cast<int>(d->Send(SCI_GETEOLMODE));
            SendMessage(WM_COMMAND, eol == SC_EOL_CRLF ? IDM_LINEENDINGS_LF : eol == SC_EOL_LF ? IDM_LINEENDINGS_CR : IDM_LINEENDINGS_CRLF); break;
        }
        case 10: d->Send(SCI_EDITTOGGLEOVERTYPE); break;
        }
        Refresh(); return 0;
    }
    handled = FALSE; return 0;
}

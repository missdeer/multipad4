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
#include <algorithm>
#include <cwchar>
#include <string_view>
#ifdef MULTIPAD4_TESTING
#include "../tests/StatusPresentationBaseline.h"
StatusMeasurementCounters statusMeasurementCounters;
bool statusPresentationBaseline = false;
#endif
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
std::wstring StatusNumber(LRESULT value, wchar_t separator) {
    auto text = std::to_wstring(value);
    if (value < 1000) return text;
    for (size_t position = text.size(); position > 3; position -= 3)
        text.insert(position - 3, 1, separator);
    return text;
}
LRESULT CALLBACK StatusWindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
    auto& frame = *reinterpret_cast<MainFrame*>(data);
    if (message == SB_SETTEXTW || message == SB_SETTEXTA) {
        const auto part = static_cast<unsigned>(w & 255);
        if (part < frame.statusText.size() && (message == SB_SETTEXTA || (w & ~WPARAM(255)) ||
            std::wstring_view(l ? reinterpret_cast<const wchar_t*>(l) : L"") != frame.statusText[part]))
            frame.statusInvalid |= 1u << part;
    }
    if (message == SB_SETPARTS && (w != frame.statusEdges.size() || !l ||
        !std::equal(frame.statusEdges.begin(), frame.statusEdges.end(), reinterpret_cast<const int*>(l))))
        frame.statusPartsInvalid = true;
    if (message == WM_SETFONT || message == WM_THEMECHANGED) {
        frame.statusMeasureInvalid = true;
        PostMessageW(frame.m_hWnd, WM_DOCUMENT_CHANGED, 1, 0);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, StatusWindowProcedure, id);
    return DefSubclassProc(window, message, w, l);
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
        const auto native = GetMenuState(menu, id, MF_BYCOMMAND);
        if ((((native & (MF_DISABLED | MF_GRAYED)) == 0) != state.enabled) || (!state.enabled && !(native & MF_GRAYED)))
            EnableMenuItem(menu, id, MF_BYCOMMAND | (state.enabled ? MF_ENABLED : MF_GRAYED));
        if (((native & MF_CHECKED) != 0) != state.checked)
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
    const auto instance = GetModuleHandleW(nullptr);
    m_hWndStatusBar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, m_hWnd, nullptr, instance, nullptr);
    statusObserved = m_hWndStatusBar && SetWindowSubclass(m_hWndStatusBar, StatusWindowProcedure, 0x46, reinterpret_cast<DWORD_PTR>(this));
    const HWND toolbar = m_hWndToolBar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | CCS_TOP | CCS_NODIVIDER | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_ADJUSTABLE,
        0, 0, 0, 0, m_hWnd, nullptr, instance, nullptr);
    ::SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    // BTNS_DROPDOWN needs DRAWDDARROWS to keep the main button a command
    // target and reserve TBN_DROPDOWN for its arrow, as in Notepad4.
    ::SendMessageW(toolbar, TB_SETEXTENDEDSTYLE, 0,
        ::SendMessageW(toolbar, TB_GETEXTENDEDSTYLE, 0, 0) | TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS);
    int dpi = GetDpiForWindow(m_hWnd);
    int size = dpi >= 192 ? 32 : dpi >= 144 ? 24 : 16;
    UINT bitmap = size == 32 ? IDB_TOOLBAR32 : size == 24 ? IDB_TOOLBAR24 : IDB_TOOLBAR16;
    toolbarImages = ImageList_LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(bitmap), size, 0, CLR_DEFAULT, IMAGE_BITMAP, LR_CREATEDIBSECTION);
    auto disabledBitmap = static_cast<HBITMAP>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(bitmap), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (disabledBitmap) {
        if (simd::BitmapAlphaBlend(disabledBitmap, GetSysColor(COLOR_3DFACE), 0x60)) {
            toolbarDisabledImages = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 0, 0);
            if (toolbarDisabledImages) {
                ImageList_AddMasked(toolbarDisabledImages, disabledBitmap, CLR_DEFAULT);
                ::SendMessageW(toolbar, TB_SETDISABLEDIMAGELIST, 0, reinterpret_cast<LPARAM>(toolbarDisabledImages));
            }
        }
        DeleteObject(disabledBitmap);
    }
    ::SendMessageW(toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(toolbarImages));
    ::SendMessageW(toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(size, size));
    ResetToolbar(toolbar);
    HMENU file = GetSubMenu(GetMenu(), 0);
    InsertMenuW(file, 4, MF_BYPOSITION | MF_STRING, ID_SAVEALL, L"Save All\tCtrl+Alt+S");
    InsertMenuW(file, GetMenuItemCount(file) - 1, MF_BYPOSITION | MF_STRING, ID_LOAD_FOR_EDITING, L"Open in full editor...");
    InsertMenuW(file, GetMenuItemCount(file) - 1, MF_BYPOSITION | MF_STRING, ID_CLOSE, L"Close Document\tCtrl+W");
    InsertMenuW(file, GetMenuItemCount(file) - 1, MF_BYPOSITION | MF_STRING, ID_CLOSEALL, L"Close All Documents");
    const bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
    if (appUpdater.enabled) {
        InsertMenuW(GetMenu(), IDM_HELP_LATEST_RELEASE, MF_BYCOMMAND | MF_STRING,
            ID_CHECK_UPDATES, chinese ? L"检查更新(&U)..." : L"Check for &Updates...");
        InsertMenuW(GetMenu(), IDM_HELP_LATEST_RELEASE, MF_BYCOMMAND | MF_STRING,
            ID_AUTO_CHECK_UPDATES, chinese ? L"自动检查更新" : L"Check for Updates Automatically");
    }
    InsertMenuW(GetMenu(), IDM_VIEW_SAVESETTINGS, MF_BYCOMMAND | MF_STRING,
        ID_RESTORE_EXIT_SESSION, chinese ? L"启动时恢复上次会话" : L"Restore Previous Session on Startup");
    InsertMenuW(GetMenu(), IDM_VIEW_SAVESETTINGS, MF_BYCOMMAND | MF_STRING,
        ID_FILE_ASSOCIATIONS, chinese ? L"文件关联(&F)..." : L"&File Associations...");
    InsertMenuW(GetMenu(), IDM_VIEW_SAVESETTINGS, MF_BYCOMMAND | MF_STRING,
        ID_TAB_SETTINGS, chinese ? L"文档标签设置(&D)..." : L"&Document Tab Settings...");
    mdiAccelerators = LoadAcceleratorsW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_MDIACCEL));
}
CommandState MainFrame::State(UINT input) {
    return ConstrainUpstreamEditorState(Active(), NormalizeCommand(input), QueryState(input));
}
CommandState MainFrame::QueryState(UINT input) {
    const UINT id = NormalizeCommand(input);
    auto* d = Active();
    if (id == ID_LOAD_FOR_EDITING) return {true, d && d->FastView(), false};
    if (d && d->FastView()) {
        if (id == ID_SEARCH_MARK) return {true, true, false};
        if (id == IDM_FILE_READONLY_MODE) return {true, true, d->fastView->ReadOnly()};
        if (id == IDM_EDIT_UNDO) return {true, !d->fastView->ReadOnly() && d->fastView->CanUndo(), false};
        if (id == IDM_EDIT_REDO) return {true, !d->fastView->ReadOnly() && d->fastView->CanRedo(), false};
        if (EditorCommandState(d, id).supported || StyleCommandState(d, id).supported) {
            if (id != ID_FIND && id != ID_FINDNEXT && id != IDM_EDIT_FINDPREV && id != IDM_EDIT_REPLACE && id != IDM_EDIT_COPY && id != IDM_EDIT_CUT && id != IDM_EDIT_PASTE && id != IDM_EDIT_DELETE &&
                id != IDM_VIEW_ZOOMIN && id != IDM_VIEW_ZOOMOUT && id != IDM_VIEW_RESETZOOM && id != IDM_EDIT_GOTOLINE)
                return {true, false, false};
        }
        switch (id) {
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
    case IDM_VIEW_CUSTOMIZE_TOOLBAR: return {true, true, false};
    case ID_CHECK_UPDATES: return {appUpdater.enabled, appUpdater.enabled && (!appUpdater.Busy() || (appUpdater.check.valid() && !appUpdater.manual)), false};
    case ID_AUTO_CHECK_UPDATES: return {appUpdater.enabled, appUpdater.enabled, settings.Get(ID_AUTO_CHECK_UPDATES, 1) != 0};
    case ID_SEARCH_RESULTS: return {true, true, searchResults.visible};
    case ID_FIND: case IDM_EDIT_REPLACE: return {true, true, false};
    case ID_SEARCH_FILES: case ID_SEARCH_MARK: return {true, d != nullptr, false};
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
    for (UINT id = IDT_FILE_NEW; id <= IDT_FILE_NEWWINDOW; ++id) {
        auto state = State(id);
        auto value = std::make_pair(state.enabled, state.checked);
        auto found = toolbarState.find(id);
        if (found == toolbarState.end() || found->second != value) {
            ::SendMessageW(m_hWndToolBar, TB_ENABLEBUTTON, id, state.enabled);
            ::SendMessageW(m_hWndToolBar, TB_CHECKBUTTON, id, state.checked);
            toolbarState[id] = value;
        }
    }
}
void MainFrame::UpdateStatus() {
#ifdef MULTIPAD4_TESTING
    if (statusPresentationBaseline) { BaselineStatusPresentation(*this); return; }
#endif
    if (!m_hWndStatusBar || !statusVisible) { KillTimer(2); return; }
    constexpr unsigned allParts = (1u << 13) - 1;
    if (!statusObserved) { statusInvalid = allParts; statusPartsInvalid = true; }
    unsigned textChanged = 0;
    auto setText = [&](size_t part, std::wstring_view value) {
        if (statusText[part] != value) { statusText[part].assign(value); textChanged |= 1u << part; }
    };
    auto changed = [&](size_t part, std::array<LRESULT, 6> value) {
        const auto bit = 1u << part;
        if (!(statusValuesValid & bit) || statusValues[part] != value) {
            statusValues[part] = value; statusValuesValid |= bit; return true;
        }
        return false;
    };
    if (!statusLocaleValid) {
        wchar_t separator[4]{};
#ifdef MULTIPAD4_TESTING
        ++statusMeasurementCounters.localeQueries;
#endif
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator, _countof(separator));
        statusSeparator = separator[0] ? separator[0] : L','; statusLocaleValid = true; statusValuesValid = 0;
    }
    const auto language = GetThreadUILanguage();
    if (!statusFormatValid || statusLanguage != language) {
        wchar_t format[512]{};
#ifdef MULTIPAD4_TESTING
        ++statusMeasurementCounters.formatLoads;
#endif
        LoadStringW(GetModuleHandleW(nullptr), IDS_STATUSITEM_FORMAT, format, _countof(format));
        statusFormat = format; statusLanguage = language; statusFormatValid = true; statusValuesValid = 0;
    }
    auto number = [&](LRESULT value) { return StatusNumber(value, statusSeparator); };
    auto* d = Active();
    const auto identity = d ? d->searchIdentity : 0;
    const bool mapped = d && d->FastView();
    if (statusIdentity != identity || statusMapped != mapped) {
        statusIdentity = identity; statusMapped = mapped; statusValuesValid = 0;
    }
    if (!d) {
        KillTimer(2); statusValuesValid = 0;
        for (size_t i = 0; i < statusText.size(); ++i) setText(i, L"");
    }
    if (d) {
        const auto margin = d->Send(SCI_GETMARGINWIDTHN, 0);
        const auto lineCount = !mapped || margin ? d->Send(SCI_GETLINECOUNT) : 0;
        const auto zoom = d->Send(SCI_GETZOOM);
        if (margin) {
            auto fontString = [&](UINT message, WPARAM parameter) {
                const auto length = (std::max)(LRESULT(0), d->Send(message, parameter));
                std::string value(static_cast<size_t>(length) + 1, '\0');
                d->Send(message, parameter, reinterpret_cast<LPARAM>(value.data())); value.resize(static_cast<size_t>(length)); return value;
            };
            auto font = fontString(SCI_STYLEGETFONT, STYLE_LINENUMBER), locale = fontString(SCI_GETFONTLOCALE, 0);
            const std::array<LRESULT, 9> metrics{d->Send(SCI_STYLEGETSIZEFRACTIONAL, STYLE_LINENUMBER),
                d->Send(SCI_STYLEGETWEIGHT, STYLE_LINENUMBER), d->Send(SCI_STYLEGETITALIC, STYLE_LINENUMBER),
                d->Send(SCI_STYLEGETSTRETCH, STYLE_LINENUMBER), d->Send(SCI_STYLEGETCHARACTERSET, STYLE_LINENUMBER),
                d->Send(SCI_GETFONTQUALITY), d->Send(SCI_GETTECHNOLOGY), zoom, GetDpiForWindow(d->hwnd)};
            if (!lineNumberValid || lineNumberIdentity != identity || lineNumberCount != lineCount ||
                lineNumberMetrics != metrics || lineNumberFont != font || lineNumberLocale != locale) {
                const auto text = "__" + std::to_string(lineCount);
#ifdef MULTIPAD4_TESTING
                ++statusMeasurementCounters.margins;
#endif
                lineNumberWidth = d->Send(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<LPARAM>(text.c_str()));
                lineNumberIdentity = identity; lineNumberCount = lineCount; lineNumberMetrics = metrics;
                lineNumberFont = std::move(font); lineNumberLocale = std::move(locale); lineNumberValid = true;
            }
            if (margin != lineNumberWidth) d->Send(SCI_SETMARGINWIDTHN, 0, lineNumberWidth);
        }
        if (!mapped) {
            auto pos = d->Send(SCI_GETCURRENTPOS), line = d->Send(SCI_LINEFROMPOSITION, pos);
            auto& stats = d->statusStatistics;
            bool pending = stats.Update(*d);
            if (pending) SetTimer(2, 30); else KillTimer(2);
            const bool prefixReady = stats.Ready(0), lineReady = stats.Ready(1), selectionReady = stats.SelectionReady();
            const bool rectangular = d->Send(SCI_SELECTIONISRECTANGLE) != 0;
            const auto selectedLines = selectionReady ? stats.selectedLines : 0;
            const auto matches = static_cast<LRESULT>(d->style.markOccurrences ? d->style.occurrences : d->search.matches);
            const bool occurrencePending = d->style.markOccurrences && d->style.occurrencePending;
            unsigned fields = 0; auto& values = statusArguments;
            if (changed(0, {line, lineCount})) { fields |= 1; values[0] = number(line + 1); values[1] = number(lineCount); }
            if (changed(1, {prefixReady ? stats.Column(0) : 0, lineReady ? stats.Column(1) : 0, prefixReady, lineReady})) {
                fields |= 2; values[2] = prefixReady ? number(stats.Column(0) + 1) : L"..."; values[3] = lineReady ? number(stats.Column(1)) : L"...";
            }
            if (changed(2, {prefixReady ? stats.Characters(0) : 0, lineReady ? stats.Characters(1) : 0, prefixReady, lineReady})) {
                fields |= 4; values[4] = prefixReady ? number(stats.Characters(0) + 1) : L"..."; values[5] = lineReady ? number(stats.Characters(1)) : L"...";
            }
            if (changed(3, {selectionReady && !rectangular ? stats.SelectedCharacters() : 0, selectionReady && !rectangular ? stats.selectedBytes : 0, rectangular, selectionReady})) {
                fields |= 8; values[6] = rectangular ? L"--" : selectionReady ? number(stats.SelectedCharacters()) : L"...";
                values[7] = rectangular ? L"--" : selectionReady ? number(stats.selectedBytes) : L"...";
            }
            if (changed(4, {selectedLines, selectionReady})) { fields |= 16; values[8] = selectionReady ? number(selectedLines) : L"..."; }
            if (changed(5, {matches, occurrencePending})) { fields |= 32; values[9] = number(matches) + (occurrencePending ? L" ..." : L""); }
            if (fields) {
                wchar_t formatted[1024]{}; size_t rendered = 0;
                if (!statusFormat.empty() && _snwprintf_s(formatted, _countof(formatted), _TRUNCATE, statusFormat.c_str(), values[0].c_str(), values[1].c_str(), values[2].c_str(), values[3].c_str(),
                    values[4].c_str(), values[5].c_str(), values[6].c_str(), values[7].c_str(), values[8].c_str(), values[9].c_str()) >= 0) {
                    const wchar_t* start = formatted;
                    for (; rendered < 6;) {
                        const auto end = wcschr(start, L'\n');
                        setText(rendered++, std::wstring_view(start, end ? static_cast<size_t>(end - start) : wcslen(start)));
                        if (!end) break; start = end + 1;
                    }
                }
                for (size_t field = rendered; field < 6; ++field) if (fields & (1u << field)) {
                    switch (field) {
                    case 0: setText(0, L"Ln " + std::to_wstring(line + 1) + L" / " + std::to_wstring(lineCount)); break;
                    case 1: setText(1, L"Col " + values[2] + L" / " + values[3]); break;
                    case 2: setText(2, L"Ch " + values[4] + L" / " + values[5]); break;
                    case 3: setText(3, L"Sel " + values[6] + L" / " + values[7]); break;
                    case 4: setText(4, L"SelLn " + (selectionReady ? std::to_wstring(selectedLines) : L"...")); break;
                    case 5: setText(5, L"Fnd " + std::to_wstring(matches) + (occurrencePending ? L" ..." : L"")); break;
                    }
                }
            }
        } else KillTimer(2);
        if (mapped) setText(7, L"Mapped document");
        else if (d->LargeFile()) setText(7, L"Large file (AutoSave off)");
        else setText(7, d->language);
        if (changed(8, {static_cast<LRESULT>(d->encoding)})) setText(8, files::EncodingName(d->encoding));
        const auto eol = d->Send(SCI_GETEOLMODE);
        if (changed(9, {eol})) setText(9, eol == SC_EOL_CRLF ? L"CR+LF" : eol == SC_EOL_LF ? L"LF" : L"CR");
        if (changed(11, {zoom})) setText(11, std::to_wstring(zoom) + L"%");
        if (mapped) {
            auto& view = *d->fastView;
            for (size_t i = 1; i < 6; ++i) setText(i, L"");
            if (changed(0, {static_cast<LRESULT>(view.Begin()), static_cast<LRESULT>(view.End())})) setText(0, L"Bytes " + std::to_wstring(view.Begin()) + L"–" + std::to_wstring(view.End()));
            if (changed(10, {view.ReadOnly()})) setText(10, view.ReadOnly() ? L"Read only" : L"Editable");
            const auto size = view.Size() / (1024 * 1024);
            if (changed(12, {static_cast<LRESULT>(size)})) setText(12, std::to_wstring(size) + L" MiB");
        } else {
            const auto overtype = d->Send(SCI_GETOVERTYPE), length = d->Send(SCI_GETLENGTH);
            if (changed(10, {overtype})) setText(10, overtype ? L"OVR" : L"INS");
            if (changed(12, {length})) { wchar_t size[64]{}; StrFormatByteSizeW(length, size, _countof(size)); setText(12, size); }
        }
    }
    RECT client{}; GetClientRect(&client);
    const auto dpi = GetDpiForWindow(m_hWnd);
    const auto font = reinterpret_cast<HFONT>(::SendMessageW(m_hWndStatusBar, WM_GETFONT, 0, 0));
    const bool measureAll = statusMeasureInvalid || dpi != statusDpi || font != statusFont;
    if (measureAll || textChanged) {
        HDC dc = ::GetDC(m_hWndStatusBar);
        if (dc) {
            const auto oldFont = font ? SelectObject(dc, font) : nullptr; bool measured = true;
            for (size_t i = 0; i < statusText.size(); ++i) if (measureAll || (textChanged & (1u << i))) {
                SIZE size{};
                if (!statusText[i].empty()) {
#ifdef MULTIPAD4_TESTING
                    ++statusMeasurementCounters.extents;
#endif
                    if (!GetTextExtentPoint32W(dc, statusText[i].c_str(), static_cast<int>(statusText[i].size()), &size)) measured = false;
                }
                statusExtents[i] = size.cx;
            }
            if (oldFont) SelectObject(dc, oldFont); ::ReleaseDC(m_hWndStatusBar, dc);
            statusMeasureInvalid = !measured;
        } else statusMeasureInvalid = true;
    }
    if (measureAll || textChanged || client.right != statusWidth || statusPartsInvalid) {
        std::array<int, 13> widths{}, edges{}; int total = 0;
        const auto padding = MulDiv(16, dpi, 96);
        for (size_t i = 0; i < widths.size(); ++i) { widths[i] = statusExtents[i] + padding; if (i != 6) total += widths[i]; }
        widths[6] = (std::max)(0L, client.right - total - padding);
        for (size_t i = 0; i < edges.size(); ++i) edges[i] = widths[i] + (i ? edges[i - 1] : 0);
        edges.back() = -1;
        if (statusPartsInvalid || edges != statusEdges) {
            statusEdges = edges; statusPartsInvalid = false;
            if (!::SendMessageW(m_hWndStatusBar, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()))) statusPartsInvalid = true;
        }
    }
    statusWidth = client.right; statusDpi = dpi; statusFont = font;
    const auto publish = statusInvalid | textChanged;
    for (size_t i = 0; i < statusText.size(); ++i) if (publish & (1u << i)) {
        statusInvalid &= ~(1u << i);
        if (!::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, i, reinterpret_cast<LPARAM>(statusText[i].c_str()))) statusInvalid |= 1u << i;
    }
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
            LoadStringW(GetModuleHandleW(nullptr), info->tbButton.idCommand, info->pszText, info->cchText);
            return TRUE;
        case TBN_RESET: ResetToolbar(m_hWndToolBar); toolbarState.clear(); UpdateCommandUI(); UpdateLayout(); return 0;
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
                root = LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_POPUPMENU)); menu = GetSubMenu(root, 3);
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
        LoadStringW(GetModuleHandleW(nullptr), static_cast<UINT>(info->hdr.idFrom), info->szText, _countof(info->szText)); return 0;
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

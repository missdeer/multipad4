#include "FrameCommands.h"
#include "MainFrame.h"
#include "UpstreamUI.h"
#include <shellapi.h>
#include <array>
#include <fstream>
#include "StyleManager.h"
#include "SystemIntegration.h"
#include "Localization.h"
#include "EditAdvanced.h"
#include "AutoSave.h"
#include <cmath>
#include <cwctype>
namespace {
struct Option { UINT id; int initial; };
constexpr Option toggles[] = {
    {IDM_VIEW_REUSEWINDOW, 0}, {IDM_VIEW_SINGLEFILEINSTANCE, 1}, {IDM_VIEW_STICKY_WINDOW_POSITION, 0},
    {IDM_VIEW_MINTOTRAY, 0}, {IDM_VIEW_TRANSPARENT, 0}, {IDM_VIEW_TRANSPARENT_INACTIVE, 0},
    {IDM_VIEW_SHOWEXCERPT, 0}, {IDM_VIEW_NOSAVERECENT, 1}, {IDM_VIEW_NOSAVEFINDREPL, 1},
    {IDM_VIEW_SAVEBEFORERUNNINGTOOLS, 0}, {IDM_SET_OPEN_FOLDER_MATEPATH, 1},
    {IDM_VIEW_SAVESETTINGS, 1}, {IDM_VIEW_FULLSCREEN_ON_START, 0}, {IDM_VIEW_FULLSCREEN_HIDE_TITLE, 1},
    {IDM_VIEW_AUTO_SCALE_TOOLBAR, 1}, {IDM_VIEW_USE_LARGE_TOOLBAR, 0},
    {IDM_SET_RTL_LAYOUT_EDIT, 0}, {IDM_SET_RTL_LAYOUT_OTHER, 0}, {IDM_SET_USE_INLINE_IME, 1},
    {IDM_VIEW_CARET_STYLE_BLOCK_OVR, 1}, {IDM_VIEW_CARET_STYLE_NOBLINK, 0}, {IDM_VIEW_CARET_STYLE_SELECTION, 0},
    {IDM_SET_SELECTIONASFINDTEXT, 1}, {IDM_SET_PASTEBUFFERASFINDTEXT, 0}, {IDM_SET_UNDO_REDO_SELECTION, 0},
    {IDM_VIEW_UNICODE_CONTROL_CHAR, 0}
};
struct Radio { UINT first, last; int initial; };
constexpr Radio radios[] = {
    {IDM_SET_RENDER_TECH_GDI, IDM_SET_RENDER_TECH_D3D, 0},
    {IDM_SET_BIDIRECTIONAL_NONE, IDM_SET_BIDIRECTIONAL_R2L, 0},
    {IDM_VIEW_FONTQUALITY_DEFAULT, IDM_VIEW_FONTQUALITY_CLEARTYPE, 3},
    {IDM_VIEW_CARET_STYLE_BLOCK, IDM_VIEW_CARET_STYLE_WIDTH3, 1},
    {IDM_LINE_SELECTION_MODE_NONE, IDM_LINE_SELECTION_MODE_OLDVS, 1},
    {IDM_VIEW_SHOWFILENAMEONLY, IDM_VIEW_SHOWFULLPATH, 0},
    {IDM_VIEW_NOESCFUNC, IDM_VIEW_ESCEXIT, 0},
    {IDM_VIEW_SCROLLPASTLASTLINE_ONE, IDM_VIEW_SCROLLPASTLASTLINE_QUARTER, 1}
};
void ApplyGlobalEditorOptions(MainFrame& frame, EditorDocument& d) {
    auto& settings = frame.settings;
    d.Send(SCI_SETTECHNOLOGY, settings.Get(IDM_SET_RENDER_TECH_GDI));
    d.Send(SCI_SETBIDIRECTIONAL, settings.Get(IDM_SET_BIDIRECTIONAL_NONE));
    d.Send(SCI_SETFONTQUALITY, settings.Get(IDM_VIEW_FONTQUALITY_DEFAULT, 3));
    d.Send(SCI_SETIMEINTERACTION, settings.Get(IDM_SET_USE_INLINE_IME, 1) ? SC_IME_INLINE : SC_IME_WINDOWED);
    d.Send(SCI_SETENDATLASTLINE, settings.Get(IDM_VIEW_SCROLLPASTLASTLINE_ONE, 1));
    d.Send(SCI_SETUNDOSELECTIONHISTORY, settings.Get(IDM_SET_UNDO_REDO_SELECTION) ? SC_UNDO_SELECTION_HISTORY_ENABLED | SC_UNDO_SELECTION_HISTORY_SCROLL : 0);
    int caret = settings.Get(IDM_VIEW_CARET_STYLE_BLOCK, 1);
    int style = caret == 0 ? CARETSTYLE_BLOCK : CARETSTYLE_LINE;
    if (settings.Get(IDM_VIEW_CARET_STYLE_BLOCK_OVR, 1)) style |= CARETSTYLE_OVERSTRIKE_BLOCK;
    if (settings.Get(IDM_VIEW_CARET_STYLE_SELECTION)) style |= CARETSTYLE_BLOCK_AFTER;
    d.Send(SCI_SETCARETSTYLE, style); d.Send(SCI_SETCARETWIDTH, max(1, caret));
    d.Send(SCI_SETCARETPERIOD, settings.Get(IDM_VIEW_CARET_STYLE_NOBLINK) ? 0 : GetCaretBlinkTime());
    d.runtime.lineSelectionMode = settings.Get(IDM_LINE_SELECTION_MODE_NONE, 1);
    if (!d.runtime.lineSelectionMode) d.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    LONG_PTR exStyle = GetWindowLongPtrW(d.hwnd, GWL_EXSTYLE);
    if (settings.Get(IDM_SET_RTL_LAYOUT_EDIT)) { exStyle |= WS_EX_LAYOUTRTL; d.Send(SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DEFAULT); }
    else exStyle &= ~WS_EX_LAYOUTRTL;
    SetWindowLongPtrW(d.hwnd, GWL_EXSTYLE, exStyle);
    // Original Edit.cpp's Unicode representation table; built-in entries stay intact.
    constexpr unsigned codepoints[] = {0x200E,0x200F,0x200D,0x200C,0x202A,0x202B,0x202D,0x202E,0x202C,
        0x206E,0x206F,0x206B,0x206A,0x206D,0x206C,0x200B,0x2060,0x2066,0x2067,0x2068,0x2069,0x061C,0x00AD};
    constexpr const char* labels[] = {"LRM","RLM","ZWJ","ZWNJ","LRE","RLE","LRO","RLO","PDF","NADS","NODS",
        "ASS","ISS","AAFS","IAFS","ZWSP","WJ","LRI","RLI","FSI","PDI","ALM","SHY"};
    for (size_t i = 0; i < std::size(codepoints); ++i) {
        auto character = files::ToUtf8(std::wstring(1, static_cast<wchar_t>(codepoints[i])));
        d.Send(settings.Get(IDM_VIEW_UNICODE_CONTROL_CHAR) ? SCI_SETREPRESENTATION : SCI_CLEARREPRESENTATION,
            reinterpret_cast<WPARAM>(character.c_str()), reinterpret_cast<LPARAM>(labels[i]));
    }
}
void ToggleFullscreen(MainFrame& frame) {
    if (!frame.runtime.fullScreen) {
        frame.runtime.placement.length = sizeof(WINDOWPLACEMENT);
        frame.GetWindowPlacement(&frame.runtime.placement);
        frame.runtime.windowStyle = frame.GetStyle();
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(frame, MONITOR_DEFAULTTONEAREST), &monitor);
        LONG_PTR style = frame.runtime.windowStyle & ~WS_THICKFRAME;
        if (frame.settings.Get(IDM_VIEW_FULLSCREEN_HIDE_TITLE, 1)) style &= ~WS_CAPTION;
        SetWindowLongPtrW(frame, GWL_STYLE, style);
        frame.SetWindowPos(nullptr, &monitor.rcMonitor, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        SetWindowLongPtrW(frame, GWL_STYLE, frame.runtime.windowStyle);
        frame.SetWindowPlacement(&frame.runtime.placement);
        frame.SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
    frame.runtime.fullScreen = !frame.runtime.fullScreen;
    frame.UpdateLayout();
}
struct RuntimeDialog { MainFrame& frame; EditorDocument* document; UINT resource; };
INT_PTR CALLBACK RuntimeDialogProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* context = reinterpret_cast<RuntimeDialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<RuntimeDialog*>(l); SetWindowLongPtrW(window, DWLP_USER, l);
        if (context->resource == IDD_CHANGENOTIFY) {
            auto& watch = context->document->runtime;
            CheckRadioButton(window, IDC_CHANGENOTIFY_NONE, IDC_CHANGENOTIFY_AUTO_RELOAD, IDC_CHANGENOTIFY_NONE + watch.monitorMode);
            CheckDlgButton(window, IDC_CHANGENOTIFY_LOG_FILE, watch.monitorLog);
            CheckDlgButton(window, IDC_CHANGENOTIFY_KEEP_AT_END, watch.keepAtEnd);
            CheckDlgButton(window, IDC_CHANGENOTIFY_RESET_WATCH, watch.resetWatch);
        } else {
            auto& settings = context->frame.settings;
            int options = settings.Get(IDM_SET_FILE_AUTOSAVE, 6);
            for (int i = 0; i < 5; ++i) CheckDlgButton(window, IDC_AUTOSAVE_ENABLE + (i ? i + 1 : 0), (options & (1 << i)) ? BST_CHECKED : BST_UNCHECKED);
            const DWORD period = AutoSavePeriodMilliseconds(context->frame);
            wchar_t text[32]{};
            swprintf_s(text, L"%lu.%03lu", period / 1000, period % 1000);
            SetDlgItemTextW(window, IDC_AUTOSAVE_PERIOD, text);
        }
        CWindow(window).CenterWindow(context->frame); return TRUE;
    }
    if (!context) return FALSE;
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) { EndDialog(window, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND) return FALSE;
    if (LOWORD(w) == IDC_AUTOSAVE_OPENFOLDER && context->resource == IDD_AUTOSAVE) {
        auto folder = AutoSaveDirectory(context->frame);
        std::error_code ec; std::filesystem::create_directories(folder, ec);
        ShellExecuteW(window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return TRUE;
    }
    if (LOWORD(w) != IDOK) return FALSE;
    if (context->resource == IDD_CHANGENOTIFY) {
        auto& watch = context->document->runtime;
        for (int i = 0; i < 3; ++i) if (IsDlgButtonChecked(window, IDC_CHANGENOTIFY_NONE + i)) watch.monitorMode = i;
        watch.monitorLog = IsDlgButtonChecked(window, IDC_CHANGENOTIFY_LOG_FILE) != 0;
        watch.keepAtEnd = IsDlgButtonChecked(window, IDC_CHANGENOTIFY_KEEP_AT_END) != 0;
        watch.resetWatch = IsDlgButtonChecked(window, IDC_CHANGENOTIFY_RESET_WATCH) != 0;
        SnapshotDocument(*context->document);
        context->frame.settings.Set(IDM_VIEW_CHANGENOTIFY, watch.monitorMode);
        context->frame.settings.Set(60110, static_cast<int>(watch.monitorLog) | (static_cast<int>(watch.keepAtEnd) << 1) | (static_cast<int>(watch.resetWatch) << 2));
    } else {
        wchar_t text[128]{};
        GetDlgItemTextW(window, IDC_AUTOSAVE_PERIOD, text, _countof(text));
        wchar_t* end = nullptr;
        const double milliseconds = wcstod(text, &end) * 1000;
        const bool converted = end != text;
        while (end && iswspace(*end)) ++end;
        if (GetWindowTextLengthW(GetDlgItem(window, IDC_AUTOSAVE_PERIOD)) >= static_cast<int>(_countof(text))
            || !converted || (end && *end) || !std::isfinite(milliseconds) || milliseconds < 0 || milliseconds > MAXDWORD) {
            SetFocus(GetDlgItem(window, IDC_AUTOSAVE_PERIOD)); MessageBeep(MB_ICONWARNING); return TRUE;
        }
        const auto period = static_cast<DWORD>(std::round(milliseconds));
        int options = 0;
        for (int i = 0; i < 5; ++i) if (IsDlgButtonChecked(window, IDC_AUTOSAVE_ENABLE + (i ? i + 1 : 0))) options |= 1 << i;
        context->frame.settings.Set(IDM_SET_FILE_AUTOSAVE, options);
        context->frame.settings.WriteString(L"AutoSave", L"PeriodMilliseconds", std::to_wstring(period));
    }
    EndDialog(window, IDOK); return TRUE;
}
}
CommandState FrameCommandState(MainFrame& frame, UINT id) {
    if (id >= IDM_LANG_USER_DEFAULT && id <= IDM_LANG_LAST_LANGUAGE)
        return {true, true, static_cast<UINT>(frame.settings.Get(IDM_LANG_USER_DEFAULT, IDM_LANG_USER_DEFAULT)) == id};
    for (const auto& option : toggles) if (id == option.id) return {true, true, frame.settings.Get(id, option.initial) != 0};
    for (const auto& radio : radios) if (id >= radio.first && id <= radio.last)
        return {true, true, frame.settings.Get(radio.first, radio.initial) == static_cast<int>(id - radio.first)};
    switch (id) {
    case IDM_SET_FILE_AUTOSAVE:
        return {true, true, (frame.settings.Get(IDM_SET_FILE_AUTOSAVE, 6) & 1) != 0 && AutoSavePeriodMilliseconds(frame) != 0};
    case IDM_VIEW_CHANGENOTIFY: return {true, frame.Active() != nullptr, frame.Active() && frame.Active()->runtime.monitorMode != 0};
    case CMD_INCLINELIMIT: case CMD_DECLINELIMIT: return {true, frame.Active() != nullptr, false};
    case BME_EDIT_BOOKMARKTOGGLE: case BME_EDIT_BOOKMARKNEXT: case BME_EDIT_BOOKMARKCLEAR:
    case BME_EDIT_BOOKMARKPREV: case BME_EDIT_BOOKMARKSELECT: return {true, frame.Active() != nullptr, false};
    case IDM_VIEW_TOGGLE_FULLSCREEN: return {true, true, frame.runtime.fullScreen};
    case IDM_VIEW_MENU: return {true, true, frame.runtime.menuVisible};
    case IDM_SET_SYSTEM_INTEGRATION: case IDM_TRAY_RESTORE: case IDM_TRAY_EXIT:
    case IDM_EDIT_CLEARCLIPBOARD:
    case IDM_VIEW_SAVESETTINGSNOW: case IDM_VIEW_CLEARWINPOS:
    case CMD_DEFAULTWINPOS: case CMD_COPYWINPOS: case CMD_OPENINIFILE: case CMD_ESCAPE: case CMD_SHIFTESC:
    case IDM_CMDLINE_HELP: case IDM_HELP_PROJECT_HOME: case IDM_HELP_LATEST_RELEASE: case IDM_HELP_LATEST_BUILD:
    case IDM_HELP_REPORT_ISSUE: case IDM_HELP_FEATURE_REQUEST: case IDM_HELP_ONLINE_WIKI:
        return {true, true, false};
    default: return {};
    }
}
bool ExecuteFrameCommand(MainFrame& frame, UINT id) {
    if (!FrameCommandState(frame, id).supported) return false;
    if (id >= IDM_LANG_USER_DEFAULT && id <= IDM_LANG_LAST_LANGUAGE) {
        if (static_cast<UINT>(frame.settings.Get(IDM_LANG_USER_DEFAULT, IDM_LANG_USER_DEFAULT)) == id) return true;
        wchar_t message[1024]{};
        LoadStringW(_Module.GetModuleInstance(), IDS_CHANGE_LANG_RESTART, message, _countof(message));
        const int result = frame.MessageBox(message, L"MultiPad4", MB_YESNOCANCEL | MB_ICONINFORMATION);
        if (result != IDYES && result != IDNO) return true;
        frame.settings.Set(IDM_LANG_USER_DEFAULT, id);
        frame.settings.Save();
        if (result == IDYES) frame.PostMessage(WM_COMMAND, IDM_FILE_RESTART);
        return true;
    }
    for (const auto& option : toggles) if (id == option.id) {
        if (id == IDM_VIEW_STICKY_WINDOW_POSITION && !frame.settings.Get(id, option.initial)) {
            SaveFramePreferences(frame); frame.settings.Save();
        }
        frame.settings.Set(id, !frame.settings.Get(id, option.initial));
        if (id == IDM_VIEW_REUSEWINDOW || id == IDM_VIEW_SINGLEFILEINSTANCE || id == IDM_VIEW_STICKY_WINDOW_POSITION)
            if (!frame.settings.SaveOption(id)) frame.MessageBox(L"Cannot save the window option.", L"MultiPad4", MB_ICONERROR);
        if (id == IDM_VIEW_TRANSPARENT || id == IDM_VIEW_TRANSPARENT_INACTIVE) {
            if (frame.settings.Get(id)) frame.settings.Set(id == IDM_VIEW_TRANSPARENT ? IDM_VIEW_TRANSPARENT_INACTIVE : IDM_VIEW_TRANSPARENT, 0);
        }
        if (id == IDM_VIEW_FULLSCREEN_HIDE_TITLE && frame.runtime.fullScreen) { ToggleFullscreen(frame); ToggleFullscreen(frame); }
        for (auto& d : frame.documents) ApplyGlobalEditorOptions(frame, *d);
        SetFrameTransparency(frame, GetForegroundWindow() == frame.m_hWnd);
        if (id == IDM_VIEW_AUTO_SCALE_TOOLBAR || id == IDM_VIEW_USE_LARGE_TOOLBAR) UpdateToolbarImages(frame);
        if (id == IDM_SET_RTL_LAYOUT_OTHER) {
            auto style = frame.GetExStyle();
            SetWindowLongPtrW(frame, GWL_EXSTYLE, frame.settings.Get(id) ? style | WS_EX_LAYOUTRTL : style & ~WS_EX_LAYOUTRTL);
            frame.SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        }
        return true;
    }
    for (const auto& radio : radios) if (id >= radio.first && id <= radio.last) {
        frame.settings.Set(radio.first, id - radio.first);
        for (auto& d : frame.documents) ApplyGlobalEditorOptions(frame, *d);
        if (auto* active = frame.Active()) {
            if (radio.first == IDM_SET_RENDER_TECH_GDI) frame.settings.Set(radio.first, static_cast<int>(active->Send(SCI_GETTECHNOLOGY)));
            if (radio.first == IDM_SET_BIDIRECTIONAL_NONE) frame.settings.Set(radio.first, static_cast<int>(active->Send(SCI_GETBIDIRECTIONAL)));
        }
        return true;
    }
    switch (id) {
    case CMD_INCLINELIMIT: case CMD_DECLINELIMIT: {
        auto* d = frame.Active(); if (!d) break;
        if (d->Send(SCI_GETEDGEMODE) == EDGE_NONE) {
            ExecuteStyleCommand(frame, *d, IDM_VIEW_LONGLINEMARKER);
            RememberDocumentPreferences(frame, *d, IDM_VIEW_LONGLINEMARKER);
        } else {
            const int column = std::clamp(frame.defaultStyle.longLineColumn + (id == CMD_INCLINELIMIT ? 1 : -1), 0, 4096);
            d->Send(SCI_SETEDGECOLUMN, column); d->style.longLineColumn = column;
            RememberDocumentPreferences(frame, *d, IDM_VIEW_LONGLINESETTINGS);
        }
        break;
    }
    case IDM_EDIT_CLEARCLIPBOARD:
        if (::OpenClipboard(frame)) { EmptyClipboard(); ::CloseClipboard(); } break;
    case BME_EDIT_BOOKMARKTOGGLE: case BME_EDIT_BOOKMARKNEXT: case BME_EDIT_BOOKMARKCLEAR:
    case BME_EDIT_BOOKMARKPREV: case BME_EDIT_BOOKMARKSELECT: {
        auto* d = frame.Active(); if (!d) break;
        auto line = d->Send(SCI_LINEFROMPOSITION, d->Send(SCI_GETCURRENTPOS));
        if (id == BME_EDIT_BOOKMARKTOGGLE) d->Send(d->Send(SCI_MARKERGET, line) & 1 ? SCI_MARKERDELETE : SCI_MARKERADD, line, 0);
        else if (id == BME_EDIT_BOOKMARKCLEAR) d->Send(SCI_MARKERDELETEALL, 0);
        else if (id == BME_EDIT_BOOKMARKSELECT) {
            auto next = d->Send(SCI_MARKERNEXT, 0, 1); bool first = true;
            while (next >= 0) {
                auto start = d->Send(SCI_POSITIONFROMLINE, next);
                auto end = next + 1 < d->Send(SCI_GETLINECOUNT) ? d->Send(SCI_POSITIONFROMLINE, next + 1) : d->Send(SCI_GETLENGTH);
                d->Send(SCI_SETMULTIPLESELECTION, TRUE);
                d->Send(first ? SCI_SETSELECTION : SCI_ADDSELECTION, end, start); first = false;
                next = d->Send(SCI_MARKERNEXT, next + 1, 1);
            }
        } else {
            bool previous = id == BME_EDIT_BOOKMARKPREV;
            auto next = d->Send(previous ? SCI_MARKERPREVIOUS : SCI_MARKERNEXT, line + (previous ? -1 : 1), 1);
            if (next < 0) next = d->Send(previous ? SCI_MARKERPREVIOUS : SCI_MARKERNEXT, previous ? d->Send(SCI_GETLINECOUNT) - 1 : 0, 1);
            if (next >= 0) { d->Send(SCI_GOTOLINE, next); d->Send(SCI_SCROLLCARET); }
        }
        break;
    }
    case IDM_SET_SYSTEM_INTEGRATION: ShowSystemIntegration(frame); break;
    case IDM_TRAY_RESTORE: SetTrayVisible(frame, false); frame.ShowWindow(SW_RESTORE); SetForegroundWindow(frame); break;
    case IDM_TRAY_EXIT: frame.PostMessage(WM_CLOSE); break;
    case IDM_VIEW_CHANGENOTIFY: case IDM_SET_FILE_AUTOSAVE: {
        auto* d = frame.Active(); if (id == IDM_VIEW_CHANGENOTIFY && !d) return true;
        RuntimeDialog context{frame, d, static_cast<UINT>(id == IDM_VIEW_CHANGENOTIFY ? IDD_CHANGENOTIFY : IDD_AUTOSAVE)};
        DialogBoxParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(context.resource), frame, RuntimeDialogProc, reinterpret_cast<LPARAM>(&context)); break;
    }
    case IDM_VIEW_TOGGLE_FULLSCREEN: ToggleFullscreen(frame); break;
    case IDM_VIEW_MENU:
        frame.runtime.menuVisible = !frame.runtime.menuVisible;
        frame.SetMenu(frame.runtime.menuVisible ? frame.runtime.mainMenu : nullptr); frame.DrawMenuBar(); break;
    case IDM_VIEW_SAVESETTINGSNOW: SaveFramePreferences(frame); if (!frame.settings.Save()) frame.MessageBox(L"Cannot save settings.", L"MultiPad4", MB_ICONERROR); break;
    case IDM_VIEW_CLEARWINPOS: frame.settings.WriteString(L"Window", L"Placement", L""); break;
    case CMD_DEFAULTWINPOS: frame.SetWindowPos(nullptr, 80, 60, 1200, 800, SWP_NOZORDER); break;
    case CMD_COPYWINPOS: {
        RECT rect{}; frame.GetWindowRect(&rect);
        auto text = std::to_string(rect.left) + "," + std::to_string(rect.top) + "," + std::to_string(rect.right - rect.left) + "," + std::to_string(rect.bottom - rect.top);
        if (auto* d = frame.Active()) d->Send(SCI_COPYTEXT, text.size(), reinterpret_cast<LPARAM>(text.c_str())); break;
    }
    case CMD_OPENINIFILE: SaveFramePreferences(frame); frame.settings.Save(); frame.Open(frame.settings.path); break;
    case CMD_ESCAPE:
        if (auto* d = frame.Active(); d && d->Send(SCI_AUTOCACTIVE)) d->Send(SCI_AUTOCCANCEL);
        else if (d && d->Send(SCI_CALLTIPACTIVE)) d->Send(SCI_CALLTIPCANCEL);
        else if (frame.runtime.fullScreen) ToggleFullscreen(frame);
        else if (frame.settings.Get(IDM_VIEW_NOESCFUNC) == 1) frame.ShowWindow(SW_MINIMIZE);
        else if (frame.settings.Get(IDM_VIEW_NOESCFUNC) == 2) frame.PostMessage(WM_CLOSE);
        break;
    case CMD_SHIFTESC: frame.PostMessage(WM_CLOSE); break;
    case IDM_CMDLINE_HELP: frame.MessageBox(L"MultiPad4 [file ...]\n\nEach file opens in its own MDI document.\nCtrl+Tab: next document; Ctrl+W: close document.\n/n: new window.\n/e:<codepage>: open using an explicit encoding.\n/g:<line>,<column>: jump after opening.\n/r: open read-only.\n--smoke-test: run integration checks.", L"Command Line", MB_OK); break;
    default: {
        const wchar_t* url = nullptr;
        switch (id) {
        case IDM_HELP_PROJECT_HOME: url = L"https://github.com/missdeer/multipad4"; break;
        case IDM_HELP_LATEST_RELEASE: url = L"https://github.com/missdeer/multipad4/releases/latest"; break;
        case IDM_HELP_LATEST_BUILD: url = L"https://github.com/missdeer/multipad4/actions/workflows/msvc-build.yml"; break;
        case IDM_HELP_REPORT_ISSUE: url = L"https://github.com/missdeer/multipad4/issues"; break;
        case IDM_HELP_FEATURE_REQUEST: url = L"https://github.com/missdeer/multipad4/issues/new"; break;
        case IDM_HELP_ONLINE_WIKI: url = L"https://github.com/missdeer/multipad4/tree/HEAD/docs"; break;
        }
        if (url) ShellExecuteW(frame, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    }
    return true;
}
void SetFrameTransparency(MainFrame& frame, bool active) {
    bool transparent = frame.settings.Get(IDM_VIEW_TRANSPARENT) || (!active && frame.settings.Get(IDM_VIEW_TRANSPARENT_INACTIVE));
    auto style = frame.GetExStyle();
    SetWindowLongPtrW(frame, GWL_EXSTYLE, transparent ? style | WS_EX_LAYERED : style & ~WS_EX_LAYERED);
    if (transparent) SetLayeredWindowAttributes(frame, 0, static_cast<BYTE>(frame.settings.Get(60002, 220)), LWA_ALPHA);
}
void SetTrayVisible(MainFrame& frame, bool visible) {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = frame; data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; data.uCallbackMessage = WM_APP + 0x103;
    data.hIcon = reinterpret_cast<HICON>(LoadImageW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDR_MAINFRAME), IMAGE_ICON, 16, 16, LR_SHARED));
    wcscpy_s(data.szTip, L"MultiPad4");
    if (visible && !frame.runtime.trayPresent) frame.runtime.trayPresent = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    else if (!visible && frame.runtime.trayPresent) { Shell_NotifyIconW(NIM_DELETE, &data); frame.runtime.trayPresent = false; }
}
void UpdateToolbarImages(MainFrame& frame) {
    int dpi = frame.settings.Get(IDM_VIEW_AUTO_SCALE_TOOLBAR, 1) ? GetDpiForWindow(frame) : 96;
    if (frame.settings.Get(IDM_VIEW_USE_LARGE_TOOLBAR)) dpi = MulDiv(dpi, 3, 2);
    int size = dpi >= 288 ? 48 : dpi >= 240 ? 40 : dpi >= 192 ? 32 : dpi >= 144 ? 24 : 16;
    UINT resource = size == 48 ? IDB_TOOLBAR48 : size == 40 ? IDB_TOOLBAR40 : size == 32 ? IDB_TOOLBAR32 : size == 24 ? IDB_TOOLBAR24 : IDB_TOOLBAR16;
    auto images = ImageList_LoadImageW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(resource), size, 0, CLR_DEFAULT, IMAGE_BITMAP, LR_CREATEDIBSECTION);
    if (!images) return;
    ::SendMessageW(frame.m_hWndToolBar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
    ::SendMessageW(frame.m_hWndToolBar, TB_SETBITMAPSIZE, 0, MAKELPARAM(size, size));
    if (frame.toolbarImages) ImageList_Destroy(frame.toolbarImages);
    frame.toolbarImages = images;
    ::SendMessageW(frame.m_hWndToolBar, TB_AUTOSIZE, 0, 0); frame.UpdateLayout();
}
void InitializeFramePreferences(MainFrame& frame) {
    frame.runtime.mainMenu = frame.GetMenu();
    auto readStyleInt = [&](const wchar_t* key, int fallback) { return _wtoi(frame.settings.ReadString(L"Editor Defaults", key, std::to_wstring(fallback).c_str()).c_str()); };
    auto& style = frame.defaultStyle;
    // File variables and the File group in Tab Settings never define defaults.
    style.useSchemeTabs = true; style.tabsOverride = false; style.appliedTabScheme = -1;
    style.dark = readStyleInt(L"Dark", 0) != 0;
    style.font = frame.settings.ReadString(L"Editor Defaults", L"Font", L"Consolas");
    style.fontSize = readStyleInt(L"FontSize", 11); style.fontKind = readStyleInt(L"FontKind", -1);
    style.codeFont = frame.settings.ReadString(L"Editor Defaults", L"CodeFont");
    style.textFont = frame.settings.ReadString(L"Editor Defaults", L"TextFont");
    style.codeFontSize = readStyleInt(L"CodeFontSize", 11); style.textFontSize = readStyleInt(L"TextFontSize", 11);
    style.completion = readStyleInt(L"Completion", 1) != 0;
    style.completionIgnoreCase = readStyleInt(L"IgnoreCase", 0) != 0;
    style.autoIndent = readStyleInt(L"AutoIndent", 1) != 0; style.latex = readStyleInt(L"LaTeX", 0) != 0;
    style.folding = readStyleInt(L"Folding", 1) != 0; style.matchBraces = readStyleInt(L"MatchBraces", 1) != 0;
    style.markOccurrences = readStyleInt(L"MarkOccurrences", 1); style.colorCalltip = readStyleInt(L"ColorCalltip", 0);
    style.highlightBlock = readStyleInt(L"HighlightBlock", 1) != 0;
    style.lineHighlight = readStyleInt(L"LineHighlight", 2); style.highlightSubline = readStyleInt(L"HighlightSubline", 1) != 0;
    style.backspaceMask = readStyleInt(L"BackspaceMask", 2); style.tabIndents = readStyleInt(L"TabIndents", 1) != 0;
    style.backspaceUnindents = (style.backspaceMask & 1) != 0;
    style.detectIndentation = readStyleInt(L"DetectIndentation", 1) != 0;
    style.wrapMode = readStyleInt(L"WrapMode", SC_WRAP_AUTO); style.wrapIndent = readStyleInt(L"WrapIndent", 0);
    style.wrapSymbols = readStyleInt(L"WrapSymbols", 2); style.wrapSymbolsShown = readStyleInt(L"WrapSymbolsShown", 0) != 0;
    style.csvOptions = readStyleInt(L"CsvOptions", ('"' << 8) | ',');
    style.csvConfigured = readStyleInt(L"CsvConfigured", 0) != 0;
    style.longLineColumn = std::clamp(readStyleInt(L"EdgeColumn", 80), 0, 1000000);
    style.longLineMode = std::clamp(readStyleInt(L"EdgeMode", EDGE_LINE), EDGE_LINE, EDGE_BACKGROUND);
    style.longLineEnabled = readStyleInt(L"EdgeVisible", 0) != 0;
    auto searchPosition = frame.settings.ReadString(L"Search", L"Position"); int searchX = 0, searchY = 0;
    if (swscanf_s(searchPosition.c_str(), L"%d,%d", &searchX, &searchY) == 2) {
        RECT rect{searchX, searchY, searchX + 100, searchY + 100};
        if (MonitorFromRect(&rect, MONITOR_DEFAULTTONULL)) { frame.searchDialog.hasSavedPosition = true; frame.searchDialog.savedPosition = rect; }
    }
    style.completionFillups = frame.settings.ReadString(L"Editor Defaults", L"CompletionFillups", L";,()[]{}\\/");
    for (int id = 100; id <= 403; ++id) {
        auto key = L"Completion" + std::to_wstring(id);
        auto value = frame.settings.ReadString(L"Editor Defaults", key.c_str());
        if (!value.empty()) style.completionOptions[id] = _wtoi(value.c_str());
    }
    for (int id : {IDM_VIEW_MARKOCCURRENCES_CASE, IDM_VIEW_MARKOCCURRENCES_WORD, IDM_VIEW_MARKOCCURRENCES_BOOKMARK}) {
        auto key = L"Completion" + std::to_wstring(id); auto value = frame.settings.ReadString(L"Editor Defaults", key.c_str());
        if (!value.empty()) style.completionOptions[id] = _wtoi(value.c_str());
    }
    frame.toolbarVisible = frame.settings.Get(IDM_VIEW_TOOLBAR, 1) != 0;
    frame.statusVisible = frame.settings.Get(IDM_VIEW_STATUSBAR, 1) != 0;
    ::ShowWindow(frame.m_hWndToolBar, frame.toolbarVisible ? SW_SHOW : SW_HIDE);
    ::ShowWindow(frame.m_hWndStatusBar, frame.statusVisible ? SW_SHOW : SW_HIDE);
    auto placement = frame.settings.ReadString(L"Window", L"Placement");
    int x = 0, y = 0, width = 0, height = 0, show = 0;
    if (swscanf_s(placement.c_str(), L"%d,%d,%d,%d,%d", &x, &y, &width, &height, &show) == 5 && width >= 200 && height >= 100) {
        RECT rect{x, y, x + width, y + height};
        if (MonitorFromRect(&rect, MONITOR_DEFAULTTONULL)) frame.SetWindowPos(nullptr, &rect, SWP_NOZORDER | SWP_NOACTIVATE);
        if (show == SW_SHOWMAXIMIZED) frame.runtime.restoredShowCmd = SW_SHOWMAXIMIZED;
    }
    RestoreUpstreamToolbar(frame.m_hWndToolBar, frame.settings.ReadString(L"Toolbar", L"Buttons"));
    UpdateToolbarImages(frame);
    if (!frame.settings.Get(IDM_VIEW_MENU, 1)) ExecuteFrameCommand(frame, IDM_VIEW_MENU);
    if (frame.settings.Get(IDM_VIEW_FULLSCREEN_ON_START)) ToggleFullscreen(frame);
    if (frame.settings.Get(IDM_VIEW_ALWAYSONTOP)) frame.SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetFrameTransparency(frame, true); frame.UpdateLayout();
    if (frame.settings.Get(IDM_SET_RTL_LAYOUT_OTHER)) {
        SetWindowLongPtrW(frame, GWL_EXSTYLE, frame.GetExStyle() | WS_EX_LAYOUTRTL);
        frame.SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
}
void ApplyDocumentPreferences(MainFrame& frame, EditorDocument& d) {
    d.style = frame.defaultStyle;
    d.style.useSchemeTabs = true; d.style.tabsOverride = false; d.style.appliedTabScheme = -1;
    ConfigureDocumentStyle(d);
    ApplyGlobalEditorOptions(frame, d);
    struct Preference { UINT id, get, set; int fallback; };
    constexpr Preference preferences[] = {
        {IDM_VIEW_SHOWWHITESPACE, SCI_GETVIEWWS, SCI_SETVIEWWS, SCWS_INVISIBLE},
        {IDM_VIEW_SHOWEOLS, SCI_GETVIEWEOL, SCI_SETVIEWEOL, 0},
        {IDM_VIEW_SHOWINDENTGUIDES, SCI_GETINDENTATIONGUIDES, SCI_SETINDENTATIONGUIDES, SC_IV_NONE},
        {IDM_SET_MULTIPLE_SELECTION, SCI_GETMULTIPLESELECTION, SCI_SETMULTIPLESELECTION, 0},
    };
    for (const auto& preference : preferences) d.Send(preference.set, frame.settings.Get(preference.id, preference.fallback));
    d.Send(SCI_SETWRAPMODE, frame.settings.Get(IDM_VIEW_WORDWRAP, SC_WRAP_AUTO) ? d.style.wrapMode : SC_WRAP_NONE);
    ApplyDocumentWrapSettings(d);
    d.runtime.monitorMode = frame.settings.Get(IDM_VIEW_CHANGENOTIFY);
    int watch = frame.settings.Get(60110);
    d.runtime.monitorLog = (watch & 1) != 0; d.runtime.keepAtEnd = (watch & 2) != 0; d.runtime.resetWatch = (watch & 4) != 0;
    if (d.runtime.resetWatch) d.runtime.monitorMode = 0;
    d.Send(SCI_SETEOLMODE, frame.settings.Get(IDM_LINEENDINGS_SETDEFAULT, SC_EOL_CRLF));
    d.runtime.timestampTemplate = frame.settings.ReadString(L"Settings2", L"TimeStamp", d.runtime.timestampTemplate.c_str());
    d.runtime.dateTimeShort = frame.settings.ReadString(L"Settings2", L"DateTimeShort");
    d.runtime.dateTimeLong = frame.settings.ReadString(L"Settings2", L"DateTimeLong");
    auto readDefault = [&](const wchar_t* key, int fallback) { return _wtoi(frame.settings.ReadString(L"Editor Defaults", key, std::to_wstring(fallback).c_str()).c_str()); };
    d.Send(SCI_SETMARGINWIDTHN, 0, readDefault(L"LineNumbers", 1) ? 54 : 0);
    d.Send(SCI_SETMARGINWIDTHN, 1, readDefault(L"BookmarkMargin", 0) ? MulDiv(16, GetDpiForWindow(d.hwnd), 96) : 0);
    d.Send(SCI_SETZOOM, std::clamp(readDefault(L"Zoom", 100), 10, 500));
    if (frame.settings.Get(IDM_VIEW_NOSAVEFINDREPL, 1)) {
        auto& search = d.search;
        search.findText = frame.settings.ReadString(L"Search", L"Find");
        search.replaceText = frame.settings.ReadString(L"Search", L"Replace");
        search.flags = _wtoi(frame.settings.ReadString(L"Search", L"Flags", L"0").c_str());
        int options = _wtoi(frame.settings.ReadString(L"Search", L"Options", L"1").c_str());
        search.wrap = (options & 1) != 0; search.regex = (options & 2) != 0; search.wildcard = (options & 4) != 0;
        search.transformBackslashes = (options & 8) != 0; search.dotMatchesNewline = (options & 16) != 0;
        search.useBoostRegex = (options & 32) != 0;
        search.closeAfterAction = (options & 64) != 0; search.bookmarkMatches = (options & 128) != 0;
        search.useMonospaced = (options & 256) != 0; search.transparent = (options & 512) != 0;
        search.closeAfterFind = (options & 1024) != 0; search.closeAfterReplace = (options & 2048) != 0;
        for (int i = 0; i < 32; ++i) {
            auto key = std::to_wstring(i); auto find = frame.settings.ReadString(L"Find History", key.c_str());
            auto replace = frame.settings.ReadString(L"Replace History", key.c_str());
            if (!find.empty()) search.findHistory.push_back(find);
            if (!replace.empty()) search.replaceHistory.push_back(replace);
        }
    }
}
void RememberDocumentPreferences(MainFrame& frame, EditorDocument& d, UINT command) {
    if (command == IDM_VIEW_TABSASSPACES) {
        d.style.tabsAsSpaces = !d.Send(SCI_GETUSETABS);
        d.style.tabsOverride = true;
    }
    const auto previousTabs = frame.defaultStyle;
    frame.defaultStyle = d.style;
    // A file's effective column (including fill-column) is not a new-file default.
    frame.defaultStyle.longLineColumn = previousTabs.longLineColumn;
    frame.defaultStyle.longLineMode = previousTabs.longLineMode;
    frame.defaultStyle.longLineEnabled = previousTabs.longLineEnabled;
    if (command == IDM_VIEW_LONGLINESETTINGS) {
        frame.defaultStyle.longLineColumn = static_cast<int>(d.Send(SCI_GETEDGECOLUMN));
        frame.defaultStyle.longLineMode = d.edgeMode;
    }
    if (command == IDM_VIEW_LONGLINEMARKER || command == IDM_VIEW_LONGLINESETTINGS)
        frame.defaultStyle.longLineEnabled = d.Send(SCI_GETEDGEMODE) != EDGE_NONE;
    // Widths and spaces are resolved from the shared global/scheme settings
    // when a document is created. Never promote a file's effective values.
    frame.defaultStyle.tabWidth = 4; frame.defaultStyle.indentWidth = 4;
    frame.defaultStyle.tabsAsSpaces = false;
    frame.defaultStyle.useSchemeTabs = true; frame.defaultStyle.tabsOverride = false;
    if (command != IDM_VIEW_TABSETTINGS) {
        frame.defaultStyle.tabIndents = previousTabs.tabIndents;
        frame.defaultStyle.backspaceMask = previousTabs.backspaceMask;
        frame.defaultStyle.detectIndentation = previousTabs.detectIndentation;
    }
    frame.defaultStyle.backspaceUnindents = (frame.defaultStyle.backspaceMask & 1) != 0;
    frame.defaultStyle.scheme = 0; frame.defaultStyle.dialect = 0; frame.defaultStyle.appliedTabScheme = -1;
    frame.defaultStyle.overrides.clear();
    frame.defaultStyle.autoClosers.clear();
    frame.defaultStyle.colorStart = frame.defaultStyle.colorEnd = -1;
    frame.defaultStyle.calltipHex.clear(); frame.defaultStyle.calltipColor = 0;
    frame.defaultStyle.resolvedDialect = 0;
    frame.defaultStyle.resolvedScheme = 0;
    frame.defaultStyle.revision = frame.defaultStyle.occurrenceRevision = 0;
    frame.defaultStyle.occurrenceNeedle.clear(); frame.defaultStyle.occurrenceCursor = 0;
    frame.defaultStyle.occurrences = 0; frame.defaultStyle.occurrenceFlags = 0; frame.defaultStyle.occurrencePending = false;
    auto writeStyle = [&](const wchar_t* key, int value) { frame.settings.WriteString(L"Editor Defaults", key, std::to_wstring(value)); };
    const auto& style = frame.defaultStyle;
    frame.settings.WriteString(L"Editor Defaults", L"Font", style.font);
    frame.settings.WriteString(L"Editor Defaults", L"CodeFont", style.codeFont);
    frame.settings.WriteString(L"Editor Defaults", L"TextFont", style.textFont);
    writeStyle(L"CodeFontSize", style.codeFontSize); writeStyle(L"TextFontSize", style.textFontSize);
    writeStyle(L"Dark", style.dark); writeStyle(L"FontSize", style.fontSize); writeStyle(L"FontKind", style.fontKind);
    writeStyle(L"Completion", style.completion); writeStyle(L"IgnoreCase", style.completionIgnoreCase);
    writeStyle(L"AutoIndent", style.autoIndent); writeStyle(L"LaTeX", style.latex);
    writeStyle(L"Folding", style.folding); writeStyle(L"MatchBraces", style.matchBraces);
    writeStyle(L"MarkOccurrences", style.markOccurrences); writeStyle(L"ColorCalltip", style.colorCalltip);
    writeStyle(L"HighlightBlock", style.highlightBlock);
    writeStyle(L"LineHighlight", style.lineHighlight); writeStyle(L"HighlightSubline", style.highlightSubline);
    writeStyle(L"BackspaceMask", style.backspaceMask); writeStyle(L"TabIndents", style.tabIndents); writeStyle(L"DetectIndentation", style.detectIndentation);
    if (command == IDM_VIEW_TABSETTINGS) {
        for (auto& other : frame.documents) {
            // Upstream has a per-file TabIndents value (including modelines),
            // while BackspaceMask and DetectIndentation are always global.
            int value = 0;
            const bool fileTabIndents = other.get() != &d && other->fileVariables.active
                && filevars::Integer(filevars::Region(other->Text()), "*tab-always-indent", value)
                && other->style.tabIndents == other->fileVariables.applied.tabIndents;
            if (!fileTabIndents) other->style.tabIndents = style.tabIndents;
            if (other->fileVariables.active) other->fileVariables.baseline.tabIndents = style.tabIndents;
            other->style.backspaceMask = style.backspaceMask;
            other->style.backspaceUnindents = style.backspaceUnindents;
            other->style.detectIndentation = style.detectIndentation;
            other->Send(SCI_SETTABINDENTS, other->style.tabIndents);
            other->Send(SCI_SETBACKSPACEUNINDENTS, style.backspaceMask & 3);
        }
    }
    writeStyle(L"WrapMode", style.wrapMode); writeStyle(L"WrapIndent", style.wrapIndent); writeStyle(L"WrapSymbols", style.wrapSymbols); writeStyle(L"WrapSymbolsShown", style.wrapSymbolsShown);
    writeStyle(L"CsvOptions", style.csvOptions); writeStyle(L"CsvConfigured", style.csvConfigured);
    frame.settings.WriteString(L"Editor Defaults", L"CompletionFillups", style.completionFillups);
    for (const auto& [id, value] : style.completionOptions) writeStyle((L"Completion" + std::to_wstring(id)).c_str(), value);
    if (command == IDM_VIEW_LINENUMBERS) writeStyle(L"LineNumbers", d.Send(SCI_GETMARGINWIDTHN, 0) != 0);
    if (command == IDM_VIEW_MARGIN) writeStyle(L"BookmarkMargin", d.Send(SCI_GETMARGINWIDTHN, 1) != 0);
    if (command == IDM_VIEW_LONGLINEMARKER || command == IDM_VIEW_LONGLINESETTINGS) {
        writeStyle(L"EdgeVisible", style.longLineEnabled); writeStyle(L"EdgeColumn", style.longLineColumn); writeStyle(L"EdgeMode", style.longLineMode);
        for (auto& other : frame.documents) {
            int value = 0;
            const bool fileColumn = other->fileVariables.active
                && filevars::Integer(filevars::Region(other->Text()), "fill-column", value)
                && other->Send(SCI_GETEDGECOLUMN) == other->fileVariables.appliedEdge;
            if (command == IDM_VIEW_LONGLINESETTINGS && other.get() != &d) {
                // Preserve an explicit file value; ordinary peers follow the global limit.
                if (!fileColumn && other->Send(SCI_GETEDGECOLUMN) == previousTabs.longLineColumn)
                    other->Send(SCI_SETEDGECOLUMN, style.longLineColumn);
                if (other->style.longLineColumn == previousTabs.longLineColumn)
                    other->style.longLineColumn = style.longLineColumn;
            }
            if (other->fileVariables.active) {
                if (other->fileVariables.baselineEdge == previousTabs.longLineColumn)
                    other->fileVariables.baselineEdge = style.longLineColumn;
                if (other->fileVariables.baseline.longLineColumn == previousTabs.longLineColumn)
                    other->fileVariables.baseline.longLineColumn = style.longLineColumn;
            }
            other->edgeMode = style.longLineMode;
            other->style.longLineMode = style.longLineMode;
            other->style.longLineEnabled = style.longLineEnabled;
            other->Send(SCI_SETEDGEMODE, style.longLineEnabled ? style.longLineMode : EDGE_NONE);
        }
    }
    if (command == IDM_VIEW_ZOOMIN || command == IDM_VIEW_ZOOMOUT || command == IDM_VIEW_ZOOM_LEVEL || command == IDM_VIEW_RESETZOOM) writeStyle(L"Zoom", static_cast<int>(d.Send(SCI_GETZOOM)));
    if (command == IDM_VIEW_WORDWRAPSETTINGS) frame.settings.Set(IDM_VIEW_WORDWRAP, static_cast<int>(d.Send(SCI_GETWRAPMODE)));
    if (command == IDM_VIEW_STYLE_THEME_DEFAULT || command == IDM_VIEW_STYLE_THEME_DARK) {
        for (auto& other : frame.documents) { other->style.dark = style.dark; ConfigureDocumentStyle(*other); }
    }
    if (command == IDM_VIEW_AUTOCOMPLETION_SETTINGS || command == IDM_VIEW_AUTOCOMPLETION_IGNORECASE || command == IDM_SET_LATEX_INPUT_METHOD) {
        for (auto& other : frame.documents) {
            other->style.completion = style.completion; other->style.completionIgnoreCase = style.completionIgnoreCase;
            other->style.autoIndent = style.autoIndent; other->style.latex = style.latex;
            other->style.completionFillups = style.completionFillups; other->style.completionOptions = style.completionOptions;
            ConfigureDocumentStyle(*other);
        }
    }
    if (command == IDM_VIEW_SCHEME_CONFIG || command == IDM_VIEW_DEFAULT_CODE_FONT || command == IDM_VIEW_DEFAULT_TEXT_FONT) {
        for (auto& other : frame.documents) {
            if (command != IDM_VIEW_SCHEME_CONFIG) {
                other->style.font = style.font; other->style.fontSize = style.fontSize;
                other->style.codeFont = style.codeFont; other->style.textFont = style.textFont;
                other->style.codeFontSize = style.codeFontSize; other->style.textFontSize = style.textFontSize;
            }
            ConfigureDocumentStyle(*other);
        }
    }
    UINT getter = 0;
    switch (command) {
    case IDM_VIEW_WORDWRAP: getter = SCI_GETWRAPMODE; break;
    case IDM_VIEW_SHOWWHITESPACE: getter = SCI_GETVIEWWS; break;
    case IDM_VIEW_SHOWEOLS: getter = SCI_GETVIEWEOL; break;
    case IDM_VIEW_SHOWINDENTGUIDES: getter = SCI_GETINDENTATIONGUIDES; break;
    case IDM_VIEW_WORDWRAPSYMBOLS: getter = SCI_GETWRAPVISUALFLAGS; break;
    case IDM_SET_MULTIPLE_SELECTION: getter = SCI_GETMULTIPLESELECTION; break;
    }
    if (getter) frame.settings.Set(command, static_cast<int>(d.Send(getter)));
}
void SaveFramePreferences(MainFrame& frame) {
    if (frame.settings.persistent) {
        try { SaveStylePreferences(frame.settings.path); SaveEditPreferences(frame.settings.path); }
        catch (const std::exception& error) { frame.Error(error); }
    }
    frame.settings.Set(IDM_VIEW_TOOLBAR, frame.toolbarVisible);
    frame.settings.Set(IDM_VIEW_STATUSBAR, frame.statusVisible);
    frame.settings.Set(IDM_VIEW_MENU, frame.runtime.menuVisible);
    frame.settings.Set(IDM_VIEW_ALWAYSONTOP, (frame.GetExStyle() & WS_EX_TOPMOST) != 0);
    if (!frame.settings.Get(IDM_VIEW_STICKY_WINDOW_POSITION)) {
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (frame.runtime.fullScreen) placement = frame.runtime.placement;
        else frame.GetWindowPlacement(&placement);
        const RECT& r = placement.rcNormalPosition;
        auto text = std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right - r.left) + L"," + std::to_wstring(r.bottom - r.top) + L"," + std::to_wstring(placement.showCmd);
        frame.settings.WriteString(L"Window", L"Placement", text);
    }
    std::wstring buttons;
    const int count = static_cast<int>(::SendMessageW(frame.m_hWndToolBar, TB_BUTTONCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        TBBUTTON button{};
        if (::SendMessageW(frame.m_hWndToolBar, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&button))) {
            if (!buttons.empty()) buttons += L","; buttons += std::to_wstring(button.idCommand);
        }
    }
    frame.settings.WriteString(L"Toolbar", L"Buttons", buttons);
}

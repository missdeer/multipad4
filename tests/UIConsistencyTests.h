#pragma once
#include "../src/MainFrame.h"
#include "../src/StyleManager.h"
#include "../src/UpstreamUI.h"
#include "RuntimeDialogTests.h"
#include <source_location>

// Compare real status controls and notification routes with the pinned
// Notepad4.cpp::UpdateStatusbar / MsgNotify behavior, without desktop input.
inline void RunUIConsistencyTests(MainFrame& frame) {
    struct PreferencesRestore {
        struct Document { EditorDocument* pointer; DocumentStyleState style; FileVariableState variables; LRESULT edgeMode, edgeColumn; };
        MainFrame& frame;
        AppSettings settings;
        DocumentStyleState defaults;
        std::vector<Document> documents;
        explicit PreferencesRestore(MainFrame& owner) : frame(owner), settings(owner.settings), defaults(owner.defaultStyle) {
            for (auto& document : owner.documents)
                documents.push_back({document.get(), document->style, document->fileVariables,
                    document->Send(SCI_GETEDGEMODE), document->Send(SCI_GETEDGECOLUMN)});
        }
        ~PreferencesRestore() {
            frame.settings = settings; frame.defaultStyle = defaults;
            for (const auto& document : documents) {
                document.pointer->style = document.style;
                document.pointer->fileVariables = document.variables;
                document.pointer->Send(SCI_SETEDGEMODE, document.edgeMode);
                document.pointer->Send(SCI_SETEDGECOLUMN, document.edgeColumn);
            }
        }
    } restore(frame);
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("UI consistency assertion at line " + std::to_string(where.line()));
    };
    auto field = [&](int index) {
        wchar_t text[1024]{};
        ::SendMessageW(frame.m_hWndStatusBar, SB_GETTEXTW, index, reinterpret_cast<LPARAM>(text));
        return std::wstring(text);
    };
    auto notify = [&](int index, UINT code) {
        NMMOUSE mouse{};
        mouse.hdr.hwndFrom = frame.m_hWndStatusBar;
        mouse.hdr.code = code;
        mouse.dwItemSpec = index;
        frame.SendMessage(WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&mouse));
    };
    auto* original = frame.Active();
    auto* d = frame.NewDocument(); require(d != nullptr);
    d->style.markOccurrences = 0;
    d->Load({"a中😀\r\nlast"}); d->Send(SCI_SETSEL, 1, 8);
    frame.Refresh();
    require(::SendMessageW(frame.m_hWndStatusBar, SB_GETPARTS, 0, 0) == 13);
    // Upstream passes selected characters first, UTF-8 bytes second.
    require(field(3).find(L"2 / 7") != std::wstring::npos);
    CaptureQAWindow(frame, L"Status-Unicode-selection");
    require(field(4).find(L"1") != std::wstring::npos);
    require(field(6).empty());
    require(field(8) == files::EncodingName(d->encoding));
    wchar_t size[64]{};
    StrFormatByteSizeW(d->Send(SCI_GETLENGTH), size, _countof(size));
    require(field(12) == size);
    require(field(0).find(L"1 / 2") != std::wstring::npos);
    require(field(1).find(L"4 / 3") != std::wstring::npos);
    require(field(2).find(L"4 / 3") != std::wstring::npos);
    require(field(5).find(L"0") != std::wstring::npos);
    require(field(7) == d->language && field(9) == L"CR+LF");
    require(field(11) == std::to_wstring(d->Send(SCI_GETZOOM)) + L"%");
    d->Load({"abc\ndef\n"});
    d->Send(SCI_SETRECTANGULARSELECTIONANCHOR, 0);
    d->Send(SCI_SETRECTANGULARSELECTIONCARET, 6); frame.Refresh();
    require(d->Send(SCI_SELECTIONISRECTANGLE));
    require(field(3).find(L"-- / --") != std::wstring::npos);
    CaptureQAWindow(frame, L"Status-rectangular-selection");
    d->Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    d->Load({std::string(1234, 'a')}); d->Send(SCI_SELECTALL); frame.Refresh();
    wchar_t separator[4]{};
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator, _countof(separator));
    const auto grouped = std::wstring(L"1") + (separator[0] ? separator[0] : L',') + L"234";
    require(field(3).find(grouped + L" / " + grouped) != std::wstring::npos);
    CaptureQAWindow(frame, L"Status-grouped-numbers");
    const auto peerText = original ? original->Text() : std::string{};
    const bool overtype = d->Send(SCI_GETOVERTYPE) != 0;
    notify(10, NM_DBLCLK);
    require((d->Send(SCI_GETOVERTYPE) != 0) != overtype);
    require(field(10) == (overtype ? L"INS" : L"OVR"));
    d->Load({"one\r\ntwo\nthree\r"}); d->Send(SCI_SETEOLMODE, SC_EOL_LF);
    notify(9, NM_CLICK);
    require(d->Text() == "one\ntwo\nthree\n");
    d->Send(SCI_UNDO); require(d->Text() == "one\r\ntwo\nthree\r");
    d->Send(SCI_SETEOLMODE, SC_EOL_CRLF);
    for (int mode : {SC_EOL_LF, SC_EOL_CR, SC_EOL_CRLF}) {
        notify(9, NM_DBLCLK);
        require(d->Send(SCI_GETEOLMODE) == mode);
    }
    d->Send(SCI_SETREADONLY, TRUE);
    const auto readOnlyText = d->Text(); const auto readOnlyMode = d->Send(SCI_GETEOLMODE);
    notify(9, NM_CLICK); notify(9, NM_DBLCLK);
    require(d->Text() == readOnlyText && d->Send(SCI_GETEOLMODE) == readOnlyMode);
    d->Send(SCI_SETREADONLY, FALSE);
    notify(5, NM_DBLCLK);
    require(IsWindow(frame.searchDialog.hwnd) && frame.searchDialog.document == d);
    frame.searchDialog.Close();
    using runtimedialogtest::Run;
    Run(frame, [&] { notify(0, NM_DBLCLK); }, [&](HWND dialog) {
        require(GetDlgItem(dialog, IDC_LINENUM) != nullptr);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    Run(frame, [&] { notify(11, NM_CLICK); }, [&](HWND dialog) {
        require(GetDlgItem(dialog, IDC_ZOOMLEVEL) != nullptr);
        RECT popup{}, owner{}; GetWindowRect(dialog, &popup); GetWindowRect(frame, &owner);
        require(popup.right <= owner.right && owner.right - popup.right < 64);
        require(popup.bottom <= owner.bottom && owner.bottom - popup.bottom < 64);
        SetDlgItemInt(dialog, IDC_ZOOMLEVEL, 175, FALSE);
        ::SendMessageW(dialog, WM_COMMAND, IDYES, 0);
        require(d->Send(SCI_GETZOOM) == 175);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(d->Send(SCI_GETZOOM) == 175);
    Run(frame, [&] { notify(8, NM_DBLCLK); }, [&](HWND dialog) {
        require(GetDlgItem(dialog, IDC_ENCODINGLIST) != nullptr);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(SelectDocumentScheme(*d, L"csv"));
    const auto csv = d->style.csvOptions;
    const auto dialect = d->style.dialect;
    Run(frame, [&] { notify(7, NM_DBLCLK); }, [&](HWND dialog) {
        require(GetDlgItem(dialog, IDC_CSV_DELIMITER_COMMA) != nullptr);
        CheckRadioButton(dialog, IDC_CSV_DELIMITER_COMMA, IDC_CSV_DELIMITER_OTHER, IDC_CSV_DELIMITER_TAB);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(d->style.csvOptions == csv && d->style.dialect == dialect);
    require(SelectDocumentScheme(*d, L"cpp"));
    const auto scheme = d->style.scheme;
    Run(frame, [&] { frame.SendMessage(WM_COMMAND, IDM_LEXER_CSV); }, [&](HWND dialog) {
        require(GetDlgItem(dialog, IDC_CSV_DELIMITER_COMMA) != nullptr);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(d->style.scheme == scheme && d->style.csvOptions == csv);
    // Keyboard-only commands are absent from the menu inventory.
    d->Load({"    indented"}); d->Send(SCI_GOTOPOS, d->Send(SCI_GETLENGTH));
    d->Send(SCI_SETEOLMODE, SC_EOL_LF); d->style.autoIndent = true;
    frame.SendMessage(WM_COMMAND, CMD_CTRLENTER);
    require(d->Text() == "    indented\n" && d->style.autoIndent);
    d->Load({"    indented"}); d->Send(SCI_GOTOPOS, d->Send(SCI_GETLENGTH));
    d->style.autoIndent = false;
    frame.SendMessage(WM_COMMAND, CMD_CTRLENTER);
    require(d->Text() == "    indented\n    " && !d->style.autoIndent);
    d->Send(SCI_SETREADONLY, TRUE);
    const auto beforeNewline = d->Text();
    frame.SendMessage(WM_COMMAND, CMD_CTRLENTER); require(d->Text() == beforeNewline);
    d->Send(SCI_SETREADONLY, FALSE);
    const auto defaults = frame.defaultStyle;
    const auto settings = frame.settings;
    struct SavedEdge { EditorDocument* document; DocumentStyleState style; LRESULT mode, column; };
    std::vector<SavedEdge> edges;
    for (auto& document : frame.documents)
        edges.push_back({document.get(), document->style, document->Send(SCI_GETEDGEMODE), document->Send(SCI_GETEDGECOLUMN)});
    frame.defaultStyle.longLineColumn = 80;
    d->style.longLineColumn = 80; d->Send(SCI_SETEDGECOLUMN, 80); d->Send(SCI_SETEDGEMODE, EDGE_NONE);
    frame.SendMessage(WM_COMMAND, CMD_INCLINELIMIT);
    require(d->Send(SCI_GETEDGEMODE) != EDGE_NONE && d->Send(SCI_GETEDGECOLUMN) == 80);
    frame.SendMessage(WM_COMMAND, CMD_INCLINELIMIT);
    require(d->Send(SCI_GETEDGECOLUMN) == 81);
    frame.SendMessage(WM_COMMAND, CMD_DECLINELIMIT);
    require(d->Send(SCI_GETEDGECOLUMN) == 80);
    frame.defaultStyle.longLineColumn = 4096;
    frame.SendMessage(WM_COMMAND, CMD_INCLINELIMIT); require(d->Send(SCI_GETEDGECOLUMN) == 4096);
    frame.defaultStyle.longLineColumn = 0;
    frame.SendMessage(WM_COMMAND, CMD_DECLINELIMIT); require(d->Send(SCI_GETEDGECOLUMN) == 0);
    frame.defaultStyle = defaults; frame.settings = settings;
    for (const auto& edge : edges) {
        edge.document->style = edge.style;
        edge.document->Send(SCI_SETEDGEMODE, edge.mode); edge.document->Send(SCI_SETEDGECOLUMN, edge.column);
    }
    if (original) require(original->Text() == peerText);
    d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false;
    require(frame.Close(frame.tabs.GetActivePage()));
}

inline void RunUIRoutingTests(MainFrame& frame) {
    auto require = [](bool ok) { if (!ok) throw std::runtime_error("UI routing consistency assertion"); };
    // Match pinned Notepad4.cpp::tbbMainWnd, including non-default buttons.
    constexpr UINT commands[] = {IDM_FILE_NEW, IDM_FILE_OPEN, IDM_FILE_BROWSE, IDM_FILE_SAVE,
        IDM_EDIT_UNDO, IDM_EDIT_REDO, IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE,
        IDM_EDIT_FIND, IDM_EDIT_REPLACE, IDM_VIEW_WORDWRAP, IDM_VIEW_ZOOMIN, IDM_VIEW_ZOOMOUT,
        IDM_VIEW_SCHEME, IDM_VIEW_SCHEME_CONFIG, IDM_FILE_EXIT, IDM_FILE_SAVEAS, IDM_FILE_SAVECOPY,
        IDM_EDIT_DELETE, IDM_FILE_PRINT, IDM_FILE_OPENFAV, IDM_FILE_ADDTOFAV,
        IDM_VIEW_FOLD_ALL, IDM_FILE_LAUNCH, IDM_VIEW_ALWAYSONTOP, IDM_FILE_NEWWINDOW};
    for (size_t i = 0; i < std::size(commands); ++i) {
        auto button = UpstreamToolbarButton(static_cast<int>(i + 1));
        require(NormalizeCommand(button.idCommand) == commands[i]);
        require(frame.State(button.idCommand).supported);
    }
    unsigned acceleratorCount = 0;
    for (UINT resource : {static_cast<UINT>(IDR_MAINWND), static_cast<UINT>(IDR_ACCFINDREPLACE), static_cast<UINT>(IDR_MDIACCEL)}) {
        auto table = LoadAcceleratorsW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(resource));
        const int count = CopyAcceleratorTableW(table, nullptr, 0); require(count > 0);
        std::vector<ACCEL> entries(static_cast<size_t>(count));
        require(CopyAcceleratorTableW(table, entries.data(), count) == count);
        for (const auto& entry : entries) {
            // Modeless search accelerators are handled by SearchDialog,
            // while Ctrl+Tab is intentionally overridden by the MDI table.
            if (resource == IDR_ACCFINDREPLACE) {
                require(entry.cmd >= IDACC_FIND && entry.cmd <= IDACC_SELTOPREV);
                continue;
            }
            if (entry.cmd == CMD_CTRLTAB) continue;
            if (!frame.State(entry.cmd).supported)
                throw std::runtime_error("Unrouted accelerator command " + std::to_string(entry.cmd));
        }
        acceleratorCount += count;
    }
    fprintf(stderr, "UI routing: 27 toolbar buttons and %u accelerator entries checked (search routes and MDI override accounted for)\n", acceleratorCount);
}

inline void RunMDIShortcutTests(MainFrame& frame) {
    auto require = [](bool ok) { if (!ok) throw std::runtime_error("MDI accelerator behavior assertion"); };
    struct KeyboardState {
        BYTE original[256]{};
        KeyboardState() { if (!GetKeyboardState(original)) throw std::runtime_error("Cannot read test-thread keyboard state"); }
        ~KeyboardState() { SetKeyboardState(original); }
    } keyboard;
    auto shortcut = [&](UINT key, bool shift = false) {
        // This changes only the calling test thread's key-state table. It sends
        // no keyboard input to the desktop or any other application.
        BYTE keys[256]{}; keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
        if (shift) keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
        require(SetKeyboardState(keys) != FALSE);
        MSG message{}; message.hwnd = frame.Active()->hwnd;
        message.message = WM_KEYDOWN; message.wParam = key;
        require(frame.PreTranslateMessage(&message) != FALSE);
    };
    const int count = frame.tabs.GetPageCount();
    auto* a = frame.NewDocument(); require(a != nullptr);
    shortcut('N'); auto* b = frame.Active(); require(b && b != a && frame.tabs.GetPageCount() == count + 2);
    shortcut(VK_TAB, true); require(frame.Active() == a);
    shortcut(VK_TAB); require(frame.Active() == b);
    shortcut('W'); require(frame.IsWindow() && frame.Active() == a && frame.tabs.GetPageCount() == count + 1);
    shortcut(VK_F4); require(frame.IsWindow() && frame.tabs.GetPageCount() == count);
}

#pragma once
#include <dlgs.h>
#include "StyleDialogTests.h"
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>

inline void RunMDILayoutTests(MainFrame& frame) {
    std::string diagnostic;
    auto require = [&](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("MDI layout assertion failed at line " + std::to_string(where.line()) + diagnostic);
    };
    require(frame.tabs.GetPageCount() == 2);
    const HWND first = frame.tabs.GetPageHWND(0), second = frame.tabs.GetPageHWND(1);
    const int savedSelection = frame.tabs.GetActivePage();
    const auto rectangle = [](HWND window) { RECT rect{}; ::GetWindowRect(window, &rect); return rect; };
    const auto clientRectangle = [](HWND window) {
        RECT rect{}; ::GetClientRect(window, &rect);
        ::MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&rect), 2); return rect;
    };
    const auto describeRect = [](const RECT& r) {
        return "[" + std::to_string(r.left) + "," + std::to_string(r.top) + "," +
            std::to_string(r.right) + "," + std::to_string(r.bottom) + "]";
    };
    const auto checkTabs = [&] {
        frame.UpdateLayout();
        HWND control = frame.GetMDITabCtrl();
        const RECT tabs = rectangle(control), owner = rectangle(::GetParent(control));
        const RECT client = rectangle(frame.m_hWndMDIClient);
        require((::GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0);
        require(tabs.right > tabs.left && tabs.bottom > tabs.top);
        require(owner.bottom > owner.top && owner.bottom <= client.top);
    };
    const auto checkMaximized = [&](HWND child) {
        frame.MDIMaximize(child); frame.UpdateLayout();
        BOOL maximized = FALSE;
        require(frame.MDIGetActive(&maximized) == child && maximized && ::IsZoomed(child));
        require(frame.tabs.GetPageHWND(frame.tabs.GetActivePage()) == child);
        const RECT mdi = clientRectangle(frame.m_hWndMDIClient), editor = clientRectangle(child);
        require(::EqualRect(&mdi, &editor) != FALSE);
        const auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetActivePage()));
        const RECT scintilla = rectangle(document->hwnd);
        require(::EqualRect(&editor, &scintilla) != FALSE);
        checkTabs();
    };
    auto& control = frame.GetMDITabCtrl();
    const auto savedSettings = frame.settings;
    const auto defaultTabs = rectangle(control);
    const auto editorFontSize = frame.Active()->Send(SCI_STYLEGETSIZE, STYLE_DEFAULT);
    wchar_t menuLabel[64]{};
    require(GetMenuStringW(frame.GetMenu(), IDM_VIEW_TABSETTINGS, menuLabel, _countof(menuLabel), MF_BYCOMMAND) != 0 &&
        std::wstring_view(menuLabel) == L"&Tabs and Indentation...\tCtrl+T");
    require(GetMenuStringW(frame.GetMenu(), ID_TAB_SETTINGS, menuLabel, _countof(menuLabel), MF_BYCOMMAND) != 0 &&
        std::wstring_view(menuLabel) == L"&Document Tab Settings...");
    require(!control.HasTitleFont() && frame.State(ID_TAB_SETTINGS).enabled);
    StyleDialogTest::RunCommand(frame, ID_TAB_SETTINGS, [&](HWND dialog) {
        require(!IsWindowEnabled(GetDlgItem(dialog, IDC_TAB_FONT_RESET)));
        wchar_t sample[64]{};
        GetDlgItemTextW(dialog, IDC_TAB_FONT_PREVIEW, sample, _countof(sample));
        require(std::wstring_view(sample) == L"中文 かな 한글 &Font…");
        const auto timer = SetTimer(dialog, 1, 10, [](HWND owner, UINT, UINT_PTR id, DWORD) {
            const HWND chooser = GetWindow(owner, GW_ENABLEDPOPUP);
            const HWND face = GetDlgItem(chooser, cmb1);
            if (chooser == owner || !face) return;
            KillTimer(owner, id);
            SendMessageW(face, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(L"Courier New"));
            SendMessageW(chooser, WM_COMMAND, MAKEWPARAM(cmb1, CBN_SELCHANGE), reinterpret_cast<LPARAM>(face));
            SetDlgItemInt(chooser, cmb3, DocumentTabStrip::titleMaxPoints, FALSE);
            PostMessageW(chooser, WM_COMMAND, IDOK, 0);
        });
        require(timer != 0);
        SendDlgItemMessageW(dialog, IDC_TAB_FONT_PREVIEW, BM_CLICK, 0, 0);
        KillTimer(dialog, timer);
        LOGFONTW font{};
        const auto preview = SendDlgItemMessageW(dialog, IDC_TAB_FONT_PREVIEW, WM_GETFONT, 0, 0);
        require(GetObjectW(reinterpret_cast<HFONT>(preview), sizeof(font), &font) == sizeof(font));
        require(std::wstring_view(font.lfFaceName) == L"Courier New");
        require(font.lfHeight == -MulDiv(DocumentTabStrip::titleMaxPoints, GetDpiForWindow(dialog), 72));
        require(IsWindowEnabled(GetDlgItem(dialog, IDC_TAB_FONT_RESET)) && !control.HasTitleFont());
    });
    const auto checkTitleFont = [&] {
        const auto handle = reinterpret_cast<HFONT>(control.SendMessage(WM_GETFONT, 0, 0));
        LOGFONTW font{};
        require(GetObjectW(handle, sizeof(font), &font) == sizeof(font));
        require(std::wstring_view(font.lfFaceName) == L"Courier New");
        require(font.lfHeight == -MulDiv(DocumentTabStrip::titleMaxPoints, control.TitleDpi(), 72));
        require(font.lfWeight == FW_NORMAL);
        const auto customTabs = rectangle(control);
        require(customTabs.bottom - customTabs.top >= control.TitleFontHeight());
        require(customTabs.bottom - customTabs.top > defaultTabs.bottom - defaultTabs.top);
        require(frame.Active()->Send(SCI_STYLEGETSIZE, STYLE_DEFAULT) == editorFontSize);
        checkTabs();
    };
    checkTitleFont();
    control.SendMessage(WM_THEMECHANGED);
    checkTitleFont();
    CaptureQAWindow(frame, L"MDI-tab-title-font");
    require(!control.HasFixedTabWidth());
    auto* longTitle = frame.NewDocument();
    require(longTitle != nullptr);
    longTitle->untitled = L"A rather long document title for checking truncation.txt";
    frame.Refresh();
    const int longTab = frame.tabs.GetActivePage();
    require(longTab == 2);
    const auto tabWidth = [&](int index) {
        RECT item{};
        require(control.GetItemRect(index, &item) != FALSE);
        return item.right - item.left;
    };
    const auto autoWidth = tabWidth(longTab);
    const auto checkFixedWidth = [&](int width) {
        require(control.HasFixedTabWidth());
        for (int index = 0; index < frame.tabs.GetPageCount(); ++index)
            require(tabWidth(index) == MulDiv(width, control.TitleDpi(), 96));
    };
    StyleDialogTest::RunCommand(frame, ID_TAB_SETTINGS, [&](HWND dialog) {
        const auto input = GetDlgItem(dialog, IDC_TAB_WIDTH_PIXELS);
        const auto spin = GetDlgItem(dialog, IDC_TAB_WIDTH_SPIN);
        require(!IsWindowEnabled(input) && !IsWindowEnabled(spin));
        require(GetDlgItemInt(dialog, IDC_TAB_WIDTH_PIXELS, nullptr, FALSE) == 100);
        SendDlgItemMessageW(dialog, IDC_TAB_FIXED_WIDTH, BM_CLICK, 0, 0);
        require(IsWindowEnabled(input) && IsWindowEnabled(spin));
        require(reinterpret_cast<HWND>(SendMessageW(spin, UDM_GETBUDDY, 0, 0)) == input);
        int minWidth = 0, maxWidth = 0;
        SendMessageW(spin, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&minWidth), reinterpret_cast<LPARAM>(&maxWidth));
        require(minWidth == DocumentTabStrip::minTabWidth && maxWidth == DocumentTabStrip::maxTabWidth);
        SendMessageW(input, WM_KEYDOWN, VK_UP, 0);
        require(GetDlgItemInt(dialog, IDC_TAB_WIDTH_PIXELS, nullptr, FALSE) == 101);
        SendMessageW(input, WM_KEYDOWN, VK_DOWN, 0);
    });
    checkFixedWidth(100);
    StyleDialogTest::RunCommand(frame, ID_TAB_SETTINGS, [](HWND dialog) {
        SetDlgItemInt(dialog, IDC_TAB_WIDTH_PIXELS, 160, FALSE);
        CaptureQAWindow(dialog, L"Tab-settings");
    });
    checkFixedWidth(160);
    require(autoWidth > tabWidth(longTab) && frame.tabs.GetPageTitle(longTab) == longTitle->untitled);
    CaptureQAWindow(frame, L"MDI-fixed-tab-width");
    StyleDialogTest::RunCommand(frame, ID_TAB_SETTINGS, [](HWND dialog) {
        SendDlgItemMessageW(dialog, IDC_TAB_FONT_RESET, BM_CLICK, 0, 0);
        SetDlgItemInt(dialog, IDC_TAB_WIDTH_PIXELS, 200, FALSE);
    }, IDCANCEL);
    checkFixedWidth(160);
    checkTitleFont();
    AppSettings persisted = frame.settings;
    persisted.path.replace_filename(L"tab-title-font.ini");
    persisted.persistent = true;
    require(persisted.Save());
    StyleDialogTest::RunCommand(frame, ID_TAB_SETTINGS, [&](HWND dialog) {
        SendDlgItemMessageW(dialog, IDC_TAB_FIXED_WIDTH, BM_CLICK, 0, 0);
        require(!IsWindowEnabled(GetDlgItem(dialog, IDC_TAB_WIDTH_PIXELS)));
        require(!IsWindowEnabled(GetDlgItem(dialog, IDC_TAB_WIDTH_SPIN)));
        require(GetDlgItemInt(dialog, IDC_TAB_WIDTH_PIXELS, nullptr, FALSE) == 160);
    });
    require(!control.HasFixedTabWidth() && tabWidth(longTab) == autoWidth);
    CaptureQAWindow(frame, L"MDI-auto-tab-width");
    require(frame.Close(longTab));
    StyleDialogTest::RunCommand(frame, ID_TAB_SETTINGS, [](HWND dialog) {
        SendDlgItemMessageW(dialog, IDC_TAB_FONT_RESET, BM_CLICK, 0, 0);
    });
    require(!control.HasTitleFont());
    const auto resetTabs = rectangle(control);
    require(resetTabs.bottom - resetTabs.top == defaultTabs.bottom - defaultTabs.top);
    frame.settings = {};
    frame.settings.path = persisted.path;
    frame.ApplyTabTitleFont();
    frame.ApplyTabWidth();
    checkFixedWidth(160);
    checkTitleFont();
    frame.settings.WriteString(L"Tab Bar", L"FontSize", L"24pt");
    frame.ApplyTabTitleFont();
    require(!control.HasTitleFont());
    frame.settings = savedSettings;
    frame.ApplyTabTitleFont();
    frame.ApplyTabWidth();
    std::filesystem::remove(persisted.path);

    for (HWND child : {first, second}) {
        const auto style = ::GetWindowLongPtrW(child, GWL_STYLE);
        require((style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
        checkMaximized(child);
    }
    const auto observeRestore = [](HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) -> LRESULT {
        if (message == WM_SIZE && w == SIZE_RESTORED)
            *reinterpret_cast<bool*>(data) = GetPropW(GetParent(window), L"SysSetRedraw") != nullptr;
        return DefSubclassProc(window, message, w, l);
    };
    for (int target : {0, 1}) {
        const HWND outgoing = frame.MDIGetActive();
        bool restoreWithoutRedraw = false;
        require(SetWindowSubclass(outgoing, observeRestore, 0, reinterpret_cast<DWORD_PTR>(&restoreWithoutRedraw)) != FALSE);
        if (target == 0) {
            RECT tab{};
            control.GetItemRect(target, &tab);
            const LPARAM point = MAKELPARAM((tab.left + tab.right) / 2, (tab.top + tab.bottom) / 2);
            control.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, point);
            control.SendMessage(WM_LBUTTONUP, 0, point);
        } else {
            frame.SendMessage(WM_COMMAND, ID_NEXTTAB);
        }
        RemoveWindowSubclass(outgoing, observeRestore, 0);
        require(restoreWithoutRedraw);
        const HWND incoming = frame.tabs.GetPageHWND(target);
        require(frame.MDIGetActive() == incoming && ::IsZoomed(incoming) && frame.tabs.GetActivePage() == target);
        require(!GetPropW(frame.m_hWndMDIClient, L"SysSetRedraw"));
    }
    CaptureQAWindow(frame, L"MDI-after-tab-switch");
    // New documents must inherit a usable maximized MDI layout as well.
    auto* added = frame.NewDocument(); require(added != nullptr);
    checkMaximized(frame.tabs.GetPageHWND(frame.tabs.GetActivePage()));
    require(frame.Close(frame.tabs.GetActivePage()));
    frame.tabs.SetActivePage(0); frame.MDIRestore(first);
    require(!::IsZoomed(first));
    RECT restored = rectangle(first), content = clientRectangle(first);
    require(restored.right > restored.left && restored.bottom > restored.top);
    require(content.top > restored.top && content.left > restored.left);
    for (UINT command : {ID_WINDOW_CASCADE, ID_WINDOW_TILE_VERT, ID_WINDOW_TILE_HORZ}) {
        frame.SendMessage(WM_COMMAND, command);
        require(!::IsZoomed(first) && !::IsZoomed(second));
        const RECT a = rectangle(first), b = rectangle(second);
        RECT overlap{}; ::IntersectRect(&overlap, &a, &b);
        diagnostic = " command=" + std::to_string(command) + " windowA=" + describeRect(a) +
            " windowB=" + describeRect(b) + " overlap=" + describeRect(overlap) +
            " clientA=" + describeRect(clientRectangle(first)) + " clientB=" + describeRect(clientRectangle(second)) +
            " mdi=" + describeRect(clientRectangle(frame.m_hWndMDIClient)) +
            " styleA=" + std::to_string(::GetWindowLongPtrW(first, GWL_STYLE)) +
            " styleB=" + std::to_string(::GetWindowLongPtrW(second, GWL_STYLE));
        fprintf(stderr, "MDI layout:%s\n", diagnostic.c_str());
        CaptureQAWindow(frame, command == ID_WINDOW_CASCADE ? L"MDI-cascade" :
            command == ID_WINDOW_TILE_VERT ? L"MDI-tile-vertical" : L"MDI-tile-horizontal");
        require(a.right > a.left && a.bottom > a.top && b.right > b.left && b.bottom > b.top);
        require(!::EqualRect(&a, &b));
        if (command != ID_WINDOW_CASCADE) {
            require(!::IntersectRect(&overlap, &a, &b));
            RECT extent{}; ::UnionRect(&extent, &a, &b);
            const RECT mdi = clientRectangle(frame.m_hWndMDIClient);
            // Native MDI divides the split dimension into equal integer sizes.
            // An odd client width/height leaves one pixel at its trailing edge;
            // the other dimension must still cover the complete client.
            const LONG count = frame.tabs.GetPageCount();
            require(extent.left == mdi.left && extent.top == mdi.top);
            if (command == ID_WINDOW_TILE_VERT) {
                const LONG width = (mdi.right - mdi.left) / count;
                require(a.right - a.left == width && b.right - b.left == width);
                require(a.top == mdi.top && b.top == mdi.top && a.bottom == mdi.bottom && b.bottom == mdi.bottom);
                require(mdi.right - extent.right >= 0 && mdi.right - extent.right < count);
            } else {
                const LONG height = (mdi.bottom - mdi.top) / count;
                require(a.bottom - a.top == height && b.bottom - b.top == height);
                require(a.left == mdi.left && b.left == mdi.left && a.right == mdi.right && b.right == mdi.right);
                require(mdi.bottom - extent.bottom >= 0 && mdi.bottom - extent.bottom < count);
            }
            for (int i = 0; i < frame.tabs.GetPageCount(); ++i) {
                const RECT content = clientRectangle(frame.tabs.GetPageHWND(i));
                const auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(i));
                const RECT editor = rectangle(document->hwnd);
                require(::EqualRect(&content, &editor) != FALSE);
            }
        }
        frame.tabs.SetActivePage(1);
        require(frame.MDIGetActive() == second && frame.Active() == frame.tabs.GetPageData(1));
        checkTabs();
    }
    // Move a minimized child away from the icon row; Arrange Icons must route
    // to the MDI client and actually return it to the native arrangement.
    ::ShowWindow(first, SW_MINIMIZE);
    require(::IsIconic(first) && !::IsIconic(second));
    RECT mdiLocal{}; ::GetClientRect(frame.m_hWndMDIClient, &mdiLocal);
    ::SetWindowPos(first, nullptr, mdiLocal.right / 2, mdiLocal.bottom / 2, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT movedIcon = rectangle(first);
    frame.SendMessage(WM_COMMAND, ID_WINDOW_ARRANGE);
    const RECT arrangedIcon = rectangle(first), mdi = clientRectangle(frame.m_hWndMDIClient);
    diagnostic = " command=" + std::to_string(ID_WINDOW_ARRANGE) + " moved=" + describeRect(movedIcon) +
        " arranged=" + describeRect(arrangedIcon) + " mdi=" + describeRect(mdi);
    fprintf(stderr, "MDI layout:%s\n", diagnostic.c_str());
    require(::IsIconic(first) && !::EqualRect(&movedIcon, &arrangedIcon));
    require(arrangedIcon.left >= mdi.left && arrangedIcon.top >= mdi.top &&
        arrangedIcon.right <= mdi.right && arrangedIcon.bottom <= mdi.bottom);
    frame.MDIRestore(first); require(!::IsIconic(first));
    // Resize the actual frame and verify maximized children track its client.
    const RECT saved = rectangle(frame);
    checkMaximized(first);
    frame.SetWindowPos(nullptr, 0, 0, saved.right - saved.left + 73, saved.bottom - saved.top + 41,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    checkMaximized(first);
    frame.SetWindowPos(nullptr, 0, 0, MulDiv(900, control.TitleDpi(), 96), saved.bottom - saved.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    frame.tabs.SetActivePage(0);
    control.SetFixedTabWidth(DocumentTabStrip::maxTabWidth);
    frame.UpdateLayout();
    const HWND scroll = FindWindowExW(control, nullptr, UPDOWN_CLASSW, nullptr);
    const HWND close = FindWindowExW(::GetParent(control), nullptr, WC_BUTTONW, nullptr);
    require(scroll && close && (GetWindowLongPtrW(scroll, GWL_STYLE) & WS_VISIBLE));
    const auto scrollRect = rectangle(scroll), closeRect = rectangle(close);
    require(scrollRect.top == closeRect.top && scrollRect.bottom == closeRect.bottom);
    require(scrollRect.right <= closeRect.left);
    const auto scrollPosition = [&] { return SendMessageW(scroll, UDM_GETPOS32, 0, 0); };
    const auto startPosition = scrollPosition();
    const auto scrollClick = [&](bool right) {
        const int offset = (scrollRect.right - scrollRect.left) / 4;
        const LPARAM point = MAKELPARAM(right ? offset * 3 : offset, (scrollRect.bottom - scrollRect.top) / 2);
        SendMessageW(scroll, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(scroll, WM_LBUTTONUP, 0, point);
    };
    scrollClick(true);
    require(scrollPosition() > startPosition && frame.tabs.GetActivePage() == 0);
    CaptureQAWindow(frame, L"MDI-tab-navigation");
    scrollClick(false);
    require(scrollPosition() == startPosition);
    RECT clippedTab{};
    require(control.GetItemRect(0, &clippedTab) != FALSE);
    POINT viewportEdge{scrollRect.left, scrollRect.top};
    ::ScreenToClient(control, &viewportEdge);
    require(clippedTab.right > viewportEdge.x);
    const LPARAM clippedLabel = MAKELPARAM(viewportEdge.x - MulDiv(10, control.TitleDpi(), 96),
        (clippedTab.top + clippedTab.bottom) / 2);
    control.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, clippedLabel);
    control.SendMessage(WM_LBUTTONUP, 0, clippedLabel);
    MSG closeRequest{};
    require(!PeekMessageW(&closeRequest, frame, WM_CLOSE_TAB, WM_CLOSE_TAB, PM_REMOVE));
    SendMessageW(close, BM_CLICK, 0, 0);
    require(PeekMessageW(&closeRequest, frame, WM_CLOSE_TAB, WM_CLOSE_TAB, PM_REMOVE) != FALSE);
    require(reinterpret_cast<HWND>(closeRequest.wParam) == first);
    frame.ApplyTabWidth();
    frame.SetWindowPos(nullptr, &saved, SWP_NOZORDER | SWP_NOACTIVATE);

    frame.tabs.SetActivePage(savedSelection); frame.MDIMaximize(frame.tabs.GetPageHWND(savedSelection));
}
inline void RunPerTabCloseTest(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Per-tab close assertion at line " + std::to_string(where.line()));
    };
    auto& control = frame.GetMDITabCtrl();
    require(frame.tabs.GetPageCount() == 0);
    require(frame.NewDocument() != nullptr);
    const HWND inactive = frame.tabs.GetPageHWND(0);
    require(frame.NewDocument() != nullptr);
    const HWND active = frame.tabs.GetPageHWND(1);
    require(frame.MDIGetActive() == active);
    RECT tab{};
    require(control.GetItemRect(0, &tab) != FALSE);
    const LPARAM button = MAKELPARAM(tab.right - MulDiv(10, control.TitleDpi(), 96), (tab.top + tab.bottom) / 2);
    const LPARAM label = MAKELPARAM(tab.left + 4, (tab.top + tab.bottom) / 2);
    CaptureQAWindow(frame, L"MDI-per-tab-close");
    control.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, button);
    control.SendMessage(WM_LBUTTONUP, 0, label);
    MSG close{};
    require(!PeekMessageW(&close, frame, WM_CLOSE_TAB, WM_CLOSE_TAB, PM_REMOVE));
    require(frame.MDIGetActive() == active);
    control.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, button);
    control.SendMessage(WM_LBUTTONUP, 0, button);
    require(PeekMessageW(&close, frame, WM_CLOSE_TAB, WM_CLOSE_TAB, PM_REMOVE) != FALSE);
    require(reinterpret_cast<HWND>(close.wParam) == inactive && frame.MDIGetActive() == active);
    DispatchMessageW(&close);
    require(frame.tabs.GetPageCount() == 1 && frame.MDIGetActive() == active);
    require(frame.Close(0));
}

inline void RunTabCloseTests(MainFrame& frame, const std::filesystem::path& dir) {
    RunPerTabCloseTest(frame);
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Tab close assertion at line " + std::to_string(where.line()));
    };
    const struct {
        UINT command;
        std::vector<int> remaining;
    } cases[] = {
        {ID_CLOSE, {0, 1, 3, 4}},
        {ID_CLOSE_LEFT_TABS, {2, 3, 4}},
        {ID_CLOSE_RIGHT_TABS, {0, 1, 2}},
        {ID_CLOSE_OTHER_TABS, {2}},
        {ID_CLOSEALL, {}},
    };
    for (const auto& test : cases) {
        require(frame.tabs.GetPageCount() == 0);
        for (int i = 0; i < 5; ++i) require(frame.NewDocument() != nullptr);
        require(frame.GetMDITabCtrl().MoveItem(0, 4));
        std::vector<HWND> remaining;
        for (int i : test.remaining) remaining.push_back(frame.tabs.GetPageHWND(i));
        frame.tabs.SetActivePage(0);
        if (test.command == ID_CLOSE) {
            auto& control = frame.GetMDITabCtrl();
            control.EnsureVisible(2, false, true);
            RECT tab{};
            require(control.GetItemRect(2, &tab) != FALSE);
            const auto rightClick = [&] {
                control.SendMessage(WM_RBUTTONUP, 0, MAKELPARAM((tab.left + tab.right) / 2, (tab.top + tab.bottom) / 2));
                MSG message{};
                require(PeekMessageW(&message, frame, WM_TAB_CONTEXT, WM_TAB_CONTEXT, PM_REMOVE) != FALSE);
                require(reinterpret_cast<HWND>(message.wParam) == frame.tabs.GetPageHWND(2));
                return message;
            };
            MSG message = rightClick();
            if (!qaArtifactDirectory.empty()) {
                struct PopupCapture {
                    unsigned ticks = 0;
                    int count = -1;
                    const wchar_t* label = nullptr;
                };
                static PopupCapture capture;
                const auto inspectPopup = [&](const wchar_t* label) {
                    capture = {0, -1, label};
                    const auto timer = SetTimer(nullptr, 0, 30, [](HWND, UINT, UINT_PTR id, DWORD) {
                        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
                            wchar_t type[16]{};
                            GetClassNameW(window, type, _countof(type));
                            MENUBARINFO info{};
                            info.cbSize = sizeof(info);
                            if (wcscmp(type, L"#32768") != 0 || !IsWindowVisible(window) ||
                                !GetMenuBarInfo(window, OBJID_CLIENT, 0, &info)) return TRUE;
                            capture.count = GetMenuItemCount(info.hMenu);
                            if (capture.label) CaptureQAWindow(window, capture.label);
                            return FALSE;
                        }, 0);
                        if (capture.count >= 0 || ++capture.ticks >= 30) {
                            KillTimer(nullptr, id);
                            EndMenu();
                        }
                    });
                    require(timer != 0);
                    DispatchMessageW(&message);
                    KillTimer(nullptr, timer);
                    return capture.count;
                };
                const int tabCommands = inspectPopup(L"MDI-tab-context-menu");
                require(tabCommands == 5);
                auto* clicked = static_cast<EditorDocument*>(frame.tabs.GetPageData(2));
                clicked->path = dir / L"标签页.txt";
                message = rightClick();
                require(inspectPopup(nullptr) == tabCommands);
                require(frame.Save(*clicked));
                message = rightClick();
                require(inspectPopup(L"MDI-tab-shell-context-menu") > tabCommands + 1);
            }
        }
        frame.CloseTabs(2, test.command);
        require(frame.tabs.GetPageCount() == static_cast<int>(remaining.size()));
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) require(frame.tabs.GetPageHWND(i) == remaining[i]);
        while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1));
    }
}

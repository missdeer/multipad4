#include "SearchDialog.h"
#include "EditSelection.h"
#include "InfoDialog.h"
#include "MainFrame.h"
#include "resource.h"
namespace {
void ShowSearchHelp(HWND window, UINT resource) {
    const wchar_t *value = nullptr;
    const int length = LoadStringW(_Module.GetModuleInstance(), resource, reinterpret_cast<LPWSTR>(&value), 0);
    if (length <= 0)
        return;
    // RT_STRING entries are counted strings, without a terminating NUL.
    const std::wstring text(value, static_cast<size_t>(length));
    const auto title = text.substr(0, text.find(L'\n'));
    MessageBoxW(window, text.c_str(), title.c_str(), MB_OK);
}
std::wstring ResourceText(UINT resource, const wchar_t *fallback) {
    wchar_t value[1024]{};
    return LoadStringW(_Module.GetModuleInstance(), resource, value, 1024) ? std::wstring(value)
                                                                           : std::wstring(fallback);
}
std::wstring Field(HWND window, int id) {
    HWND control = GetDlgItem(window, id);
    int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(length);
    return value;
}
void Remember(std::vector<std::wstring> &history, const std::wstring &text) {
    if (text.empty())
        return;
    std::erase(history, text);
    history.insert(history.begin(), text);
    if (history.size() > 32)
        history.resize(32);
}
void FillHistory(HWND window, int id, const std::vector<std::wstring> &history, const std::wstring &value) {
    for (const auto &item : history)
        SendDlgItemMessageW(window, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    SetDlgItemTextW(window, id, value.c_str());
}
} // namespace
BOOL SearchDialog::PreTranslateMessage(MSG *message) {
    if (!hwnd || (message->hwnd != hwnd && !IsChild(hwnd, message->hwnd)))
        return FALSE;
    if (accelerators && TranslateAcceleratorW(hwnd, accelerators, message))
        return TRUE;
    return IsDialogMessageW(hwnd, message);
}
void SearchDialog::Open(MainFrame &frame, EditorDocument &target, bool replace) {
    if (hwnd && document == &target && replacing == replace) {
        SetForegroundWindow(hwnd);
        return;
    }
    if (hwnd && document && owner &&
        std::any_of(owner->documents.begin(), owner->documents.end(),
                    [&](const auto &item) { return item.get() == document; }))
        ReadFields();
    Close();
    owner = &frame;
    document = &target;
    replacing = replace;
    target.search.closeAfterAction = replace ? target.search.closeAfterReplace : target.search.closeAfterFind;
    if (!accelerators)
        accelerators = LoadAcceleratorsW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDR_ACCFINDREPLACE));
    hwnd = CreateDialogParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(replace ? IDD_REPLACE : IDD_FIND),
                              frame.m_hWnd, Procedure, reinterpret_cast<LPARAM>(this));
    if (hwnd)
        ShowWindow(hwnd, SW_SHOW);
}
void SearchDialog::Close() {
    if (hwnd && IsWindow(hwnd) && document && owner &&
        std::any_of(owner->documents.begin(), owner->documents.end(),
                    [&](const auto &item) { return item.get() == document; })) {
        try {
            ReadFields();
        } catch (...) { /* Destruction must not throw through a Win32 callback. */
        }
    }
    if (hwnd && IsWindow(hwnd))
        DestroyWindow(hwnd);
    hwnd = nullptr;
    document = nullptr;
}
bool SearchDialog::ReadFields() {
    if (!document || !hwnd)
        return false;
    auto &search = document->search;
    auto previous = search.findText;
    const auto previousFlags = search.flags;
    const bool previousRegex = search.regex, previousWildcard = search.wildcard,
               previousTransform = search.transformBackslashes, previousDot = search.dotMatchesNewline,
               previousBoost = search.useBoostRegex;
    search.findText = Field(hwnd, IDC_FINDTEXT);
    if (replacing)
        search.replaceText = Field(hwnd, IDC_REPLACETEXT);
    search.flags = (IsDlgButtonChecked(hwnd, IDC_FINDCASE) ? SCFIND_MATCHCASE : 0) |
                   (IsDlgButtonChecked(hwnd, IDC_FINDWORD) ? SCFIND_WHOLEWORD : 0) |
                   (IsDlgButtonChecked(hwnd, IDC_FINDSTART) ? SCFIND_WORDSTART : 0);
    search.wrap = !IsDlgButtonChecked(hwnd, IDC_NOWRAP);
    search.regex = IsDlgButtonChecked(hwnd, IDC_FINDREGEXP) == BST_CHECKED;
    search.useBoostRegex = IsDlgButtonChecked(hwnd, IDC_FIND_CXXREGEX) == BST_CHECKED;
    search.wildcard = IsDlgButtonChecked(hwnd, IDC_WILDCARDSEARCH) == BST_CHECKED;
    search.transformBackslashes = IsDlgButtonChecked(hwnd, IDC_FINDTRANSFORMBS) == BST_CHECKED;
    search.dotMatchesNewline = IsDlgButtonChecked(hwnd, IDC_FIND_DOTALL) == BST_CHECKED;
    search.closeAfterAction = IsDlgButtonChecked(hwnd, IDC_FINDCLOSE) == BST_CHECKED;
    if (replacing)
        search.closeAfterReplace = search.closeAfterAction;
    else
        search.closeAfterFind = search.closeAfterAction;
    search.bookmarkMatches = IsDlgButtonChecked(hwnd, IDC_FINDALLBOOKMARK) == BST_CHECKED;
    search.useMonospaced = IsDlgButtonChecked(hwnd, IDC_USEMONOSPACEDFONT) == BST_CHECKED;
    search.transparent = IsDlgButtonChecked(hwnd, IDC_TRANSPARENT) == BST_CHECKED;
    if (previous != search.findText || previousFlags != search.flags || previousRegex != search.regex ||
        previousWildcard != search.wildcard || previousTransform != search.transformBackslashes ||
        previousDot != search.dotMatchesNewline || previousBoost != search.useBoostRegex)
        search.lastMatchStart = search.lastMatchEnd = -1;
    Remember(search.findHistory, search.findText);
    if (replacing)
        Remember(search.replaceHistory, search.replaceText);
    return !search.findText.empty();
}
INT_PTR CALLBACK SearchDialog::Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    try {
        auto *dialog = reinterpret_cast<SearchDialog *>(GetWindowLongPtrW(window, DWLP_USER));
        if (message == WM_INITDIALOG) {
            dialog = reinterpret_cast<SearchDialog *>(l);
            dialog->hwnd = window;
            SetWindowLongPtrW(window, DWLP_USER, l);
            const auto &search = dialog->document->search;
            wchar_t caption[256]{};
            GetWindowTextW(window, caption, 256);
            std::wstring title = std::wstring(caption) + L" — " + dialog->document->Title();
            SetWindowTextW(window, title.c_str());
            FillHistory(window, IDC_FINDTEXT, search.findHistory, search.findText);
            FillHistory(window, IDC_REPLACETEXT, search.replaceHistory, search.replaceText);
            auto check = [&](int id, bool value) { CheckDlgButton(window, id, value ? BST_CHECKED : BST_UNCHECKED); };
            check(IDC_FINDCASE, (search.flags & SCFIND_MATCHCASE) != 0);
            check(IDC_FINDWORD, (search.flags & SCFIND_WHOLEWORD) != 0);
            check(IDC_FINDSTART, (search.flags & SCFIND_WORDSTART) != 0);
            check(IDC_NOWRAP, !search.wrap);
            check(IDC_FINDREGEXP, search.regex);
            check(IDC_WILDCARDSEARCH, search.wildcard);
            check(IDC_FINDTRANSFORMBS, search.transformBackslashes);
            check(IDC_FIND_DOTALL, search.dotMatchesNewline);
            check(IDC_FINDCLOSE, search.closeAfterAction);
            check(IDC_FINDALLBOOKMARK, search.bookmarkMatches);
            check(IDC_USEMONOSPACEDFONT, search.useMonospaced);
            check(IDC_TRANSPARENT, search.transparent);
            check(IDC_FIND_CXXREGEX, search.useBoostRegex);
            // The upstream resize helper replaces this template placeholder.
            // Native thick-frame resizing remains available without covering the links.
            ShowWindow(GetDlgItem(window, IDC_RESIZEGRIP2), SW_HIDE);
            dialog->ApplyFont();
            if (dialog->hasSavedPosition)
                SetWindowPos(window, nullptr, dialog->savedPosition.left, dialog->savedPosition.top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER);
            else
                CWindow(window).CenterWindow(dialog->owner->m_hWnd);
            RECT client{};
            GetClientRect(window, &client);
            dialog->layoutWidth = client.right;
            dialog->layoutHeight = client.bottom;
            dialog->controls.clear();
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
                RECT rectangle{};
                GetWindowRect(child, &rectangle);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&rectangle), 2);
                int id = GetDlgCtrlID(child);
                bool stretch = id == IDC_FINDTEXT || id == IDC_REPLACETEXT;
                bool right = rectangle.left > client.right * 2 / 3 && !stretch;
                dialog->controls.push_back({child, rectangle, right, stretch});
            }
            return TRUE;
        }
        if (!dialog)
            return FALSE;
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
            try {
                dialog->ReadFields();
            } catch (const std::exception &error) {
                dialog->owner->Error(error);
            }
            dialog->Close();
            return TRUE;
        }
        if (message == WM_NCDESTROY) {
            if (dialog->monoFont) {
                DeleteObject(dialog->monoFont);
                dialog->monoFont = nullptr;
            }
            dialog->hwnd = nullptr;
            dialog->document = nullptr;
            return FALSE;
        }
        if (message == WM_GETMINMAXINFO && dialog->layoutWidth) {
            auto *bounds = reinterpret_cast<MINMAXINFO *>(l);
            RECT windowRect{}, client{};
            GetWindowRect(window, &windowRect);
            GetClientRect(window, &client);
            bounds->ptMinTrackSize.x = dialog->layoutWidth + (windowRect.right - windowRect.left - client.right);
            bounds->ptMinTrackSize.y = dialog->layoutHeight + (windowRect.bottom - windowRect.top - client.bottom);
            return TRUE;
        }
        if (message == WM_SIZE && dialog->layoutWidth) {
            int delta = LOWORD(l) - dialog->layoutWidth;
            for (const auto &control : dialog->controls) {
                auto rectangle = control.rectangle;
                if (control.right)
                    OffsetRect(&rectangle, delta, 0);
                if (control.stretch)
                    rectangle.right += delta;
                SetWindowPos(control.control, nullptr, rectangle.left, rectangle.top, rectangle.right - rectangle.left,
                             rectangle.bottom - rectangle.top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return TRUE;
        }
        if (message == WM_ACTIVATE && dialog->document && dialog->document->search.transparent) {
            LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
            SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_LAYERED);
            SetLayeredWindowAttributes(window, 0, LOWORD(w) == WA_INACTIVE ? 180 : 255, LWA_ALPHA);
        }
        if (message == WM_NOTIFY) {
            auto *notification = reinterpret_cast<NMHDR *>(l);
            if (notification->code != NM_CLICK && notification->code != NM_RETURN)
                return FALSE;
            switch (notification->idFrom) {
            case IDC_TOGGLEFINDREPLACE: {
                auto *target = dialog->document;
                auto *frame = dialog->owner;
                bool replace = !dialog->replacing;
                dialog->ReadFields();
                dialog->Open(*frame, *target, replace);
                return TRUE;
            }
            case IDC_CLEAR_FIND:
                dialog->document->search.findHistory.clear();
                SendDlgItemMessageW(window, IDC_FINDTEXT, CB_RESETCONTENT, 0, 0);
                return TRUE;
            case IDC_CLEAR_REPLACE:
                dialog->document->search.replaceHistory.clear();
                SendDlgItemMessageW(window, IDC_REPLACETEXT, CB_RESETCONTENT, 0, 0);
                return TRUE;
            case IDC_SAVEPOSITION:
                GetWindowRect(window, &dialog->savedPosition);
                dialog->hasSavedPosition = true;
                return TRUE;
            case IDC_RESETPOSITION:
                dialog->hasSavedPosition = false;
                CWindow(window).CenterWindow(dialog->owner->m_hWnd);
                return TRUE;
            case IDC_REGEXPHELP:
                ShowSearchHelp(window, IDS_REGEXPHELP);
                return TRUE;
            case IDC_BACKSLASHHELP:
                ShowSearchHelp(window, IDS_BACKSLASHHELP);
                return TRUE;
            case IDC_WILDCARDHELP:
                ShowSearchHelp(window, IDS_WILDCARDHELP);
                return TRUE;
            }
        }
        if (message != WM_COMMAND)
            return FALSE;
        WORD id = LOWORD(w);
        bool extendSelection = false;
        switch (id) {
        case IDACC_FIND:
        case IDACC_REPLACE: {
            auto *target = dialog->document;
            auto *owner = dialog->owner;
            dialog->ReadFields();
            dialog->Open(*owner, *target, id == IDACC_REPLACE);
            return TRUE;
        }
        case IDACC_FINDNEXT:
            id = IDOK;
            break;
        case IDACC_FINDPREV:
            id = IDC_FINDPREV;
            break;
        case IDACC_REPLACENEXT:
            if (!dialog->replacing)
                return TRUE;
            id = IDC_REPLACE;
            break;
        case IDACC_SELTONEXT:
            id = IDOK;
            extendSelection = true;
            break;
        case IDACC_SELTOPREV:
            id = IDC_FINDPREV;
            extendSelection = true;
            break;
        case IDACC_SAVEFIND: {
            if (!dialog->document)
                return TRUE;
            ExecuteEditorCommand(*dialog->document, IDM_EDIT_SAVEFIND);
            SetDlgItemTextW(window, IDC_FINDTEXT, dialog->document->search.findText.c_str());
            CheckDlgButton(window, IDC_FINDREGEXP, BST_UNCHECKED);
            CheckDlgButton(window, IDC_FINDTRANSFORMBS, BST_UNCHECKED);
            SetFocus(GetDlgItem(window, IDC_FINDTEXT));
            return TRUE;
        }
        }
        if ((id == IDOK || id == IDC_FINDPREV) && (GetKeyState(VK_SHIFT) & 0x8000))
            extendSelection = true;
        if (id == IDC_TRANSPARENT) {
            dialog->ReadFields();
            if (!dialog->document->search.transparent) {
                auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
                SetWindowLongPtrW(window, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
                RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            return TRUE;
        }
        if (id == IDC_USEMONOSPACEDFONT) {
            dialog->ReadFields();
            dialog->ApplyFont();
            return TRUE;
        }
        if (id == IDC_FINDREGEXP && IsDlgButtonChecked(window, id))
            CheckDlgButton(window, IDC_WILDCARDSEARCH, BST_UNCHECKED);
        if (id == IDC_WILDCARDSEARCH && IsDlgButtonChecked(window, id))
            CheckDlgButton(window, IDC_FINDREGEXP, BST_UNCHECKED);
        if (id != IDOK && id != IDC_FINDPREV && id != IDC_REPLACE && id != IDC_REPLACEALL && id != IDC_REPLACEINSEL &&
            id != IDC_FINDALL)
            return FALSE;
        if (!dialog->document || std::none_of(dialog->owner->documents.begin(), dialog->owner->documents.end(),
                                              [&](const auto &d) { return d.get() == dialog->document; }))
            return TRUE;
        try {
            if (!dialog->ReadFields()) {
                ShowInfoDialog(window, dialog->owner->settings, ResourceText(IDS_NOTFOUND, L"Cannot find the text."),
                               L"Find Text", MB_OK | MB_ICONWARNING, L"MsgNotFound");
                return TRUE;
            }
            auto &target = *dialog->document;
            if (id == IDC_FINDALL || (id == IDC_REPLACEALL && !dialog->replacing)) {
                if (!target.FindAll(id == IDC_REPLACEALL, target.search.bookmarkMatches))
                    ShowInfoDialog(window, dialog->owner->settings,
                                   ResourceText(IDS_NOTFOUND, L"Cannot find the text."), L"Find Text",
                                   MB_OK | MB_ICONWARNING, L"MsgNotFound");
            } else if (id == IDC_REPLACEALL || id == IDC_REPLACEINSEL) {
                if (id == IDC_REPLACEINSEL && RejectRectangularEdit(target))
                    return TRUE;
                const auto count = target.ReplaceAll(id == IDC_REPLACEINSEL);
                auto result = ResourceText(IDS_REPLCOUNT, L"%s occurrence(s) replaced.");
                if (auto placeholder = result.find(L"%s"); placeholder != std::wstring::npos)
                    result.replace(placeholder, 2, std::to_wstring(count));
                ShowInfoDialog(window, dialog->owner->settings,
                               count ? result : ResourceText(IDS_NOTFOUND, L"Cannot find the text."), L"Replace Text",
                               MB_OK | (count ? MB_ICONINFORMATION : MB_ICONWARNING),
                               count ? L"MsgReplaceCount" : L"MsgNotFound");
            } else {
                bool replaced = id == IDC_REPLACE && dialog->replacing && target.ReplaceOne();
                bool cancelled = false;
                auto confirmWrap = [&](bool backwards) {
                    wchar_t message[512]{};
                    LoadStringW(_Module.GetModuleInstance(), backwards ? IDS_FIND_WRAPRE : IDS_FIND_WRAPFW, message,
                                512);
                    const auto result =
                        ShowInfoDialog(window, dialog->owner->settings, message, L"Find Text",
                                       MB_OKCANCEL | MB_ICONINFORMATION, backwards ? L"MsgFindWrap2" : L"MsgFindWrap1");
                    cancelled = result != IDOK;
                    return !cancelled;
                };
                const bool backwards = id == IDC_FINDPREV;
                auto oldAnchor = target.Send(SCI_GETANCHOR), oldCaret = target.Send(SCI_GETCURRENTPOS);
                struct RestoreWrap {
                    bool &value;
                    bool old;
                    ~RestoreWrap() {
                        value = old;
                    }
                } restore{target.search.wrap, target.search.wrap};
                if (extendSelection)
                    target.search.wrap = false;
                const bool found = target.FindNext(backwards, confirmWrap);
                if (found && extendSelection)
                    target.Send(SCI_SETSEL, backwards ? max(oldAnchor, oldCaret) : min(oldAnchor, oldCaret),
                                target.Send(backwards ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND));
                if (!found && !replaced && !cancelled)
                    ShowInfoDialog(window, dialog->owner->settings,
                                   ResourceText(IDS_NOTFOUND, L"Cannot find the text."), L"Find Text",
                                   MB_OK | MB_ICONWARNING, L"MsgNotFound");
            }
            dialog->owner->Refresh();
            if (target.search.closeAfterAction && !(dialog->replacing && id == IDOK))
                dialog->Close();
        } catch (const std::exception &error) {
            dialog->owner->Error(error);
        }
        return TRUE;
    } catch (const std::exception &error) {
        auto *dialog = reinterpret_cast<SearchDialog *>(GetWindowLongPtrW(window, DWLP_USER));
        if (dialog && dialog->owner)
            dialog->owner->Error(error);
        return TRUE;
    }
}
void SearchDialog::ApplyFont() {
    if (!hwnd || !document)
        return;
    if (!monoFont) {
        LOGFONTW font{};
        font.lfHeight = -MulDiv(10, GetDpiForWindow(hwnd), 72);
        wcscpy_s(font.lfFaceName, L"Consolas");
        monoFont = CreateFontIndirectW(&font);
    }
    auto font =
        document->search.useMonospaced ? monoFont : reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    for (int id : {IDC_FINDTEXT, IDC_REPLACETEXT})
        SendDlgItemMessageW(hwnd, id, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

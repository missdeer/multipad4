// MDI adaptations of Goto, Zoom and Long Lines callbacks from the pinned
// notepad4 Edit.cpp / Dialogs.cpp. Upstream copyright/license: THIRD_PARTY.md.
#include "EditorDialogs.h"
#include "resource.h"
#include <cerrno>
namespace {
struct DialogContext { EditorDocument& document; UINT resource; bool bottomRight; };
bool Number(HWND window, int control, long long& value, bool allowEmpty = false) {
    wchar_t text[64]{}; GetDlgItemTextW(window, control, text, 64);
    if (!text[0] && allowEmpty) { value = 0; return true; }
    wchar_t* end = nullptr; errno = 0; value = wcstoll(text, &end, 10);
    if (end == text || *end || errno == ERANGE) {
        SetFocus(GetDlgItem(window, control)); MessageBeep(MB_ICONWARNING); return false;
    }
    return true;
}
INT_PTR CALLBACK EditorProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    try {
    auto* context = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<DialogContext*>(l); SetWindowLongPtrW(window, DWLP_USER, l);
        auto& d = context->document;
        switch (context->resource) {
        case IDD_LINENUM: {
            auto line = d.Send(SCI_LINEFROMPOSITION, d.Send(SCI_GETCURRENTPOS));
            SetDlgItemInt(window, IDC_LINENUM, static_cast<UINT>(line + 1), FALSE);
            SetDlgItemInt(window, IDC_COLNUM, static_cast<UINT>(d.Send(SCI_GETCOLUMN, d.Send(SCI_GETCURRENTPOS)) + 1), FALSE);
            auto label = L"&Line (empty or 1 - " + std::to_wstring(d.Send(SCI_GETLINECOUNT)) + L"):";
            SetDlgItemTextW(window, IDC_LINE_RANGE, label.c_str());
            label = L"&Column / position (1 - " + std::to_wstring(d.Send(SCI_GETLENGTH) + 1) + L"):";
            SetDlgItemTextW(window, IDC_COLUMN_RANGE, label.c_str()); break;
        }
        case IDD_ZOOMLEVEL:
            for (int zoom : {50, 75, 100, 125, 150, 175, 200, 250, 300, 400, 500}) {
                const auto text = std::to_wstring(zoom);
                SendDlgItemMessageW(window, IDC_ZOOMLEVEL, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            }
            SetDlgItemInt(window, IDC_ZOOMLEVEL, static_cast<UINT>(d.Send(SCI_GETZOOM)), FALSE); break;
        case IDD_LONGLINES:
            SetDlgItemInt(window, IDC_LONGLINE_LIMIT, static_cast<UINT>(d.Send(SCI_GETEDGECOLUMN)), FALSE);
            CheckRadioButton(window, IDC_LONGLINE_EDGE_LINE, IDC_LONGLINE_BACK_COLOR,
                d.edgeMode == EDGE_LINE ? IDC_LONGLINE_EDGE_LINE : IDC_LONGLINE_BACK_COLOR); break;
        }
        if (context->bottomRight) {
            // Pinned Helpers.cpp::SetToRightBottomEx, used by status-bar zoom.
            RECT dialog{}, parent{}, client{};
            GetWindowRect(window, &dialog); GetWindowRect(GetParent(window), &parent);
            GetClientRect(GetParent(window), &client);
            const int x = parent.right - (dialog.right - dialog.left) - ((parent.right - parent.left) - client.right) / 2;
            const int y = parent.bottom - (dialog.bottom - dialog.top) - ((parent.bottom - parent.top) - client.bottom) / 2;
            SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        } else CWindow(window).CenterWindow(GetParent(window));
        return TRUE;
    }
    if (!context) return FALSE;
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) { EndDialog(window, IDCANCEL); return TRUE; }
    if (message != WM_COMMAND || (LOWORD(w) != IDOK && LOWORD(w) != IDYES)) return FALSE;
    auto& d = context->document;
    long long value = 0;
    switch (context->resource) {
    case IDD_LINENUM: {
        long long column = 0;
        if (!Number(window, IDC_LINENUM, value, true) || !Number(window, IDC_COLNUM, column, true)) return TRUE;
        LRESULT position = 0;
        if (value == 0 && column > 0 && column <= d.Send(SCI_GETLENGTH) + 1) {
            position = d.Send(SCI_POSITIONBEFORE, column);
        } else if (value > 0 && value <= d.Send(SCI_GETLINECOUNT) && column >= 0) {
            position = d.Send(SCI_FINDCOLUMN, value - 1, max(0LL, column - 1));
        } else { MessageBeep(MB_ICONWARNING); return TRUE; }
        d.Send(SCI_GOTOPOS, position); d.Send(SCI_SCROLLCARET); break;
    }
    case IDD_ZOOMLEVEL:
        if (!Number(window, IDC_ZOOMLEVEL, value) || value < 10 || value > 500) { MessageBeep(MB_ICONWARNING); return TRUE; }
        d.Send(SCI_SETZOOM, value); break;
    case IDD_LONGLINES:
        if (!Number(window, IDC_LONGLINE_LIMIT, value) || value < 0 || value > 1000000) { MessageBeep(MB_ICONWARNING); return TRUE; }
        d.Send(SCI_SETEDGECOLUMN, value);
        d.edgeMode = IsDlgButtonChecked(window, IDC_LONGLINE_EDGE_LINE) ? EDGE_LINE : EDGE_BACKGROUND;
        d.style.longLineColumn=static_cast<int>(value);d.style.longLineMode=d.edgeMode;d.style.longLineEnabled=true;d.Send(SCI_SETEDGEMODE,d.edgeMode);
        break;
    }
    if (LOWORD(w) == IDOK) EndDialog(window, IDOK);
    return TRUE;
    }catch(const std::exception& error){OutputDebugStringA(error.what());MessageBeep(MB_ICONERROR);return TRUE;}
}
const std::wstring BuildDetails = [] {
    wchar_t compiler[128]{};
#if defined(__INTEL_LLVM_COMPILER)
    swprintf_s(compiler, L"Intel oneAPI %d.%d.%d", __INTEL_LLVM_COMPILER / 10000,
        (__INTEL_LLVM_COMPILER / 100) % 100, __INTEL_LLVM_COMPILER % 100);
#elif defined(__clang__)
    swprintf_s(compiler, L"Clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#else
    swprintf_s(compiler, L"Visual C++ %d.%02d.%05d.%d", _MSC_VER / 100,
        _MSC_VER % 100, _MSC_FULL_VER % 100000, _MSC_BUILD);
#endif
    return std::wstring(L"Built with ") + compiler + L"\r\nWindows / C++20 / WTL / Scintilla";
}();
const std::wstring BuildInfo = L"MultiPad4 0.1\r\n" + BuildDetails;
INT_PTR CALLBACK AboutProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) {
        SetWindowTextW(window, L"About MultiPad4");
        SetDlgItemTextW(window, IDC_VERSION, L"MultiPad4 0.1");
        SetDlgItemTextW(window, IDC_BUILD_INFO, BuildDetails.c_str());
        SetDlgItemTextW(window, IDC_NEW_PAGE_LINK, L"<a href=\"https://github.com/zufuliu/notepad4\">notepad4 upstream</a> | <a href=\"https://github.com/missdeer/multipad4\">MultiPad4</a>");
        SetDlgItemTextW(window, IDC_MOD_PAGE_LINK, L"<a href=\"https://github.com/XhmikosR/notepad2-mod\">Notepad2-mod</a>");
        SetDlgItemTextW(window, IDC_WEBPAGE_LINK, L"<a href=\"https://www.flos-freeware.ch\">www.flos-freeware.ch</a>");
        SetDlgItemTextW(window, IDC_EMAIL_LINK, L"florian.balmer@gmail.com");
        SetDlgItemTextW(window, IDC_SCI_PAGE_LINK, L"<a href=\"https://www.scintilla.org\">www.scintilla.org</a>");
        CWindow(window).CenterWindow(GetParent(window)); return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL)) { EndDialog(window, LOWORD(w)); return TRUE; }
    if (message == WM_COMMAND && LOWORD(w) == IDC_COPY_BUILD_INFO) {
        if (OpenClipboard(window)) {
            const size_t bytes = (BuildInfo.size() + 1) * sizeof(wchar_t);
            auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (memory) {
                auto* data = GlobalLock(memory);
                if (data) { memcpy(data, BuildInfo.c_str(), bytes); GlobalUnlock(memory);
                    EmptyClipboard(); if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
                } else GlobalFree(memory);
            }
            CloseClipboard();
        }
        return TRUE;
    }
    if (message == WM_NOTIFY) {
        auto* notification = reinterpret_cast<NMHDR*>(l);
        if (notification->code == NM_CLICK || notification->code == NM_RETURN) {
            const auto* link = reinterpret_cast<NMLINK*>(l);
            ShellExecuteW(window, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL); return TRUE;
        }
    }
    return FALSE;
}
}
INT_PTR ShowStyleSettingsDialog(HWND,EditorDocument&,UINT);
INT_PTR ShowEditorDialog(HWND owner, EditorDocument& document, UINT resource, bool bottomRight) {
    if(resource==IDD_TABSETTINGS || resource==IDD_WORDWRAP || resource==IDD_AUTOCOMPLETION || resource==IDD_CSV_OPTIONS)return ShowStyleSettingsDialog(owner,document,resource);
    DialogContext context{document, resource, bottomRight};
    return DialogBoxParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(resource), owner, EditorProcedure, reinterpret_cast<LPARAM>(&context));
}
void ShowAboutDialog(HWND owner) {
    DialogBoxParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutProcedure, 0);
}

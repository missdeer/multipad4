// Adapted from notepad4 Dialogs.cpp::InfoBoxDlgProc; explicit settings owner.
#include "App.h"
#include "resource.h"
#include "AppSettings.h"
#include "InfoDialog.h"
namespace {
struct Context { AppSettings& settings; const std::wstring& message; const wchar_t* title; UINT type; const wchar_t* key; int mode; };
INT_PTR CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<Context*>(l); SetWindowLongPtrW(window, DWLP_USER, l);
        SetWindowTextW(window, context->title); SetDlgItemTextW(window, IDC_INFOBOXTEXT, context->message.c_str());
        UINT type = context->type & MB_TYPEMASK;
        bool yesNo = type == MB_YESNO;
        ShowWindow(GetDlgItem(window, IDYES), yesNo ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(window, IDNO), yesNo ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(window, IDOK), yesNo ? SW_HIDE : SW_SHOW);
        ShowWindow(GetDlgItem(window, IDCANCEL), type == MB_OKCANCEL ? SW_SHOW : SW_HIDE);
        if (type == MB_OK) {
            RECT right{}; GetWindowRect(GetDlgItem(window, IDCANCEL), &right); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&right), 2);
            SetWindowPos(GetDlgItem(window, IDOK), nullptr, right.left, right.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        EnableWindow(GetDlgItem(window, IDC_INFOBOXCHECK), context->key[0] && context->mode != 2);
        UINT iconType = context->type & MB_ICONMASK;
        const wchar_t* icon = iconType == MB_ICONQUESTION ? IDI_QUESTION : iconType == MB_ICONINFORMATION ? IDI_INFORMATION : iconType == MB_ICONERROR ? IDI_ERROR : IDI_EXCLAMATION;
        SendDlgItemMessageW(window, IDC_INFOBOXICON, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(nullptr, icon)), 0);
        SendMessageW(window, DM_SETDEFID, yesNo ? IDYES : IDOK, 0);
        // The original fixed template has three lines. Grow the message area
        // for long paths/errors while moving its controls as one bottom row.
        HWND label = GetDlgItem(window, IDC_INFOBOXTEXT); RECT textRect{}; GetClientRect(label, &textRect);
        HDC dc = GetDC(label); auto font = reinterpret_cast<HFONT>(SendMessageW(label, WM_GETFONT, 0, 0));
        auto old = font ? SelectObject(dc, font) : nullptr;
        RECT measured = textRect; DrawTextW(dc, context->message.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        if (old) SelectObject(dc, old); ReleaseDC(label, dc);
        int extra = max(0L, measured.bottom - textRect.bottom);
        if (extra) {
            RECT client{}; GetWindowRect(window, &client); SetWindowPos(window, nullptr, 0, 0, client.right - client.left, client.bottom - client.top + extra, SWP_NOMOVE | SWP_NOZORDER);
            for (int id : {IDC_INFOBOXCHECK, IDYES, IDNO, IDOK, IDCANCEL}) {
                HWND control = GetDlgItem(window, id); RECT r{}; GetWindowRect(control, &r); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2);
                SetWindowPos(control, nullptr, r.left, r.top + extra, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            GetWindowRect(label, &textRect); SetWindowPos(label, nullptr, 0, 0, textRect.right - textRect.left, textRect.bottom - textRect.top + extra, SWP_NOMOVE | SWP_NOZORDER);
        }
        CWindow(window).CenterWindow(GetParent(window)); return TRUE;
    }
    if (!context) return FALSE;
    if (message == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL || LOWORD(w) == IDYES || LOWORD(w) == IDNO)) {
        if (context->key[0] && context->mode != 2 && IsDlgButtonChecked(window, IDC_INFOBOXCHECK))
            context->settings.WriteString(L"Suppressed Messages", context->key, LOWORD(w) == IDOK || LOWORD(w) == IDYES ? L"1" : L"3");
        EndDialog(window, LOWORD(w)); return TRUE;
    }
    if (message == WM_CLOSE) { EndDialog(window, (context->type & MB_TYPEMASK) == MB_YESNO ? IDNO : IDCANCEL); return TRUE; }
    return FALSE;
}
}
INT_PTR ShowInfoDialog(HWND owner, AppSettings& settings, const std::wstring& message, const wchar_t* title, UINT type, const wchar_t* suppressionKey) {
    int mode = suppressionKey[0] ? _wtoi(settings.ReadString(L"Suppressed Messages", suppressionKey, L"0").c_str()) : 2;
    if (mode == 1) return (type & MB_TYPEMASK) == MB_YESNO ? IDYES : IDOK;
    if (mode == 3) return (type & MB_TYPEMASK) == MB_YESNO ? IDNO : IDCANCEL;
    Context context{settings, message, title, type, suppressionKey, mode};
    return DialogBoxParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDD_INFOBOX), owner, Procedure, reinterpret_cast<LPARAM>(&context));
}

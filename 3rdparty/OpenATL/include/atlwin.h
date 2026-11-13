// OpenATL - Clean-room ATL subset for WTL 10.0
// Window management classes

#ifndef __ATLWIN_H__
#define __ATLWIN_H__

#pragma once

#include "atlbase.h"

#include <commctrl.h>
#include <olectl.h>
#include <shellapi.h>
#include <errno.h>

// WM_FORWARDMSG - used by WTL for message forwarding
#ifndef WM_FORWARDMSG
#define WM_FORWARDMSG 0x037F
#endif

// Missing property sheet constants (Vista+)
#ifndef PSH_AEROWIZARD
#define PSH_AEROWIZARD 0x00004000
#endif
#ifndef PSH_RESIZABLE
#define PSH_RESIZABLE 0x04000000
#endif
#ifndef PSH_NOMARGIN
#define PSH_NOMARGIN 0x08000000
#endif
#ifndef PSH_HEADERBITMAP
#define PSH_HEADERBITMAP 0x08000000
#endif
#ifndef PSM_ENABLEWIZBUTTONS
#define PSM_ENABLEWIZBUTTONS (WM_USER + 163)
#endif
#ifndef PSM_SETBUTTONTEXT
#define PSM_SETBUTTONTEXT (WM_USER + 164)
#endif
#ifndef PSM_SETNEXTTEXT
#define PSM_SETNEXTTEXT (WM_USER + 165)
#endif
#ifndef PSM_SHOWWIZBUTTONS
#define PSM_SHOWWIZBUTTONS (WM_USER + 166)
#endif

// Missing syslink control message
#ifndef LM_GETIDEALSIZE
#define LM_GETIDEALSIZE (WM_USER + 0x301)
#endif

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// _U_RECT / _U_MENUorID - union parameter wrappers

struct _U_RECT {
    LPRECT m_lpRect;
    _U_RECT(LPRECT lpRect) noexcept : m_lpRect(lpRect) {}
    _U_RECT(RECT& rc) noexcept : m_lpRect(&rc) {}
};

struct _U_MENUorID {
    HMENU m_hMenu;
    _U_MENUorID(HMENU hMenu) noexcept : m_hMenu(hMenu) {}
    _U_MENUorID(UINT nID) noexcept : m_hMenu((HMENU)(UINT_PTR)nID) {}
};

struct _U_STRINGorID {
    LPCTSTR m_lpstr;
    _U_STRINGorID(LPCTSTR lpStr) noexcept : m_lpstr(lpStr) {}
    _U_STRINGorID(UINT nID) noexcept : m_lpstr(MAKEINTRESOURCE(nID)) {}
};

///////////////////////////////////////////////////////////////////////////////
// _ATL_MSG - extended MSG with bHandled

struct _ATL_MSG : public MSG {
    BOOL bHandled;

    _ATL_MSG() noexcept : bHandled(TRUE)
    {
        memset(static_cast<MSG*>(this), 0, sizeof(MSG));
    }

    _ATL_MSG(HWND hWnd, UINT uMsg, WPARAM wParamIn, LPARAM lParamIn, BOOL bHandledIn = TRUE) noexcept
        : bHandled(bHandledIn)
    {
        this->hwnd = hWnd;
        this->message = uMsg;
        this->wParam = wParamIn;
        this->lParam = lParamIn;
        this->time = 0;
        this->pt.x = this->pt.y = 0;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CWinTraits / CWinTraitsOR

template <DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
class CWinTraits {
public:
    static DWORD GetWndStyle(DWORD dwStyle) noexcept
    {
        return (dwStyle == 0) ? t_dwStyle : dwStyle;
    }
    static DWORD GetWndExStyle(DWORD dwExStyle) noexcept
    {
        return (dwExStyle == 0) ? t_dwExStyle : dwExStyle;
    }
};

template <DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0, class TWinTraits = CWinTraits<0, 0>>
class CWinTraitsOR {
public:
    static DWORD GetWndStyle(DWORD dwStyle) noexcept
    {
        return dwStyle | t_dwStyle | TWinTraits::GetWndStyle(dwStyle);
    }
    static DWORD GetWndExStyle(DWORD dwExStyle) noexcept
    {
        return dwExStyle | t_dwExStyle | TWinTraits::GetWndExStyle(dwExStyle);
    }
};

typedef CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0> CControlWinTraits;
typedef CWinTraits<WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_APPWINDOW | WS_EX_WINDOWEDGE> CFrameWinTraits;
typedef CWinTraits<WS_OVERLAPPEDWINDOW | WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_MDICHILD> CMDIChildWinTraits;

///////////////////////////////////////////////////////////////////////////////
// CMessageMap - abstract base for message processing

class ATL_NO_VTABLE CMessageMap {
public:
    virtual BOOL ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult, DWORD dwMsgMapID) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// CWndProcThunk - executable thunk for window procedures
//
// Thunk converts HWND in the first parameter to a 'this' pointer
// so that the static WindowProc can recover the object.

#pragma pack(push, 1)

struct CWndProcThunk {
    _AtlCreateWndData cd;

#if defined(_M_AMD64) || defined(__x86_64__) || defined(__amd64__)
    // x86_64 thunk:
    // mov rcx, this       ; 48 B9 <8-byte this>
    // mov rax, proc       ; 48 B8 <8-byte proc>
    // jmp rax             ; FF E0
    struct ThunkCode {
        BYTE  mov_rcx[2];    // 48 B9
        ULONG_PTR pThis;     // 8-byte this pointer
        BYTE  mov_rax[2];    // 48 B8
        ULONG_PTR pProc;     // 8-byte proc address
        BYTE  jmp_rax[2];    // FF E0
    };
#elif defined(_M_IX86) || defined(__i386__)
    // x86 thunk:
    // mov dword ptr [esp+4], this  ; C7 44 24 04 <4-byte this>
    // jmp proc                     ; E9 <4-byte relative>
    struct ThunkCode {
        BYTE  mov_esp4[4];    // C7 44 24 04
        DWORD pThis;          // 4-byte this pointer
        BYTE  jmp;            // E9
        DWORD relProc;        // 4-byte relative offset
    };
#elif defined(_M_ARM64) || defined(__aarch64__)
    // AArch64 thunk:
    // ldr x0,  [pc, #8]   ; 58000040
    // ldr x16, [pc, #8]   ; 58000050
    // br x16              ; D61F0200
    // <8-byte this>
    // <8-byte proc>
    struct ThunkCode {
        DWORD ldr_x0;        // 58000040 - ldr x0, [pc, #8]
        DWORD ldr_x16;       // 58000050 - ldr x16, [pc, #8]  (adjusted)
        DWORD br_x16;        // D61F0200
        ULONG_PTR pThis;
        ULONG_PTR pProc;
    };
#else
#error "Unsupported architecture for CWndProcThunk"
#endif

    ThunkCode* pThunkCode;

    BOOL Init(WNDPROC proc, void* pThis) noexcept
    {
        // Allocate executable memory
        if (pThunkCode == NULL) {
            pThunkCode = (ThunkCode*)::VirtualAlloc(NULL, sizeof(ThunkCode),
                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (pThunkCode == NULL)
                return FALSE;
        }

        // Allow writing to executable memory
        DWORD dwOldProtect = 0;
        ::VirtualProtect(pThunkCode, sizeof(ThunkCode), PAGE_EXECUTE_READWRITE, &dwOldProtect);

#if defined(_M_AMD64) || defined(__x86_64__) || defined(__amd64__)
        pThunkCode->mov_rcx[0] = 0x48;
        pThunkCode->mov_rcx[1] = 0xB9;
        pThunkCode->pThis = (ULONG_PTR)pThis;
        pThunkCode->mov_rax[0] = 0x48;
        pThunkCode->mov_rax[1] = 0xB8;
        pThunkCode->pProc = (ULONG_PTR)proc;
        pThunkCode->jmp_rax[0] = 0xFF;
        pThunkCode->jmp_rax[1] = 0xE0;
#elif defined(_M_IX86) || defined(__i386__)
        pThunkCode->mov_esp4[0] = 0xC7;
        pThunkCode->mov_esp4[1] = 0x44;
        pThunkCode->mov_esp4[2] = 0x24;
        pThunkCode->mov_esp4[3] = 0x04;
        pThunkCode->pThis = (DWORD)(ULONG_PTR)pThis;
        pThunkCode->jmp = 0xE9;
        pThunkCode->relProc = (DWORD)((ULONG_PTR)proc - ((ULONG_PTR)&pThunkCode->relProc + sizeof(DWORD)));
#elif defined(_M_ARM64) || defined(__aarch64__)
        pThunkCode->ldr_x0 = 0x58000060;   // ldr x0, [pc, #12]
        pThunkCode->ldr_x16 = 0x58000070;  // ldr x16, [pc, #12]
        pThunkCode->br_x16 = 0xD61F0200;
        pThunkCode->pThis = (ULONG_PTR)pThis;
        pThunkCode->pProc = (ULONG_PTR)proc;
#endif

        // Restore original protection and flush instruction cache
        ::VirtualProtect(pThunkCode, sizeof(ThunkCode), dwOldProtect, &dwOldProtect);
        ::FlushInstructionCache(::GetCurrentProcess(), pThunkCode, sizeof(ThunkCode));
        return TRUE;
    }

    WNDPROC GetWndProc() noexcept
    {
        return (WNDPROC)pThunkCode;
    }

    // WTL uses GetWNDPROC (uppercase)
    WNDPROC GetWNDPROC() noexcept
    {
        return (WNDPROC)pThunkCode;
    }

    void* GetCodeAddress() noexcept
    {
        return pThunkCode;
    }

    CWndProcThunk() noexcept : pThunkCode(NULL)
    {
        memset(&cd, 0, sizeof(cd));
    }

    ~CWndProcThunk()
    {
        if (pThunkCode != NULL) {
            ::VirtualFree(pThunkCode, 0, MEM_RELEASE);
            pThunkCode = NULL;
        }
    }
};

#pragma pack(pop)

///////////////////////////////////////////////////////////////////////////////
// CWindow - HWND wrapper class

class CWindow {
public:
    static RECT rcDefault;
    HWND m_hWnd;

    CWindow(HWND hWnd = NULL) noexcept : m_hWnd(hWnd) {}

    CWindow& operator=(HWND hWnd) noexcept
    {
        m_hWnd = hWnd;
        return *this;
    }

    operator HWND() const noexcept { return m_hWnd; }

    static LPCTSTR GetWndClassName() noexcept
    {
        return NULL;
    }

    void Attach(HWND hWndNew) noexcept
    {
        ATLASSERT(m_hWnd == NULL || !::IsWindow(m_hWnd));
        m_hWnd = hWndNew;
    }

    HWND Detach() noexcept
    {
        HWND hWnd = m_hWnd;
        m_hWnd = NULL;
        return hWnd;
    }

    HWND Create(LPCTSTR lpstrWndClass, HWND hWndParent, _U_RECT rect = NULL,
        LPCTSTR szWindowName = NULL, DWORD dwStyle = 0, DWORD dwExStyle = 0,
        _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) noexcept
    {
        ATLASSERT(m_hWnd == NULL);
        m_hWnd = ::CreateWindowEx(dwExStyle, lpstrWndClass, szWindowName,
            dwStyle, rect.m_lpRect ? rect.m_lpRect->left : CW_USEDEFAULT,
            rect.m_lpRect ? rect.m_lpRect->top : CW_USEDEFAULT,
            rect.m_lpRect ? (rect.m_lpRect->right - rect.m_lpRect->left) : CW_USEDEFAULT,
            rect.m_lpRect ? (rect.m_lpRect->bottom - rect.m_lpRect->top) : CW_USEDEFAULT,
            hWndParent, MenuOrID.m_hMenu,
            _AtlBaseModule.GetModuleInstance(), lpCreateParam);
        return m_hWnd;
    }

    BOOL DestroyWindow() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        if (!::DestroyWindow(m_hWnd))
            return FALSE;
        m_hWnd = NULL;
        return TRUE;
    }

    // Attributes
    DWORD GetStyle() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (DWORD)::GetWindowLong(m_hWnd, GWL_STYLE);
    }

    DWORD GetExStyle() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (DWORD)::GetWindowLong(m_hWnd, GWL_EXSTYLE);
    }

    BOOL ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        DWORD dwStyle = (DWORD)::GetWindowLong(m_hWnd, GWL_STYLE);
        DWORD dwNewStyle = (dwStyle & ~dwRemove) | dwAdd;
        if (dwStyle == dwNewStyle)
            return FALSE;
        ::SetWindowLong(m_hWnd, GWL_STYLE, dwNewStyle);
        if (nFlags != 0)
            ::SetWindowPos(m_hWnd, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | nFlags);
        return TRUE;
    }

    BOOL ModifyStyleEx(DWORD dwRemove, DWORD dwAdd, UINT nFlags = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        DWORD dwStyle = (DWORD)::GetWindowLong(m_hWnd, GWL_EXSTYLE);
        DWORD dwNewStyle = (dwStyle & ~dwRemove) | dwAdd;
        if (dwStyle == dwNewStyle)
            return FALSE;
        ::SetWindowLong(m_hWnd, GWL_EXSTYLE, dwNewStyle);
        if (nFlags != 0)
            ::SetWindowPos(m_hWnd, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | nFlags);
        return TRUE;
    }

    BOOL ResizeClient(int nWidth, int nHeight, BOOL bRedraw = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        RECT rcWnd;
        if (!GetClientRect(&rcWnd)) return FALSE;
        if (nWidth != -1) rcWnd.right = nWidth;
        if (nHeight != -1) rcWnd.bottom = nHeight;
        if (!::AdjustWindowRectEx(&rcWnd, GetStyle(), (!(GetStyle() & WS_CHILD) && (::GetMenu(m_hWnd) != NULL)), GetExStyle()))
            return FALSE;
        UINT uFlags = SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE;
        if (!bRedraw) uFlags |= SWP_NOREDRAW;
        return ::SetWindowPos(m_hWnd, NULL, 0, 0, rcWnd.right - rcWnd.left, rcWnd.bottom - rcWnd.top, uFlags);
    }

    BOOL SetWindowContextHelpId(DWORD dwContextHelpId) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetWindowContextHelpId(m_hWnd, dwContextHelpId);
    }

    DWORD GetWindowContextHelpId() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowContextHelpId(m_hWnd);
    }

    LONG GetWindowLong(int nIndex) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowLong(m_hWnd, nIndex);
    }

    LONG SetWindowLong(int nIndex, LONG dwNewLong) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetWindowLong(m_hWnd, nIndex, dwNewLong);
    }

    LONG_PTR GetWindowLongPtr(int nIndex) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowLongPtr(m_hWnd, nIndex);
    }

    LONG_PTR SetWindowLongPtr(int nIndex, LONG_PTR dwNewLong) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetWindowLongPtr(m_hWnd, nIndex, dwNewLong);
    }

    WORD GetWindowWord(int nIndex) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (WORD)::GetWindowLong(m_hWnd, nIndex);
    }

    WORD SetWindowWord(int nIndex, WORD wNewWord) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (WORD)::SetWindowLong(m_hWnd, nIndex, (LONG)wNewWord);
    }

    // Message functions
    LRESULT SendMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SendMessage(m_hWnd, message, wParam, lParam);
    }

    BOOL PostMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::PostMessage(m_hWnd, message, wParam, lParam);
    }

    BOOL SendNotifyMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SendNotifyMessage(m_hWnd, message, wParam, lParam);
    }

    LRESULT SendMessageToDescendants(UINT message, WPARAM wParam = 0, LPARAM lParam = 0, BOOL bDeep = TRUE) noexcept
    {
        for (HWND hWndChild = ::GetWindow(m_hWnd, GW_CHILD); hWndChild != NULL;
            hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT)) {
            ::SendMessage(hWndChild, message, wParam, lParam);
            if (bDeep) {
                CWindow(hWndChild).SendMessageToDescendants(message, wParam, lParam, bDeep);
            }
        }
        return 0;
    }

    // Window state
    BOOL IsWindow() const noexcept { return ::IsWindow(m_hWnd); }
    BOOL IsWindowVisible() const noexcept { return ::IsWindowVisible(m_hWnd); }
    BOOL IsWindowEnabled() const noexcept { return ::IsWindowEnabled(m_hWnd); }
    BOOL IsWindowUnicode() const noexcept { return ::IsWindowUnicode(m_hWnd); }
    BOOL IsParentDialog() const noexcept { return (BOOL)((ULONG_PTR)::GetWindowLongPtr(m_hWnd, DWLP_DLGPROC)); }

    BOOL ShowWindow(int nCmdShow) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ShowWindow(m_hWnd, nCmdShow);
    }

    BOOL EnableWindow(BOOL bEnable = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::EnableWindow(m_hWnd, bEnable);
    }

    BOOL SetWindowText(LPCTSTR lpszString) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetWindowText(m_hWnd, lpszString);
    }

    int GetWindowText(LPTSTR lpszStringBuf, int nMaxCount) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowText(m_hWnd, lpszStringBuf, nMaxCount);
    }

    int GetWindowTextLength() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowTextLength(m_hWnd);
    }

    // Font
    void SetFont(HFONT hFont, BOOL bRedraw = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        ::SendMessage(m_hWnd, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(bRedraw, 0));
    }

    HFONT GetFont() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0);
    }

    // Menu
    HMENU GetMenu() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetMenu(m_hWnd);
    }

    BOOL SetMenu(HMENU hMenu) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetMenu(m_hWnd, hMenu);
    }

    BOOL DrawMenuBar() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::DrawMenuBar(m_hWnd);
    }

    HMENU GetSystemMenu(BOOL bRevert) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetSystemMenu(m_hWnd, bRevert);
    }

    BOOL HiliteMenuItem(HMENU hMenu, UINT uItemHilite, UINT uHilite) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::HiliteMenuItem(m_hWnd, hMenu, uItemHilite, uHilite);
    }

    // Window size/position
    BOOL GetClientRect(LPRECT lpRect) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetClientRect(m_hWnd, lpRect);
    }

    BOOL GetWindowRect(LPRECT lpRect) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowRect(m_hWnd, lpRect);
    }

    BOOL MoveWindow(int x, int y, int nWidth, int nHeight, BOOL bRepaint = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::MoveWindow(m_hWnd, x, y, nWidth, nHeight, bRepaint);
    }

    BOOL MoveWindow(LPCRECT lpRect, BOOL bRepaint = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::MoveWindow(m_hWnd, lpRect->left, lpRect->top,
            lpRect->right - lpRect->left, lpRect->bottom - lpRect->top, bRepaint);
    }

    BOOL SetWindowPos(HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT nFlags) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetWindowPos(m_hWnd, hWndInsertAfter, x, y, cx, cy, nFlags);
    }

    BOOL SetWindowPos(HWND hWndInsertAfter, LPCRECT lpRect, UINT nFlags) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetWindowPos(m_hWnd, hWndInsertAfter, lpRect->left, lpRect->top,
            lpRect->right - lpRect->left, lpRect->bottom - lpRect->top, nFlags);
    }

    UINT ArrangeIconicWindows() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ArrangeIconicWindows(m_hWnd);
    }

    BOOL BringWindowToTop() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::BringWindowToTop(m_hWnd);
    }

    // Coordinate mapping
    BOOL MapWindowPoints(HWND hWndTo, LPPOINT lpPoint, UINT nCount) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (BOOL)(::MapWindowPoints(m_hWnd, hWndTo, lpPoint, nCount) != 0);
    }

    BOOL MapWindowPoints(HWND hWndTo, LPRECT lpRect) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (BOOL)(::MapWindowPoints(m_hWnd, hWndTo, (LPPOINT)lpRect, 2) != 0);
    }

    int MapWindowPoints(HWND hWndTo, LPPOINT lpPoint) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::MapWindowPoints(m_hWnd, hWndTo, lpPoint, 1);
    }

    BOOL ClientToScreen(LPPOINT lpPoint) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ClientToScreen(m_hWnd, lpPoint);
    }

    BOOL ClientToScreen(LPRECT lpRect) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        if (!::ClientToScreen(m_hWnd, (LPPOINT)lpRect))
            return FALSE;
        return ::ClientToScreen(m_hWnd, ((LPPOINT)lpRect) + 1);
    }

    BOOL ScreenToClient(LPPOINT lpPoint) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ScreenToClient(m_hWnd, lpPoint);
    }

    BOOL ScreenToClient(LPRECT lpRect) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        if (!::ScreenToClient(m_hWnd, (LPPOINT)lpRect))
            return FALSE;
        return ::ScreenToClient(m_hWnd, ((LPPOINT)lpRect) + 1);
    }

    // Update and painting
    HDC GetDC() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetDC(m_hWnd);
    }

    HDC GetWindowDC() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowDC(m_hWnd);
    }

    int ReleaseDC(HDC hDC) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ReleaseDC(m_hWnd, hDC);
    }

    HDC BeginPaint(LPPAINTSTRUCT lpPaint) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::BeginPaint(m_hWnd, lpPaint);
    }

    void EndPaint(LPPAINTSTRUCT lpPaint) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        ::EndPaint(m_hWnd, lpPaint);
    }

    BOOL UpdateWindow() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::UpdateWindow(m_hWnd);
    }

    void SetRedraw(BOOL bRedraw = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        ::SendMessage(m_hWnd, WM_SETREDRAW, (WPARAM)bRedraw, 0);
    }

    BOOL GetUpdateRect(LPRECT lpRect, BOOL bErase = FALSE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetUpdateRect(m_hWnd, lpRect, bErase);
    }

    int GetUpdateRgn(HRGN hRgn, BOOL bErase = FALSE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetUpdateRgn(m_hWnd, hRgn, bErase);
    }

    BOOL Invalidate(BOOL bErase = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::InvalidateRect(m_hWnd, NULL, bErase);
    }

    BOOL InvalidateRect(LPCRECT lpRect, BOOL bErase = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::InvalidateRect(m_hWnd, lpRect, bErase);
    }

    BOOL InvalidateRgn(HRGN hRgn, BOOL bErase = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::InvalidateRgn(m_hWnd, hRgn, bErase);
    }

    BOOL ValidateRect(LPCRECT lpRect) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ValidateRect(m_hWnd, lpRect);
    }

    BOOL ValidateRgn(HRGN hRgn) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ValidateRgn(m_hWnd, hRgn);
    }

    BOOL RedrawWindow(LPCRECT lpRectUpdate = NULL, HRGN hRgnUpdate = NULL, UINT flags = RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::RedrawWindow(m_hWnd, lpRectUpdate, hRgnUpdate, flags);
    }

    // Timer
    UINT_PTR SetTimer(UINT_PTR nIDEvent, UINT nElapse, TIMERPROC lpfnTimer = NULL) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetTimer(m_hWnd, nIDEvent, nElapse, lpfnTimer);
    }

    BOOL KillTimer(UINT_PTR nIDEvent) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::KillTimer(m_hWnd, nIDEvent);
    }

    // Window tree
    CWindow GetParent() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::GetParent(m_hWnd));
    }

    CWindow SetParent(HWND hWndNewParent) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::SetParent(m_hWnd, hWndNewParent));
    }

    CWindow GetTopLevelParent() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        HWND hWndParent = m_hWnd;
        HWND hWndTmp;
        while ((hWndTmp = ::GetParent(hWndParent)) != NULL)
            hWndParent = hWndTmp;
        return CWindow(hWndParent);
    }

    CWindow GetTopLevelWindow() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        HWND hWndParent;
        HWND hWndTmp = m_hWnd;
        do {
            hWndParent = hWndTmp;
            hWndTmp = (::GetWindowLong(hWndParent, GWL_STYLE) & WS_CHILD) ? ::GetParent(hWndParent) : ::GetWindow(hWndParent, GW_OWNER);
        } while (hWndTmp != NULL);
        return CWindow(hWndParent);
    }

    CWindow GetWindow(UINT nCmd) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::GetWindow(m_hWnd, nCmd));
    }

    CWindow GetTopWindow() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::GetTopWindow(m_hWnd));
    }

    CWindow GetLastActivePopup() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::GetLastActivePopup(m_hWnd));
    }

    BOOL IsChild(HWND hWnd) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::IsChild(m_hWnd, hWnd);
    }

    // Dialog item
    CWindow GetDlgItem(int nID) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::GetDlgItem(m_hWnd, nID));
    }

    UINT GetDlgItemInt(int nID, BOOL* lpTrans = NULL, BOOL bSigned = TRUE) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetDlgItemInt(m_hWnd, nID, lpTrans, bSigned);
    }

    BOOL SetDlgItemInt(int nID, UINT nValue, BOOL bSigned = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetDlgItemInt(m_hWnd, nID, nValue, bSigned);
    }

    UINT GetDlgItemText(int nID, LPTSTR lpStr, int nMaxCount) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetDlgItemText(m_hWnd, nID, lpStr, nMaxCount);
    }

    BOOL SetDlgItemText(int nID, LPCTSTR lpszString) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetDlgItemText(m_hWnd, nID, lpszString);
    }

    BOOL CheckDlgButton(int nIDButton, UINT nCheck) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::CheckDlgButton(m_hWnd, nIDButton, nCheck);
    }

    BOOL CheckRadioButton(int nIDFirstButton, int nIDLastButton, int nIDCheckButton) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::CheckRadioButton(m_hWnd, nIDFirstButton, nIDLastButton, nIDCheckButton);
    }

    UINT IsDlgButtonChecked(int nIDButton) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::IsDlgButtonChecked(m_hWnd, nIDButton);
    }

    LRESULT SendDlgItemMessage(int nID, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SendDlgItemMessage(m_hWnd, nID, message, wParam, lParam);
    }

    CWindow GetNextDlgGroupItem(HWND hWndCtl, BOOL bPrevious = FALSE) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::GetNextDlgGroupItem(m_hWnd, hWndCtl, bPrevious));
    }

    CWindow GetNextDlgTabItem(HWND hWndCtl, BOOL bPrevious = FALSE) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::GetNextDlgTabItem(m_hWnd, hWndCtl, bPrevious));
    }

    // Scrolling
    int GetScrollPos(int nBar) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetScrollPos(m_hWnd, nBar);
    }

    BOOL GetScrollRange(int nBar, LPINT lpMinPos, LPINT lpMaxPos) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetScrollRange(m_hWnd, nBar, lpMinPos, lpMaxPos);
    }

    int SetScrollPos(int nBar, int nPos, BOOL bRedraw = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetScrollPos(m_hWnd, nBar, nPos, bRedraw);
    }

    BOOL SetScrollRange(int nBar, int nMinPos, int nMaxPos, BOOL bRedraw = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetScrollRange(m_hWnd, nBar, nMinPos, nMaxPos, bRedraw);
    }

    BOOL GetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetScrollInfo(m_hWnd, nBar, lpScrollInfo);
    }

    int SetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo, BOOL bRedraw = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetScrollInfo(m_hWnd, nBar, lpScrollInfo, bRedraw);
    }

    BOOL ScrollWindow(int xAmount, int yAmount, LPCRECT lpRect = NULL, LPCRECT lpClipRect = NULL) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ScrollWindow(m_hWnd, xAmount, yAmount, lpRect, lpClipRect);
    }

    int ScrollWindowEx(int dx, int dy, UINT uFlags) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ScrollWindowEx(m_hWnd, dx, dy, NULL, NULL, NULL, NULL, uFlags);
    }

    int ScrollWindowEx(int dx, int dy, LPCRECT lpRectScroll, LPCRECT lpRectClip,
        HRGN hRgnUpdate, LPRECT lpRectUpdate, UINT uFlags) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::ScrollWindowEx(m_hWnd, dx, dy, lpRectScroll, lpRectClip, hRgnUpdate, lpRectUpdate, uFlags);
    }

    // Focus / Active / Capture
    CWindow SetFocus() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::SetFocus(m_hWnd));
    }

    CWindow SetActiveWindow() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::SetActiveWindow(m_hWnd));
    }

    CWindow SetCapture() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::SetCapture(m_hWnd));
    }

    // Icon
    HICON SetIcon(HICON hIcon, BOOL bBigIcon = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (HICON)::SendMessage(m_hWnd, WM_SETICON, bBigIcon, (LPARAM)hIcon);
    }

    HICON GetIcon(BOOL bBigIcon = TRUE) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (HICON)::SendMessage(m_hWnd, WM_GETICON, bBigIcon, 0);
    }

    // Properties
    BOOL GetWindowPlacement(WINDOWPLACEMENT* lpwndpl) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowPlacement(m_hWnd, lpwndpl);
    }

    BOOL SetWindowPlacement(const WINDOWPLACEMENT* lpwndpl) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetWindowPlacement(m_hWnd, lpwndpl);
    }

    int GetDlgCtrlID() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetDlgCtrlID(m_hWnd);
    }

    int SetDlgCtrlID(int nID) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (int)::SetWindowLongPtr(m_hWnd, GWLP_ID, nID);
    }

    BOOL IsIconic() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::IsIconic(m_hWnd);
    }

    BOOL IsZoomed() const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::IsZoomed(m_hWnd);
    }

    BOOL FlashWindow(BOOL bInvert) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::FlashWindow(m_hWnd, bInvert);
    }

    BOOL OpenClipboard() noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::OpenClipboard(m_hWnd);
    }

    CWindow ChildWindowFromPoint(POINT point) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::ChildWindowFromPoint(m_hWnd, point));
    }

    CWindow ChildWindowFromPointEx(POINT point, UINT uFlags) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return CWindow(::ChildWindowFromPointEx(m_hWnd, point, uFlags));
    }

    // Misc
    BOOL SetWindowRgn(HRGN hRgn, BOOL bRedraw = FALSE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return (BOOL)::SetWindowRgn(m_hWnd, hRgn, bRedraw);
    }

    int GetWindowRgn(HRGN hRgn) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetWindowRgn(m_hWnd, hRgn);
    }

    int MessageBox(LPCTSTR lpszText, LPCTSTR lpszCaption = _T(""), UINT nType = MB_OK) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::MessageBox(m_hWnd, lpszText, lpszCaption, nType);
    }

    BOOL WinHelp(LPCTSTR lpszHelp, UINT nCmd = HELP_CONTEXT, DWORD dwData = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::WinHelp(m_hWnd, lpszHelp, nCmd, dwData);
    }

    BOOL LockWindowUpdate(BOOL bLock = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::LockWindowUpdate(bLock ? m_hWnd : NULL);
    }

    void DragAcceptFiles(BOOL bAccept = TRUE) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        ::DragAcceptFiles(m_hWnd, bAccept);
    }

    BOOL SetLayeredWindowAttributes(COLORREF crKey, BYTE bAlpha, DWORD dwFlags) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::SetLayeredWindowAttributes(m_hWnd, crKey, bAlpha, dwFlags);
    }

    BOOL GetLayeredWindowAttributes(COLORREF* pcrKey, BYTE* pbAlpha, DWORD* pdwFlags) const noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::GetLayeredWindowAttributes(m_hWnd, pcrKey, pbAlpha, pdwFlags);
    }

    BOOL PrintWindow(HDC hDC, UINT nFlags = 0) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::PrintWindow(m_hWnd, hDC, nFlags);
    }

    BOOL AnimateWindow(DWORD dwTime, DWORD dwFlags) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));
        return ::AnimateWindow(m_hWnd, dwTime, dwFlags);
    }

    // Static helpers
    static BOOL IsWindow(HWND hWnd) noexcept { return ::IsWindow(hWnd); }
    static HWND GetFocus() noexcept { return ::GetFocus(); }
    static HWND GetActiveWindow() noexcept { return ::GetActiveWindow(); }
    static HWND GetCapture() noexcept { return ::GetCapture(); }
    static HWND GetDesktopWindow() noexcept { return ::GetDesktopWindow(); }

    // Centering support
    BOOL CenterWindow(HWND hWndCenter = NULL) noexcept
    {
        ATLASSERT(::IsWindow(m_hWnd));

        // Determine owner window to center against
        DWORD dwStyle = GetStyle();
        if (hWndCenter == NULL) {
            if (dwStyle & WS_CHILD)
                hWndCenter = ::GetParent(m_hWnd);
            else
                hWndCenter = ::GetWindow(m_hWnd, GW_OWNER);
        }

        // Get coordinates of the window relative to its parent
        RECT rcDlg;
        ::GetWindowRect(m_hWnd, &rcDlg);
        RECT rcArea;
        RECT rcCenter;

        if (!(dwStyle & WS_CHILD)) {
            // Center within screen coordinates
            HMONITOR hMonitor = NULL;
            if (hWndCenter != NULL) {
                hMonitor = ::MonitorFromWindow(hWndCenter, MONITOR_DEFAULTTONEAREST);
            } else {
                hMonitor = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
            }
            MONITORINFO mi = { sizeof(MONITORINFO) };
            ::GetMonitorInfo(hMonitor, &mi);
            rcArea = mi.rcWork;

            if (hWndCenter == NULL)
                rcCenter = rcArea;
            else
                ::GetWindowRect(hWndCenter, &rcCenter);
        } else {
            // Center within parent client coordinates
            ::GetClientRect(hWndCenter, &rcArea);
            ATLASSERT(::IsWindow(hWndCenter));
            ::GetClientRect(hWndCenter, &rcCenter);
            ::MapWindowPoints(hWndCenter, ::GetParent(m_hWnd), (POINT*)&rcCenter, 2);
        }

        int DlgWidth = rcDlg.right - rcDlg.left;
        int DlgHeight = rcDlg.bottom - rcDlg.top;

        int xLeft = (rcCenter.left + rcCenter.right) / 2 - DlgWidth / 2;
        int yTop = (rcCenter.top + rcCenter.bottom) / 2 - DlgHeight / 2;

        // Ensure visible
        if (xLeft + DlgWidth > rcArea.right)
            xLeft = rcArea.right - DlgWidth;
        if (xLeft < rcArea.left)
            xLeft = rcArea.left;

        if (yTop + DlgHeight > rcArea.bottom)
            yTop = rcArea.bottom - DlgHeight;
        if (yTop < rcArea.top)
            yTop = rcArea.top;

        return ::SetWindowPos(m_hWnd, NULL, xLeft, yTop, -1, -1,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
};

__declspec(selectany) RECT CWindow::rcDefault = { CW_USEDEFAULT, CW_USEDEFAULT, 0, 0 };

///////////////////////////////////////////////////////////////////////////////
// _ATL_WNDCLASSINFOW / CWndClassInfo

struct _ATL_WNDCLASSINFOW {
    WNDCLASSEXW m_wc;
    LPCWSTR m_lpszOrigName;
    WNDPROC pWndProc;
    LPCWSTR m_lpszCursorID;
    BOOL m_bSystemCursor;
    ATOM m_atom;
    WCHAR m_szAutoName[5 + sizeof(void*) * 2];

    ATOM Register(WNDPROC* pProc) noexcept
    {
        if (m_atom == 0) {
            CComCritSecLock<CComCriticalSection> lock(_AtlWinModule.m_csWindowCreate, false);
            if (FAILED(lock.Lock())) {
                ATLASSERT(FALSE);
                return 0;
            }

            if (m_atom == 0) {
                HINSTANCE hInst = _AtlBaseModule.GetModuleInstance();

                if (m_lpszOrigName != NULL) {
                    ATLASSERT(pProc != NULL);
                    LPCWSTR lpsz = m_wc.lpszClassName;
                    WNDPROC proc = m_wc.lpfnWndProc;

                    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
                    if (!::GetClassInfoExW(_AtlBaseModule.GetModuleInstance(), m_lpszOrigName, &wc)) {
                        if (!::GetClassInfoExW(NULL, m_lpszOrigName, &wc))
                            return 0;
                    }
                    m_wc = wc;
                    pWndProc = m_wc.lpfnWndProc;
                    m_wc.lpszClassName = lpsz;
                    m_wc.lpfnWndProc = proc;
                } else {
                    m_wc.hCursor = ::LoadCursorW(m_bSystemCursor ? NULL : hInst, m_lpszCursorID);
                }

                m_wc.hInstance = hInst;
                m_wc.style &= ~CS_GLOBALCLASS;

                if (m_wc.lpszClassName == NULL) {
                    wsprintfW(m_szAutoName, L"ATL:%p", &m_wc);
                    m_wc.lpszClassName = m_szAutoName;
                }

                WNDCLASSEXW wcTemp = m_wc;
                m_atom = (ATOM)::GetClassInfoExW(m_wc.hInstance, m_wc.lpszClassName, &wcTemp);
                if (m_atom == 0)
                    m_atom = ::RegisterClassExW(&m_wc);
            }
        }

        if (m_lpszOrigName != NULL) {
            ATLASSERT(pProc != NULL);
            ATLASSERT(pWndProc != NULL);
            *pProc = pWndProc;
        }

        return m_atom;
    }
};

typedef _ATL_WNDCLASSINFOW CWndClassInfo;

///////////////////////////////////////////////////////////////////////////////
// CWindowImplRoot - root class for CWindowImpl hierarchy

template <typename TBase = CWindow>
class ATL_NO_VTABLE CWindowImplRoot : public TBase, public CMessageMap {
public:
    enum { WINSTATE_DESTROYED = 0x00000001 };

    CWndProcThunk m_thunk;
    const _ATL_MSG* m_pCurrentMsg;
    DWORD m_dwState;
    WNDPROC m_pfnSuperWindowProc;
    BOOL m_bMsgHandled;

    CWindowImplRoot() noexcept
        : m_pCurrentMsg(NULL), m_dwState(0),
          m_pfnSuperWindowProc(::DefWindowProc),
          m_bMsgHandled(FALSE)
    {
    }

    BOOL IsMsgHandled() const noexcept { return m_bMsgHandled; }
    void SetMsgHandled(BOOL bHandled) noexcept { m_bMsgHandled = bHandled; }

    virtual ~CWindowImplRoot() {}

    virtual void OnFinalMessage(HWND /*hWnd*/) {}

    // Reflect notification messages to child windows
    LRESULT ReflectNotifications(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) noexcept
    {
        HWND hWndChild = NULL;

        switch (uMsg) {
        case WM_COMMAND:
            if (lParam != NULL) // from a control, not a menu
                hWndChild = (HWND)lParam;
            break;
        case WM_NOTIFY:
            hWndChild = ((LPNMHDR)lParam)->hwndFrom;
            break;
        case WM_PARENTNOTIFY:
            switch (LOWORD(wParam)) {
            case WM_CREATE:
            case WM_DESTROY:
                hWndChild = (HWND)lParam;
                break;
            default:
                hWndChild = ::GetDlgItem(this->m_hWnd, HIWORD(wParam));
                break;
            }
            break;
        case WM_DRAWITEM:
            if (wParam)
                hWndChild = ((LPDRAWITEMSTRUCT)lParam)->hwndItem;
            break;
        case WM_MEASUREITEM:
            if (wParam)
                hWndChild = ::GetDlgItem(this->m_hWnd, ((LPMEASUREITEMSTRUCT)lParam)->CtlID);
            break;
        case WM_COMPAREITEM:
            if (wParam)
                hWndChild = ((LPCOMPAREITEMSTRUCT)lParam)->hwndItem;
            break;
        case WM_DELETEITEM:
            if (wParam)
                hWndChild = ((LPDELETEITEMSTRUCT)lParam)->hwndItem;
            break;
        case WM_VKEYTOITEM:
        case WM_CHARTOITEM:
        case WM_HSCROLL:
        case WM_VSCROLL:
            hWndChild = (HWND)lParam;
            break;
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORMSGBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
            hWndChild = (HWND)lParam;
            break;
        default:
            break;
        }

        if (hWndChild == NULL) {
            bHandled = FALSE;
            return 1;
        }

        ATLASSERT(::IsWindow(hWndChild));
        return ::SendMessage(hWndChild, OCM__BASE + uMsg, wParam, lParam);
    }

    // Forward notification messages from a child to its parent
    LRESULT ForwardNotifications(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) noexcept
    {
        LRESULT lResult = 0;
        switch (uMsg) {
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_PARENTNOTIFY:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_COMPAREITEM:
        case WM_DELETEITEM:
        case WM_VKEYTOITEM:
        case WM_CHARTOITEM:
        case WM_HSCROLL:
        case WM_VSCROLL:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORMSGBOX:
        case WM_CTLCOLORSCROLLBAR:
        case WM_CTLCOLORSTATIC:
            lResult = ::SendMessage(::GetParent(this->m_hWnd), uMsg, wParam, lParam);
            break;
        default:
            bHandled = FALSE;
            break;
        }
        return lResult;
    }

    static BOOL DefaultReflectionHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult) noexcept
    {
        switch (uMsg) {
        case OCM_COMMAND:
        case OCM_NOTIFY:
        case OCM_PARENTNOTIFY:
        case OCM_DRAWITEM:
        case OCM_MEASUREITEM:
        case OCM_COMPAREITEM:
        case OCM_DELETEITEM:
        case OCM_VKEYTOITEM:
        case OCM_CHARTOITEM:
        case OCM_HSCROLL:
        case OCM_VSCROLL:
        case OCM_CTLCOLORBTN:
        case OCM_CTLCOLORDLG:
        case OCM_CTLCOLOREDIT:
        case OCM_CTLCOLORLISTBOX:
        case OCM_CTLCOLORMSGBOX:
        case OCM_CTLCOLORSCROLLBAR:
        case OCM_CTLCOLORSTATIC:
            lResult = ::DefWindowProc(hWnd, uMsg - OCM__BASE, wParam, lParam);
            return TRUE;
        default:
            break;
        }
        return FALSE;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CWindowImplBaseT - base class for CWindowImpl

template <typename TBase = CWindow, typename TWinTraits = CControlWinTraits>
class ATL_NO_VTABLE CWindowImplBaseT : public CWindowImplRoot<TBase> {
public:
    // m_pfnSuperWindowProc is inherited from CWindowImplRoot

    CWindowImplBaseT() noexcept
    {
    }

    static DWORD GetWndStyle(DWORD dwStyle) noexcept
    {
        return TWinTraits::GetWndStyle(dwStyle);
    }

    static DWORD GetWndExStyle(DWORD dwExStyle) noexcept
    {
        return TWinTraits::GetWndExStyle(dwExStyle);
    }

    virtual WNDPROC GetWindowProc()
    {
        return WindowProc;
    }

    static LRESULT CALLBACK StartWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CWindowImplBaseT<TBase, TWinTraits>* pThis =
            (CWindowImplBaseT<TBase, TWinTraits>*)_AtlWinModule.ExtractCreateWndData();
        ATLASSERT(pThis != NULL);
        if (pThis == NULL)
            return 0;

        pThis->m_hWnd = hWnd;

        // Initialize the thunk
        pThis->m_thunk.Init(pThis->GetWindowProc(), pThis);
        WNDPROC pProc = pThis->m_thunk.GetWndProc();

        // Subclass the window with the thunk
        ::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pProc);

        // Process the first message through the thunk
        return pProc(hWnd, uMsg, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CWindowImplBaseT<TBase, TWinTraits>* pThis =
            (CWindowImplBaseT<TBase, TWinTraits>*)hWnd;
        _ATL_MSG msg(pThis->m_hWnd, uMsg, wParam, lParam);
        const _ATL_MSG* pOldMsg = pThis->m_pCurrentMsg;
        pThis->m_pCurrentMsg = &msg;

        LRESULT lRes = 0;
        BOOL bRet = pThis->ProcessWindowMessage(pThis->m_hWnd, uMsg, wParam, lParam, lRes, 0);

        ATLASSERT(pThis->m_pCurrentMsg == &msg);
        pThis->m_pCurrentMsg = pOldMsg;

        if (!bRet) {
            if (uMsg != WM_NCDESTROY) {
                lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
            } else {
                LONG_PTR pfnWndProc = ::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC);
                lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
                if ((pThis->m_pfnSuperWindowProc != ::DefWindowProc) &&
                    (::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC) == pfnWndProc)) {
                    ::SetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC, (LONG_PTR)pThis->m_pfnSuperWindowProc);
                }
                pThis->m_dwState |= CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
            }
        }

        if ((pThis->m_dwState & CWindowImplRoot<TBase>::WINSTATE_DESTROYED) && (pThis->m_pCurrentMsg == NULL)) {
            HWND hWndThis = pThis->m_hWnd;
            pThis->m_hWnd = NULL;
            pThis->m_dwState &= ~CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
            pThis->OnFinalMessage(hWndThis);
        }

        return lRes;
    }

    LRESULT DefWindowProc() noexcept
    {
        const _ATL_MSG* pMsg = this->m_pCurrentMsg;
        LRESULT lRes = 0;
        if (pMsg != NULL)
            lRes = DefWindowProc(pMsg->message, pMsg->wParam, pMsg->lParam);
        return lRes;
    }

    LRESULT DefWindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
    {
        return ::CallWindowProc(this->m_pfnSuperWindowProc, this->m_hWnd, uMsg, wParam, lParam);
    }

    HWND Create(HWND hWndParent, _U_RECT rect, LPCTSTR szWindowName,
        DWORD dwStyle, DWORD dwExStyle, _U_MENUorID MenuOrID, ATOM atom, LPVOID lpCreateParam = NULL) noexcept
    {
        ATLASSERT(this->m_hWnd == NULL);

        BOOL bRet = this->m_thunk.Init(NULL, NULL);
        if (bRet == FALSE) {
            ::SetLastError(ERROR_OUTOFMEMORY);
            return NULL;
        }

        if (atom == 0)
            return NULL;

        _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, this);

        if ((MenuOrID.m_hMenu == NULL) && (dwStyle & WS_CHILD))
            MenuOrID.m_hMenu = (HMENU)(UINT_PTR)this;
        if (rect.m_lpRect == NULL)
            rect.m_lpRect = &TBase::rcDefault;

        HWND hWnd = ::CreateWindowEx(dwExStyle, MAKEINTATOM(atom), szWindowName,
            dwStyle, rect.m_lpRect->left, rect.m_lpRect->top,
            rect.m_lpRect->right - rect.m_lpRect->left,
            rect.m_lpRect->bottom - rect.m_lpRect->top,
            hWndParent, MenuOrID.m_hMenu,
            _AtlBaseModule.GetModuleInstance(), lpCreateParam);

        ATLASSERT((hWnd == NULL) || (this->m_hWnd == hWnd));
        return hWnd;
    }

    BOOL SubclassWindow(HWND hWnd) noexcept
    {
        ATLASSERT(this->m_hWnd == NULL);
        ATLASSERT(::IsWindow(hWnd));

        BOOL bRet = this->m_thunk.Init(GetWindowProc(), this);
        if (bRet == FALSE)
            return FALSE;

        WNDPROC pProc = this->m_thunk.GetWndProc();
        WNDPROC pfnWndProc = (WNDPROC)::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pProc);
        if (pfnWndProc == NULL)
            return FALSE;

        this->m_pfnSuperWindowProc = pfnWndProc;
        this->m_hWnd = hWnd;
        return TRUE;
    }

    HWND UnsubclassWindow(BOOL bForce = FALSE) noexcept
    {
        ATLASSERT(this->m_hWnd != NULL);

        WNDPROC pOurProc = this->m_thunk.GetWndProc();
        WNDPROC pActiveProc = (WNDPROC)::GetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC);

        HWND hWnd = NULL;
        if (bForce || pActiveProc == pOurProc) {
            if (!::SetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC, (LONG_PTR)this->m_pfnSuperWindowProc))
                return NULL;

            this->m_pfnSuperWindowProc = ::DefWindowProc;
            hWnd = this->m_hWnd;
            this->m_hWnd = NULL;
        }
        return hWnd;
    }
};

} // namespace ATL (temporarily close for macro definitions)

///////////////////////////////////////////////////////////////////////////////
// Message Map Macros

#define BEGIN_MSG_MAP(theClass) \
public: \
    BOOL ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult, DWORD dwMsgMapID = 0) \
    { \
        BOOL bHandled = TRUE; \
        (void)hWnd; \
        (void)uMsg; \
        (void)wParam; \
        (void)lParam; \
        (void)lResult; \
        (void)dwMsgMapID; \
        (void)bHandled; \
        switch(dwMsgMapID) \
        { \
        case 0:

#define ALT_MSG_MAP(msgMapID) \
            break; \
        case msgMapID:

#define END_MSG_MAP() \
            break; \
        default: \
            ATLTRACE2(ATL::atlTraceWindowing, 0, _T("Invalid message map ID (%i)\n"), dwMsgMapID); \
            ATLASSERT(FALSE); \
            break; \
        } \
        return FALSE; \
    }

#define MESSAGE_HANDLER(msg, func) \
    if(uMsg == msg) \
    { \
        bHandled = TRUE; \
        lResult = func(uMsg, wParam, lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define MESSAGE_RANGE_HANDLER(msgFirst, msgLast, func) \
    if(uMsg >= msgFirst && uMsg <= msgLast) \
    { \
        bHandled = TRUE; \
        lResult = func(uMsg, wParam, lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define COMMAND_HANDLER(id, code, func) \
    if(uMsg == WM_COMMAND && id == LOWORD(wParam) && code == HIWORD(wParam)) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define COMMAND_ID_HANDLER(id, func) \
    if(uMsg == WM_COMMAND && id == LOWORD(wParam)) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define COMMAND_CODE_HANDLER(code, func) \
    if(uMsg == WM_COMMAND && code == HIWORD(wParam)) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define COMMAND_RANGE_HANDLER(idFirst, idLast, func) \
    if(uMsg == WM_COMMAND && LOWORD(wParam) >= idFirst && LOWORD(wParam) <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define COMMAND_RANGE_CODE_HANDLER(idFirst, idLast, code, func) \
    if(uMsg == WM_COMMAND && code == HIWORD(wParam) && LOWORD(wParam) >= idFirst && LOWORD(wParam) <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define NOTIFY_HANDLER(id, cd, func) \
    if(uMsg == WM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom && cd == ((LPNMHDR)lParam)->code) \
    { \
        bHandled = TRUE; \
        lResult = func((int)wParam, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define NOTIFY_ID_HANDLER(id, func) \
    if(uMsg == WM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom) \
    { \
        bHandled = TRUE; \
        lResult = func((int)wParam, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define NOTIFY_CODE_HANDLER(cd, func) \
    if(uMsg == WM_NOTIFY && cd == ((LPNMHDR)lParam)->code) \
    { \
        bHandled = TRUE; \
        lResult = func((int)wParam, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define NOTIFY_RANGE_HANDLER(idFirst, idLast, func) \
    if(uMsg == WM_NOTIFY && ((LPNMHDR)lParam)->idFrom >= idFirst && ((LPNMHDR)lParam)->idFrom <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func((int)wParam, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define NOTIFY_RANGE_CODE_HANDLER(idFirst, idLast, cd, func) \
    if(uMsg == WM_NOTIFY && cd == ((LPNMHDR)lParam)->code && ((LPNMHDR)lParam)->idFrom >= idFirst && ((LPNMHDR)lParam)->idFrom <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func((int)wParam, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define CHAIN_MSG_MAP(theChainClass) \
    { \
        if(theChainClass::ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult)) \
            return TRUE; \
    }

#define CHAIN_MSG_MAP_ALT(theChainClass, msgMapID) \
    { \
        if(theChainClass::ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult, msgMapID)) \
            return TRUE; \
    }

#define CHAIN_MSG_MAP_MEMBER(theChainMember) \
    { \
        if(theChainMember.ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult)) \
            return TRUE; \
    }

#define CHAIN_MSG_MAP_ALT_MEMBER(theChainMember, msgMapID) \
    { \
        if(theChainMember.ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult, msgMapID)) \
            return TRUE; \
    }

#define CHAIN_MSG_MAP_DYNAMIC(dynaChainID) \
    { \
        if(CDynamicChain::CallChain(dynaChainID, hWnd, uMsg, wParam, lParam, lResult)) \
            return TRUE; \
    }

// CHAIN_COMMANDS_MEMBER / CHAIN_COMMANDS_ALT_MEMBER are defined in WTL's atlwinx.h

#define FORWARD_NOTIFICATIONS() \
    { \
        bHandled = TRUE; \
        lResult = this->ForwardNotifications(uMsg, wParam, lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECT_NOTIFICATIONS() \
    { \
        bHandled = TRUE; \
        lResult = this->ReflectNotifications(uMsg, wParam, lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define DEFAULT_REFLECTION_HANDLER() \
    if(this->DefaultReflectionHandler(hWnd, uMsg, wParam, lParam, lResult)) \
        return TRUE;

///////////////////////////////////////////////////////////////////////////////
// Reflected message handler macros

#define REFLECTED_COMMAND_HANDLER(id, code, func) \
    if(uMsg == OCM_COMMAND && id == LOWORD(wParam) && code == HIWORD(wParam)) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_COMMAND_ID_HANDLER(id, func) \
    if(uMsg == OCM_COMMAND && id == LOWORD(wParam)) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_COMMAND_CODE_HANDLER(code, func) \
    if(uMsg == OCM_COMMAND && code == HIWORD(wParam)) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_COMMAND_RANGE_HANDLER(idFirst, idLast, func) \
    if(uMsg == OCM_COMMAND && LOWORD(wParam) >= idFirst && LOWORD(wParam) <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_COMMAND_RANGE_CODE_HANDLER(idFirst, idLast, code, func) \
    if(uMsg == OCM_COMMAND && code == HIWORD(wParam) && LOWORD(wParam) >= idFirst && LOWORD(wParam) <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_NOTIFY_HANDLER(id, cd, func) \
    if(uMsg == OCM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom && cd == ((LPNMHDR)lParam)->code) \
    { \
        bHandled = TRUE; \
        lResult = func((int)((LPNMHDR)lParam)->idFrom, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_NOTIFY_ID_HANDLER(id, func) \
    if(uMsg == OCM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom) \
    { \
        bHandled = TRUE; \
        lResult = func((int)((LPNMHDR)lParam)->idFrom, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_NOTIFY_CODE_HANDLER(cd, func) \
    if(uMsg == OCM_NOTIFY && cd == ((LPNMHDR)lParam)->code) \
    { \
        bHandled = TRUE; \
        lResult = func((int)((LPNMHDR)lParam)->idFrom, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_NOTIFY_RANGE_HANDLER(idFirst, idLast, func) \
    if(uMsg == OCM_NOTIFY && ((LPNMHDR)lParam)->idFrom >= idFirst && ((LPNMHDR)lParam)->idFrom <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func((int)((LPNMHDR)lParam)->idFrom, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define REFLECTED_NOTIFY_RANGE_CODE_HANDLER(idFirst, idLast, cd, func) \
    if(uMsg == OCM_NOTIFY && cd == ((LPNMHDR)lParam)->code && ((LPNMHDR)lParam)->idFrom >= idFirst && ((LPNMHDR)lParam)->idFrom <= idLast) \
    { \
        bHandled = TRUE; \
        lResult = func((int)((LPNMHDR)lParam)->idFrom, (LPNMHDR)lParam, bHandled); \
        if(bHandled) \
            return TRUE; \
    }

#define DECLARE_EMPTY_MSG_MAP() \
public: \
    BOOL ProcessWindowMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&, DWORD = 0) \
    { \
        return FALSE; \
    }

///////////////////////////////////////////////////////////////////////////////
// Window Class Macros

#define DECLARE_WND_CLASS(WndClassName) \
static ATL::CWndClassInfo& GetWndClassInfo() \
{ \
    static ATL::CWndClassInfo wc = \
    { \
        { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, StartWindowProc, \
          0, 0, NULL, NULL, NULL, (HBRUSH)(COLOR_WINDOW + 1), NULL, (LPCTSTR)(WndClassName), NULL }, \
        NULL, NULL, IDC_ARROW, TRUE, 0, _T("") \
    }; \
    return wc; \
}

#define DECLARE_WND_CLASS_EX(WndClassName, style, bkgnd) \
static ATL::CWndClassInfo& GetWndClassInfo() \
{ \
    static ATL::CWndClassInfo wc = \
    { \
        { sizeof(WNDCLASSEX), style, StartWindowProc, \
          0, 0, NULL, NULL, NULL, (HBRUSH)(bkgnd + 1), NULL, (LPCTSTR)(WndClassName), NULL }, \
        NULL, NULL, IDC_ARROW, TRUE, 0, _T("") \
    }; \
    return wc; \
}

#define DECLARE_WND_SUPERCLASS(WndClassName, OrigWndClassName) \
static ATL::CWndClassInfo& GetWndClassInfo() \
{ \
    static ATL::CWndClassInfo wc = \
    { \
        { sizeof(WNDCLASSEX), 0, StartWindowProc, \
          0, 0, NULL, NULL, NULL, NULL, NULL, WndClassName, NULL }, \
        OrigWndClassName, NULL, NULL, TRUE, 0, _T("") \
    }; \
    return wc; \
}

namespace ATL { // reopen namespace

///////////////////////////////////////////////////////////////////////////////
// CWindowImpl - implements a window

template <typename T, typename TBase = CWindow, typename TWinTraits = CControlWinTraits>
class ATL_NO_VTABLE CWindowImpl : public CWindowImplBaseT<TBase, TWinTraits> {
public:
    using CWindowImplBaseT<TBase, TWinTraits>::StartWindowProc;

    DECLARE_WND_CLASS(nullptr)

    HWND Create(HWND hWndParent, _U_RECT rect = NULL, LPCTSTR szWindowName = NULL,
        DWORD dwStyle = 0, DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) noexcept
    {
        if (T::GetWndClassInfo().m_lpszOrigName == NULL)
            T::GetWndClassInfo().m_lpszOrigName = TBase::GetWndClassName();

        ATOM atom = T::GetWndClassInfo().Register(&this->m_pfnSuperWindowProc);

        dwStyle = T::GetWndStyle(dwStyle);
        dwExStyle = T::GetWndExStyle(dwExStyle);

        return CWindowImplBaseT<TBase, TWinTraits>::Create(hWndParent, rect, szWindowName,
            dwStyle, dwExStyle, MenuOrID, atom, lpCreateParam);
    }
};

///////////////////////////////////////////////////////////////////////////////
// CDialogImplBaseT - base class for CDialogImpl

template <typename TBase = CWindow>
class ATL_NO_VTABLE CDialogImplBaseT : public CWindowImplRoot<TBase> {
public:
    virtual DLGPROC GetDialogProc()
    {
        return DialogProc;
    }

    static INT_PTR CALLBACK StartDialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CDialogImplBaseT<TBase>* pThis =
            (CDialogImplBaseT<TBase>*)_AtlWinModule.ExtractCreateWndData();
        ATLASSERT(pThis != NULL);
        if (pThis == NULL)
            return 0;

        pThis->m_hWnd = hWnd;
        pThis->m_thunk.Init((WNDPROC)pThis->GetDialogProc(), pThis);
        DLGPROC pProc = (DLGPROC)pThis->m_thunk.GetWndProc();
        ::SetWindowLongPtr(hWnd, DWLP_DLGPROC, (LONG_PTR)pProc);

        return pProc(hWnd, uMsg, wParam, lParam);
    }

    static INT_PTR CALLBACK DialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CDialogImplBaseT<TBase>* pThis = (CDialogImplBaseT<TBase>*)hWnd;
        _ATL_MSG msg(pThis->m_hWnd, uMsg, wParam, lParam);
        const _ATL_MSG* pOldMsg = pThis->m_pCurrentMsg;
        pThis->m_pCurrentMsg = &msg;

        LRESULT lRes = 0;
        BOOL bRet = pThis->ProcessWindowMessage(pThis->m_hWnd, uMsg, wParam, lParam, lRes, 0);

        ATLASSERT(pThis->m_pCurrentMsg == &msg);
        pThis->m_pCurrentMsg = pOldMsg;

        if (bRet) {
            switch (uMsg) {
            case WM_COMPAREITEM:
            case WM_VKEYTOITEM:
            case WM_CHARTOITEM:
            case WM_INITDIALOG:
            case WM_QUERYDRAGICON:
            case WM_CTLCOLORMSGBOX:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSCROLLBAR:
            case WM_CTLCOLORSTATIC:
                // Return the result directly for these messages
                return (INT_PTR)lRes;
            default:
                break;
            }
            if (lRes != 0) {
                ::SetWindowLongPtr(pThis->m_hWnd, DWLP_MSGRESULT, lRes);
            }
            return TRUE;
        }

        if (uMsg == WM_NCDESTROY) {
            pThis->m_dwState |= CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
        }

        if ((pThis->m_dwState & CWindowImplRoot<TBase>::WINSTATE_DESTROYED) && (pThis->m_pCurrentMsg == NULL)) {
            HWND hWndThis = pThis->m_hWnd;
            pThis->m_hWnd = NULL;
            pThis->m_dwState &= ~CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
            pThis->OnFinalMessage(hWndThis);
        }

        return FALSE;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CDialogImpl - implements a dialog

template <typename T, typename TBase = CWindow>
class ATL_NO_VTABLE CDialogImpl : public CDialogImplBaseT<TBase> {
public:
    HWND Create(HWND hWndParent, LPARAM dwInitParam = NULL) noexcept
    {
        ATLASSERT(this->m_hWnd == NULL);
        BOOL bRet = this->m_thunk.Init(NULL, NULL);
        if (bRet == FALSE) {
            ::SetLastError(ERROR_OUTOFMEMORY);
            return NULL;
        }

        _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, this);

#ifdef _UNICODE
        HWND hWnd = ::CreateDialogParamW(_AtlBaseModule.GetResourceInstance(),
            MAKEINTRESOURCEW(static_cast<T*>(this)->IDD), hWndParent,
            T::StartDialogProc, dwInitParam);
#else
        HWND hWnd = ::CreateDialogParamA(_AtlBaseModule.GetResourceInstance(),
            MAKEINTRESOURCEA(static_cast<T*>(this)->IDD), hWndParent,
            T::StartDialogProc, dwInitParam);
#endif

        ATLASSERT((hWnd == NULL) || (this->m_hWnd == hWnd));
        return hWnd;
    }

    BOOL DestroyWindow() noexcept
    {
        ATLASSERT(::IsWindow(this->m_hWnd));
        return ::DestroyWindow(this->m_hWnd);
    }

    INT_PTR DoModal(HWND hWndParent = ::GetActiveWindow(), LPARAM dwInitParam = NULL) noexcept
    {
        ATLASSERT(this->m_hWnd == NULL);
        BOOL bRet = this->m_thunk.Init(NULL, NULL);
        if (bRet == FALSE) {
            ::SetLastError(ERROR_OUTOFMEMORY);
            return -1;
        }

        _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, this);

#ifdef _UNICODE
        INT_PTR nRet = ::DialogBoxParamW(_AtlBaseModule.GetResourceInstance(),
            MAKEINTRESOURCEW(static_cast<T*>(this)->IDD), hWndParent,
            T::StartDialogProc, dwInitParam);
#else
        INT_PTR nRet = ::DialogBoxParamA(_AtlBaseModule.GetResourceInstance(),
            MAKEINTRESOURCEA(static_cast<T*>(this)->IDD), hWndParent,
            T::StartDialogProc, dwInitParam);
#endif

        return nRet;
    }

    BOOL EndDialog(int nRetCode) noexcept
    {
        ATLASSERT(::IsWindow(this->m_hWnd));
        return ::EndDialog(this->m_hWnd, nRetCode);
    }
};

///////////////////////////////////////////////////////////////////////////////
// CContainedWindowT - for containment support

template <typename TBase = CWindow, typename TWinTraits = CControlWinTraits>
class CContainedWindowT : public TBase {
public:
    CMessageMap* m_pObject;
    DWORD m_dwMsgMapID;
    WNDPROC m_pfnSuperWindowProc;

    CWndProcThunk m_thunk;
    const _ATL_MSG* m_pCurrentMsg;
    DWORD m_dwState;

    enum { WINSTATE_DESTROYED = 0x00000001 };

    CContainedWindowT(CMessageMap* pObject, DWORD dwMsgMapID = 0)
        : m_pObject(pObject), m_dwMsgMapID(dwMsgMapID),
          m_pfnSuperWindowProc(::DefWindowProc), m_pCurrentMsg(NULL), m_dwState(0)
    {
    }

    CContainedWindowT(LPTSTR lpszClassName, CMessageMap* pObject, DWORD dwMsgMapID = 0)
        : m_pObject(pObject), m_dwMsgMapID(dwMsgMapID),
          m_pfnSuperWindowProc(::DefWindowProc), m_pCurrentMsg(NULL), m_dwState(0)
    {
        (void)lpszClassName;
    }

    LRESULT DefWindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept
    {
        return ::CallWindowProc(m_pfnSuperWindowProc, this->m_hWnd, uMsg, wParam, lParam);
    }

    BOOL SubclassWindow(HWND hWnd) noexcept
    {
        ATLASSERT(this->m_hWnd == NULL);
        ATLASSERT(::IsWindow(hWnd));

        BOOL bRet = m_thunk.Init(WindowProc, this);
        if (bRet == FALSE)
            return FALSE;

        WNDPROC pProc = m_thunk.GetWndProc();
        WNDPROC pfnWndProc = (WNDPROC)::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pProc);
        if (pfnWndProc == NULL)
            return FALSE;

        m_pfnSuperWindowProc = pfnWndProc;
        this->m_hWnd = hWnd;
        return TRUE;
    }

    HWND UnsubclassWindow(BOOL bForce = FALSE) noexcept
    {
        ATLASSERT(this->m_hWnd != NULL);
        WNDPROC pOurProc = m_thunk.GetWndProc();
        WNDPROC pActiveProc = (WNDPROC)::GetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC);

        HWND hWnd = NULL;
        if (bForce || pActiveProc == pOurProc) {
            if (!::SetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_pfnSuperWindowProc))
                return NULL;
            m_pfnSuperWindowProc = ::DefWindowProc;
            hWnd = this->m_hWnd;
            this->m_hWnd = NULL;
        }
        return hWnd;
    }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CContainedWindowT<TBase, TWinTraits>* pThis =
            (CContainedWindowT<TBase, TWinTraits>*)hWnd;

        _ATL_MSG msg(pThis->m_hWnd, uMsg, wParam, lParam);
        const _ATL_MSG* pOldMsg = pThis->m_pCurrentMsg;
        pThis->m_pCurrentMsg = &msg;

        LRESULT lRes = 0;
        BOOL bRet = pThis->m_pObject->ProcessWindowMessage(pThis->m_hWnd, uMsg, wParam, lParam, lRes, pThis->m_dwMsgMapID);

        ATLASSERT(pThis->m_pCurrentMsg == &msg);
        pThis->m_pCurrentMsg = pOldMsg;

        if (!bRet) {
            if (uMsg != WM_NCDESTROY) {
                lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
            } else {
                LONG_PTR pfnWndProc = ::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC);
                lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
                if (pThis->m_pfnSuperWindowProc != ::DefWindowProc &&
                    ::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC) == pfnWndProc) {
                    ::SetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC, (LONG_PTR)pThis->m_pfnSuperWindowProc);
                }
                pThis->m_dwState |= WINSTATE_DESTROYED;
            }
        }

        if ((pThis->m_dwState & WINSTATE_DESTROYED) && (pThis->m_pCurrentMsg == NULL)) {
            pThis->m_hWnd = NULL;
            pThis->m_dwState &= ~WINSTATE_DESTROYED;
        }

        return lRes;
    }
};

typedef CContainedWindowT<CWindow> CContainedWindow;

// Standard typedefs expected by WTL
typedef CWindowImplBaseT<CWindow> CWindowImplBase;
typedef CDialogImplBaseT<CWindow> CDialogImplBase;

///////////////////////////////////////////////////////////////////////////////
// CAxWindow2 - stub for ActiveX hosting (not supported, provides interface only)

class CAxWindow2 : public CWindow {
public:
    CAxWindow2(HWND hWnd = NULL) noexcept : CWindow(hWnd) {}

    HWND Create(HWND hWndParent, _U_RECT rect = NULL, LPCTSTR szWindowName = NULL,
        DWORD dwStyle = 0, DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) noexcept
    {
        (void)hWndParent; (void)rect; (void)szWindowName;
        (void)dwStyle; (void)dwExStyle; (void)MenuOrID; (void)lpCreateParam;
        return NULL;
    }

    HRESULT CreateControlLic(LPCOLESTR /*lpszName*/, IStream* /*pStream*/ = NULL,
        IUnknown** /*ppUnkContainer*/ = NULL, BSTR /*bstrLicKey*/ = NULL) noexcept
    {
        return E_NOTIMPL;
    }
};

} // namespace ATL

#endif // __ATLWIN_H__

#pragma once
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <memory>
#include <algorithm>

template<class Handle, auto Close>
struct WinDeleter {
    using pointer = Handle;
    void operator()(Handle handle) const { Close(handle); }
};
using UniqueMenu = std::unique_ptr<std::remove_pointer_t<HMENU>, WinDeleter<HMENU, DestroyMenu>>;
using UniqueFont = std::unique_ptr<std::remove_pointer_t<HFONT>, WinDeleter<HFONT, DeleteObject>>;
using UniqueHandle = std::unique_ptr<void, WinDeleter<HANDLE, CloseHandle>>;

class WinWindow {
public:
    HWND m_hWnd = nullptr;
    operator HWND() const { return m_hWnd; }
    BOOL IsWindow() const { return ::IsWindow(m_hWnd); }
    BOOL IsWindowVisible() const { return ::IsWindowVisible(m_hWnd); }
    BOOL IsWindowEnabled() const { return ::IsWindowEnabled(m_hWnd); }
    BOOL IsIconic() const { return ::IsIconic(m_hWnd); }
    BOOL IsZoomed() const { return ::IsZoomed(m_hWnd); }
    BOOL IsChild(HWND child) const { return ::IsChild(m_hWnd, child); }
    BOOL ShowWindow(int command) const { return ::ShowWindow(m_hWnd, command); }
    BOOL EnableWindow(BOOL enabled = TRUE) const { return ::EnableWindow(m_hWnd, enabled); }
    BOOL DestroyWindow() const { return ::DestroyWindow(m_hWnd); }
    BOOL UpdateWindow() const { return ::UpdateWindow(m_hWnd); }
    BOOL Invalidate(BOOL erase = TRUE) const { return ::InvalidateRect(m_hWnd, nullptr, erase); }
    BOOL GetClientRect(RECT* rect) const { return ::GetClientRect(m_hWnd, rect); }
    BOOL GetWindowRect(RECT* rect) const { return ::GetWindowRect(m_hWnd, rect); }
    BOOL SetWindowPos(HWND after, int x, int y, int width, int height, UINT flags) const {
        return ::SetWindowPos(m_hWnd, after, x, y, width, height, flags);
    }
    BOOL SetWindowPos(HWND after, const RECT* rect, UINT flags) const {
        return SetWindowPos(after, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, flags);
    }
    BOOL GetWindowPlacement(WINDOWPLACEMENT* placement) const { return ::GetWindowPlacement(m_hWnd, placement); }
    BOOL SetWindowPlacement(const WINDOWPLACEMENT* placement) const { return ::SetWindowPlacement(m_hWnd, placement); }
    BOOL ClientToScreen(POINT* point) const { return ::ClientToScreen(m_hWnd, point); }
    BOOL ScreenToClient(POINT* point) const { return ::ScreenToClient(m_hWnd, point); }
    HMENU GetMenu() const { return ::GetMenu(m_hWnd); }
    BOOL SetMenu(HMENU menu) const { return ::SetMenu(m_hWnd, menu); }
    BOOL DrawMenuBar() const { return ::DrawMenuBar(m_hWnd); }
    HWND GetWindow(UINT relation) const { return ::GetWindow(m_hWnd, relation); }
    DWORD GetStyle() const { return static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_STYLE)); }
    DWORD GetExStyle() const { return static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE)); }
    LRESULT SendMessage(UINT message, WPARAM w = 0, LPARAM l = 0) const { return ::SendMessageW(m_hWnd, message, w, l); }
    BOOL PostMessage(UINT message, WPARAM w = 0, LPARAM l = 0) const { return ::PostMessageW(m_hWnd, message, w, l); }
    int MessageBox(const wchar_t* text, const wchar_t* title, UINT flags) const { return ::MessageBoxW(m_hWnd, text, title, flags); }
    UINT_PTR SetTimer(UINT_PTR id, UINT interval) const { return ::SetTimer(m_hWnd, id, interval, nullptr); }
    BOOL KillTimer(UINT_PTR id) const { return ::KillTimer(m_hWnd, id); }
    void DragAcceptFiles(BOOL accept) const { ::DragAcceptFiles(m_hWnd, accept); }
};

inline void CenterWindow(HWND window, HWND owner) {
    RECT dialog{}, relative{};
    if (!GetWindowRect(window, &dialog)) return;
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(owner ? owner : window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    if (!owner || IsIconic(owner) || !GetWindowRect(owner, &relative)) relative = monitor.rcWork;
    const LONG width = dialog.right - dialog.left, height = dialog.bottom - dialog.top;
    const auto& work = monitor.rcWork;
    const LONG x = std::clamp(relative.left + (relative.right - relative.left - width) / 2, work.left, std::max(work.left, work.right - width));
    const LONG y = std::clamp(relative.top + (relative.bottom - relative.top - height) / 2, work.top, std::max(work.top, work.bottom - height));
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

inline void TreeGetItemText(HWND tree, HTREEITEM node, wchar_t* buffer, int capacity) {
    TVITEMW item{TVIF_TEXT, node};
    item.pszText = buffer;
    item.cchTextMax = capacity;
    TreeView_GetItem(tree, &item);
}

inline LPARAM TreeGetItemData(HWND tree, HTREEITEM node) {
    TVITEMW item{TVIF_PARAM, node};
    TreeView_GetItem(tree, &item);
    return item.lParam;
}

inline void TreeSetItemText(HWND tree, HTREEITEM node, wchar_t* text) {
    TVITEMW item{TVIF_TEXT, node};
    item.pszText = text;
    TreeView_SetItem(tree, &item);
}

inline void TreeSetItemData(HWND tree, HTREEITEM node, LPARAM data) {
    TVITEMW item{TVIF_PARAM, node};
    item.lParam = data;
    TreeView_SetItem(tree, &item);
}

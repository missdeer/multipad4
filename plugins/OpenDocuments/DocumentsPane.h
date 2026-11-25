#pragma once
#include "PluginApp.h"
#include "../PluginApi.h"
enum class DockSide { Left, Right, Bottom, Floating };
class DockHost;
class DockPane : public PaneWindow<DockPane> {
public:
    static constexpr auto WindowClass = L"MultiPad4.OpenDocumentsPane";
    HWND owner = nullptr;
    DockHost* host = nullptr;
    HWND list = nullptr;
    bool floating = false;
    void Refresh();
    void UpdateSelection();
    void ActivateSelection();
    LRESULT Dispatch(UINT message, WPARAM w, LPARAM l, BOOL& handled) {
        switch (message) {
        case WM_CREATE: return OnCreate(message, w, l, handled);
        case WM_SIZE: return OnSize(message, w, l, handled);
        case WM_CLOSE: return OnClose(message, w, l, handled);
        case WM_CONTEXTMENU: return OnContext(message, w, l, handled);
        case WM_APP + 0x102: return OnAction(message, w, l, handled);
        case WM_COMMAND: {
            const WORD id = LOWORD(w), code = HIWORD(w);
            const auto control = reinterpret_cast<HWND>(l);
            if (code == LBN_DBLCLK) return OnSelect(code, id, control, handled);
            break;
        }
        }
        handled = FALSE;
        return 0;
    }
    LRESULT OnAction(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContext(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSelect(WORD, WORD, HWND, BOOL&);
};
class DockHost {
public:
    HWND owner = nullptr;
    DockPane pane;
    DockSide side = DockSide::Left;
    bool visible = true;
    int extent = 220;
    void Create(HWND frame, HINSTANCE instance);
    void Layout() { pane.floating = !pane.IsDocking(); }
    void SetSide(DockSide value);
    void Toggle();
};

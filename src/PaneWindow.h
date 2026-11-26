#pragma once
#include "DockLayout.h"
#include "WinWindow.h"
#include "DpiFont.h"

template<class Derived>
class PaneWindow : public WinWindow {
public:
    HWND Create(HWND owner, RECT rect, const wchar_t* title, HINSTANCE instance = GetModuleHandleW(nullptr)) {
        const auto name = Derived::WindowClass;
        WNDCLASSW type{};
        type.style = CS_DBLCLKS;
        type.lpfnWndProc = WindowProcedure;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        type.lpszClassName = name;
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, name, title,
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, owner, nullptr, instance, this);
    }

    bool IsDocking() const { return Placement().side != PaneSide::Floating; }
    DockPlacement Placement() const { return interaction.Placement(); }
    bool Place(const DockPlacement& placement) { return interaction.Apply(placement); }
    void Hide() { SetVisible(false); }
    void Show() { SetVisible(true); }

private:
    PaneInteraction interaction;
    DpiGuiFont guiFont;
    void UpdateGuiFont(UINT dpi) {
        if (!guiFont.Update(dpi)) return;
        for (HWND child = ::GetWindow(m_hWnd, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
            wchar_t type[32]{}; ::GetClassNameW(child, type, 32);
            if (_wcsicmp(type, L"Scintilla") != 0) guiFont.Apply(child);
        }
        ::RedrawWindow(m_hWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void SetVisible(bool visible) {
        auto placement = Placement();
        if (placement.visible == visible) return;
        placement.visible = visible;
        Place(placement);
    }

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<PaneWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l);
            self = static_cast<PaneWindow*>(create->lpCreateParams);
            self->m_hWnd = window;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->interaction.Attach(window, create->hwndParent);
        }
        if (!self) return DefWindowProcW(window, message, w, l);
        if (message == WM_GETFONT) return reinterpret_cast<LRESULT>(self->guiFont.Get());
        if (message == WM_DPICHANGED || message == WM_DPICHANGED_AFTERPARENT || message == WM_SETTINGCHANGE)
            self->UpdateGuiFont(message == WM_DPICHANGED ? HIWORD(w) : GetDpiForWindow(window));
        LRESULT result{};
        if (self->interaction.Dispatch(message, w, l, result)) return result;
        BOOL handled = TRUE;
        result = static_cast<Derived*>(self)->Dispatch(message, w, l, handled);
        if (message == WM_CREATE && result != -1) self->UpdateGuiFont(GetDpiForWindow(window));
        if (handled) return result;
        if (message == WM_NCDESTROY) {
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->m_hWnd = nullptr;
        }
        return DefWindowProcW(window, message, w, l);
    }
};

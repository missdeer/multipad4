#pragma once
#include "DockLayout.h"
#include "WinWindow.h"

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
        LRESULT result{};
        if (self->interaction.Dispatch(message, w, l, result)) return result;
        BOOL handled = TRUE;
        result = static_cast<Derived*>(self)->Dispatch(message, w, l, handled);
        if (handled) return result;
        if (message == WM_NCDESTROY) {
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->m_hWnd = nullptr;
        }
        return DefWindowProcW(window, message, w, l);
    }
};

#pragma once
#include "PluginApp.h"
#include <filesystem>

constexpr UINT WM_MATEPATH_ACTION = WM_APP + 0x110;
enum class DockSide { Left, Right, Bottom, Floating };
class MatepathPane : public PaneWindow<MatepathPane> {
public:
    static constexpr auto WindowClass = L"MultiPad4.MatepathPane";
    std::unique_ptr<std::remove_pointer_t<HMODULE>, WinDeleter<HMODULE, FreeLibrary>> richEditLibrary{
        LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    enum { Back = 100, Forward, Up, Reload, Go, Filter, Hidden, Properties, SortName, SortSize, SortType, SortDate, Reverse, Follow, Address, Pattern, Browse };
    HWND owner = nullptr;
    HWND list = nullptr, address = nullptr, pattern = nullptr;
    HWND back = nullptr, forward = nullptr, up = nullptr, reload = nullptr, go = nullptr, apply = nullptr;
    HWND browse = nullptr, tooltips = nullptr;
    UniqueFont iconFont;
    int iconDpi = 0;
    std::filesystem::path directory;
    std::wstring fileSpec = L"*.*";
    bool visible = true, showHidden = false, reverse = false;
    int sort = 0;
    DockSide side = DockSide::Right;
    DockSide CurrentSide() const;
    std::vector<std::filesystem::path> history;
    size_t historyIndex = 0;
    void CreatePane(HWND frame, HINSTANCE instance);
    void DestroyPane();
    void SetSide(DockSide value, bool show = true);
    void Toggle();
    void Layout();
    bool Navigate(const std::filesystem::path& path, bool remember = true);
    void RefreshItems();
    void ActivateSelection();
    void Reveal(const std::filesystem::path& path);
    BOOL PreTranslateMessage(MSG* msg);
    void FollowActive();
    LRESULT OnAction(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT Dispatch(UINT message, WPARAM w, LPARAM l, BOOL& handled) {
        switch (message) {
        case WM_MATEPATH_ACTION: return OnAction(message, w, l, handled);
        case WM_CREATE: return OnCreate(message, w, l, handled);
        case WM_ERASEBKGND: return OnEraseBackground(message, w, l, handled);
        case WM_DRAWITEM: return OnDrawButton(message, w, l, handled);
        case WM_SIZE: return OnSize(message, w, l, handled);
        case WM_DESTROY: return OnDestroy(message, w, l, handled);
        case WM_CLOSE: return OnClose(message, w, l, handled);
        case WM_NOTIFY: return OnNotify(message, w, l, handled);
        case WM_CONTEXTMENU: return OnContext(message, w, l, handled);
        case WM_DPICHANGED: return OnDpi(message, w, l, handled);
        case WM_DPICHANGED_AFTERPARENT: return OnDpi(message, w, l, handled);
        case WM_COMMAND: {
            const WORD id = LOWORD(w), code = HIWORD(w);
            const auto control = reinterpret_cast<HWND>(l);
            if ((id >= Back && id <= Follow) || id == Browse || id == Pattern) return OnCommand(code, id, control, handled);
            break;
        }
        }
        handled = FALSE;
        return 0;
    }
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnEraseBackground(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDrawButton(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContext(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDpi(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCommand(WORD, WORD, HWND, BOOL&);
};

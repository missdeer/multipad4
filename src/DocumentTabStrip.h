#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>

class DocumentTabStrip {
public:
    static constexpr int titleMinPoints = 6, titleMaxPoints = 32;
    static constexpr int defaultTabWidth = 100, minTabWidth = 40, maxTabWidth = 1000;
    bool Create(HWND frame, HWND mdi);
    HWND LayoutWindow() const { return layoutWindow; }
    operator HWND() const { return control; }
    int GetItemCount() const;
    int GetCurSel() const;
    HWND ChildAt(int index) const;
    bool Insert(HWND child, const wchar_t* title);
    void Remove(int index);
    void SetTitle(int index, const wchar_t* title);
    void SetIcon(HWND child, HICON icon);
    void UpdateImages();
    void SyncSelection(HWND active = nullptr);
    void Activate(int index);
    bool MoveItem(int from, int to);
    BOOL GetItemRect(int index, RECT* rect) const;
    void EnsureVisible(int index, bool, bool);
    LRESULT SendMessage(UINT message, WPARAM w = 0, LPARAM l = 0) const { return ::SendMessageW(control, message, w, l); }
    bool HasTitleFont() const { return titlePoints != 0; }
    HFONT GetFont() const { return titleFont; }
    int TitleFontPoints() const { return titlePoints; }
    UINT TitleDpi() const { return GetDpiForWindow(frame); }
    int TitleFontHeight() const;
    bool HasFixedTabWidth() const { return fixedTabWidth != 0; }
    void SetTitleFont(const std::wstring& face, long points);
    void SetFixedTabWidth(int width);
    void ApplyLayout();
    ~DocumentTabStrip();

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK TabProcedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data);
    void LayoutCloseButton(int width, int height);
    int HitTest(POINT point) const;
    HWND frame = nullptr, mdi = nullptr, layoutWindow = nullptr, bar = nullptr, control = nullptr, close = nullptr;
    HFONT titleFont = nullptr;
    HIMAGELIST icons = nullptr;
    std::wstring tooltip;
    int titlePoints = 0, fixedTabWidth = 0, dragIndex = -1;
    POINT dragOrigin{};
    bool dragging = false;
};

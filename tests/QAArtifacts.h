#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
inline std::filesystem::path qaArtifactDirectory;
inline unsigned qaArtifactSequence = 0;
// Render only a window owned by this test process. This is an application
// artifact export, never a desktop capture or interaction with another app.
inline void CaptureQAWindow(HWND window, const wchar_t* label) {
    if (qaArtifactDirectory.empty()) return;
    DWORD process = 0; GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) throw std::runtime_error("QA capture requires a window from this process.");
    RECT rect{}; GetWindowRect(window, &rect);
    const LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width < 1 || height < 1 || width > 10000 || height > 10000) return;
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr; HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap && memory) {
        auto old = SelectObject(memory, bitmap);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        RECT canvas{0, 0, width, height}; FillRect(memory, &canvas, GetSysColorBrush(COLOR_3DFACE));
        BOOL rendered = PrintWindow(window, memory, 0);
        // Classic controls support synchronous WM_PRINT even when the desktop
        // compositor has no current surface for a newly opened modal dialog.
        SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
        if (!rendered) {
            wchar_t type[16]{};
            GetClassNameW(window, type, _countof(type));
            if (wcscmp(type, L"#32768") == 0) {
                // Native popup menus can reject PrintWindow.
                HDC popup = GetWindowDC(window);
                if (popup) {
                    rendered = BitBlt(memory, 0, 0, width, height, popup, 0, 0, SRCCOPY);
                    ReleaseDC(window, popup);
                }
            }
        }
        if (rendered) {
            std::filesystem::create_directories(qaArtifactDirectory);
            BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
            header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
            const DWORD bytes = static_cast<DWORD>(width * height * 4); header.bfSize = header.bfOffBits + bytes;
            std::ofstream out(qaArtifactDirectory / (std::to_wstring(++qaArtifactSequence) + L"-" + label + L".bmp"), std::ios::binary);
            out.write(reinterpret_cast<const char*>(&header), sizeof(header));
            out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
            out.write(static_cast<const char*>(pixels), bytes);
            std::wofstream layout(qaArtifactDirectory / (std::to_wstring(qaArtifactSequence) + L"-layout.txt"));
            layout << L"Window " << width << L"x" << height << L"\n";
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
                RECT childRect{}; GetWindowRect(child, &childRect); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&childRect), 2);
                wchar_t text[512]{}, type[64]{}; GetWindowTextW(child, text, _countof(text)); GetClassNameW(child, type, _countof(type));
                layout << GetDlgCtrlID(child) << L" " << type << L" visible=" << ((GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) != 0)
                    << L" rect=" << childRect.left << L"," << childRect.top << L"," << childRect.right << L"," << childRect.bottom << L" " << text << L"\n";
            }
        }
        SelectObject(memory, old); DeleteObject(bitmap);
    }
    if (memory) DeleteDC(memory); ReleaseDC(window, screen);
}

inline void CaptureQAPopup(HWND owner, HMENU menu, const wchar_t* label) {
    if (qaArtifactDirectory.empty()) return;
    static const wchar_t* popupLabel = nullptr;
    popupLabel = label;
    const auto popupTimer = SetTimer(nullptr, 0, 50, [](HWND, UINT, UINT_PTR id, DWORD) {
        KillTimer(nullptr, id);
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM parameter) -> BOOL {
            wchar_t type[16]{};
            GetClassNameW(window, type, _countof(type));
            if (!IsWindowVisible(window) || wcscmp(type, L"#32768") != 0) return TRUE;
            CaptureQAWindow(window, reinterpret_cast<const wchar_t*>(parameter));
            return FALSE;
        }, reinterpret_cast<LPARAM>(popupLabel));
        EndMenu();
    });
    if (!popupTimer) throw std::runtime_error("Cannot start QA popup timer");
    RECT bounds{};
    GetWindowRect(owner, &bounds);
    TrackPopupMenuEx(menu, TPM_RETURNCMD, bounds.left + 240, bounds.top + 55, owner, nullptr);
    KillTimer(nullptr, popupTimer);
    popupLabel = nullptr;
}

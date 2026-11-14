#pragma once
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <locale>
#include <cmath>

// Persist values, never the framework's HWNDs (which belong to one process).
namespace pluginLayout {
inline std::filesystem::path ConfigPath(HWND host, const wchar_t* filename) {
    wchar_t commandLine[32768]{}, directory[32768]{};
    SendMessageW(host, NPPM_GETCURRENTCMDLINE, _countof(commandLine), reinterpret_cast<LPARAM>(commandLine));
    if (wcsstr(commandLine, L"--smoke-test") || wcsstr(commandLine, L"--qa-render") ||
        wcsstr(commandLine, L"--plugin-test") || wcsstr(commandLine, L"--no-plugins-test")) return {};
    if (!SendMessageW(host, NPPM_GETPLUGINSCONFIGDIR, _countof(directory), reinterpret_cast<LPARAM>(directory))) return {};
    return std::filesystem::path(directory) / filename;
}
inline std::wstring Read(const std::filesystem::path& path, const wchar_t* section) {
    if (path.empty()) return {};
    wchar_t value[1024]{};
    GetPrivateProfileStringW(section, L"DockLayout", L"", value, _countof(value), path.c_str());
    return value;
}
template<class Pane>
bool Save(const std::filesystem::path& path, const wchar_t* section, const Pane& pane, bool visible) {
    if (path.empty()) return true;
    dockwins::DFDOCKPOSEX state{};
    if (!pane.GetDockingWindowPlacement(&state)) return false;
    // Hide() temporarily undocks the window and retains the dock position.
    const bool docked = state.bDocking || (!visible && state.dockPos.hdr.hBar != HNONDOCKBAR);
    const auto& pos = state.dockPos;
    std::wostringstream out; out.imbue(std::locale::classic()); out.precision(9);
    out << 1 << L' ' << visible << L' ' << docked << L' ' << GetDpiForWindow(pane.m_hWnd)
        << L' ' << state.rect.left << L' ' << state.rect.top << L' ' << state.rect.right << L' ' << state.rect.bottom
        << L' ' << (pos.dwDockSide & 3) << L' ' << pos.nBar << L' ' << pos.fPctPos
        << L' ' << pos.nWidth << L' ' << pos.nHeight << L' ' << pos.nIndex;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const WORD bom = 0xFEFF; DWORD written = 0;
        WriteFile(file, &bom, sizeof(bom), &written, nullptr); CloseHandle(file);
    }
    return WritePrivateProfileStringW(section, L"DockLayout", out.str().c_str(), path.c_str()) != FALSE;
}
template<class Pane>
bool Restore(const std::filesystem::path& path, const wchar_t* section, Pane& pane, HWND owner, bool& visible) {
    std::wistringstream input(Read(path, section)); input.imbue(std::locale::classic());
    dockwins::DFDOCKPOSEX state{};
    int version = 0, show = 0, docked = 0, dpi = 0;
    auto& pos = state.dockPos;
    if (!(input >> version >> show >> docked >> dpi >> state.rect.left >> state.rect.top >> state.rect.right >> state.rect.bottom
        >> pos.dwDockSide >> pos.nBar >> pos.fPctPos >> pos.nWidth >> pos.nHeight >> pos.nIndex)) return false;
    input >> std::ws;
    const auto width = static_cast<long long>(state.rect.right) - state.rect.left;
    const auto height = static_cast<long long>(state.rect.bottom) - state.rect.top;
    if (!input.eof() || version != 1 || show < 0 || show > 1 || docked < 0 || docked > 1 || dpi < 48 || dpi > 768 ||
        width < 64 || height < 64 || width > 32768 || height > 32768 || pos.dwDockSide > 3 ||
        !std::isfinite(pos.fPctPos) || pos.fPctPos < 0 || pos.fPctPos > 1 || pos.nBar > 1024 || pos.nIndex > 1024 ||
        (docked && (pos.nWidth < 16 || pos.nHeight < 16 || pos.nWidth > 32768 || pos.nHeight > 32768))) return false;
    const int currentDpi = static_cast<int>(GetDpiForWindow(owner));
    pos.nWidth = MulDiv(static_cast<int>(pos.nWidth), currentDpi, dpi);
    pos.nHeight = MulDiv(static_cast<int>(pos.nHeight), currentDpi, dpi);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromRect(&state.rect, MONITOR_DEFAULTTONEAREST), &monitor)) {
        // Keep the title and resize edges reachable after display/DPI changes.
        const LONG w = (std::min<LONG>)(MulDiv(static_cast<int>(width), currentDpi, dpi), monitor.rcWork.right - monitor.rcWork.left);
        const LONG h = (std::min<LONG>)(MulDiv(static_cast<int>(height), currentDpi, dpi), monitor.rcWork.bottom - monitor.rcWork.top);
        state.rect.left = (std::clamp)(state.rect.left, monitor.rcWork.left, monitor.rcWork.right - w);
        state.rect.top = (std::clamp)(state.rect.top, monitor.rcWork.top, monitor.rcWork.bottom - h);
        state.rect.right = state.rect.left + w; state.rect.bottom = state.rect.top + h;
    }
    pos.hdr.hWnd = pane.m_hWnd;
    pos.hdr.hBar = docked ? owner : HNONDOCKBAR;
    state.bDocking = docked; state.bVisible = show;
    // Undock first: the framework caches the current position in Undock(),
    // which would otherwise overwrite the restored hidden position.
    if (pane.IsDocking()) pane.Undock();
    if (!pane.SetDockingWindowPlacement(&state)) return false;
    visible = show != 0;
    return true;
}
}

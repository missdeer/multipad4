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
    const auto state = pane.Placement();
    const bool docked = state.side != PaneSide::Floating;
    const int side = docked ? (3 - static_cast<int>(state.side)) : 0;
    std::wostringstream out; out.imbue(std::locale::classic()); out.precision(9);
    out << 1 << L' ' << visible << L' ' << docked << L' ' << GetDpiForWindow(pane.m_hWnd)
        << L' ' << state.floating.left << L' ' << state.floating.top << L' ' << state.floating.right << L' ' << state.floating.bottom
        << L' ' << side << L' ' << state.bar << L' ' << state.position
        << L' ' << state.width << L' ' << state.height << L' ' << state.index;
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
    DockPlacement state{};
    int version = 0, show = 0, docked = 0, dpi = 0, side = 0;
    auto& rect = state.floating;
    if (!(input >> version >> show >> docked >> dpi >> rect.left >> rect.top >> rect.right >> rect.bottom
        >> side >> state.bar >> state.position >> state.width >> state.height >> state.index)) return false;
    input >> std::ws;
    const auto width = static_cast<long long>(rect.right) - rect.left;
    const auto height = static_cast<long long>(rect.bottom) - rect.top;
    if (!input.eof() || version != 1 || show < 0 || show > 1 || docked < 0 || docked > 1 || dpi < 48 || dpi > 768 ||
        width < 64 || height < 64 || width > 32768 || height > 32768 || side < 0 || side > 3 ||
        !std::isfinite(state.position) || state.position < 0 || state.position > 1 || state.bar > 1024 || state.index > 1024 ||
        state.width < 0 || state.height < 0 || state.width > 32768 || state.height > 32768 ||
        (docked && (state.width < 16 || state.height < 16))) return false;
    const int currentDpi = static_cast<int>(GetDpiForWindow(owner));
    state.width = MulDiv(state.width, currentDpi, dpi);
    state.height = MulDiv(state.height, currentDpi, dpi);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor)) {
        // Keep the title and resize edges reachable after display/DPI changes.
        const LONG w = (std::min<LONG>)(MulDiv(static_cast<int>(width), currentDpi, dpi), monitor.rcWork.right - monitor.rcWork.left);
        const LONG h = (std::min<LONG>)(MulDiv(static_cast<int>(height), currentDpi, dpi), monitor.rcWork.bottom - monitor.rcWork.top);
        rect.left = (std::clamp)(rect.left, monitor.rcWork.left, monitor.rcWork.right - w);
        rect.top = (std::clamp)(rect.top, monitor.rcWork.top, monitor.rcWork.bottom - h);
        rect.right = rect.left + w; rect.bottom = rect.top + h;
    }
    state.side = docked ? static_cast<PaneSide>(3 - side) : PaneSide::Floating;
    state.visible = show != 0;
    if (!pane.Place(state)) return false;
    visible = show != 0;
    return true;
}
}

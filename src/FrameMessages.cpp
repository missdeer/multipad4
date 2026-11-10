#include "MainFrame.h"
#include "CommandLine.h"
#include "FrameCommands.h"
#include "UpstreamUI.h"
#include <windowsx.h>
LRESULT MainFrame::OnClipboardUpdate(UINT, WPARAM, LPARAM, BOOL&) {
    DWORD sequence = GetClipboardSequenceNumber();
    if (!runtime.pasteBoard || sequence == runtime.clipboardSequence) return 0;
    runtime.clipboardSequence = sequence;
    for (auto& document : documents) {
        auto& d = *document;
        if (d.hwnd != runtime.pasteBoard || d.Send(SCI_GETREADONLY)) continue;
        if (!::OpenClipboard(m_hWnd)) return 0;
        std::wstring wide;
        if (auto memory = GetClipboardData(CF_UNICODETEXT)) {
            if (auto text = static_cast<const wchar_t*>(GlobalLock(memory))) { wide = text; GlobalUnlock(memory); }
        }
        ::CloseClipboard();
        if (wide.empty()) return 0;
        try {
            auto text = files::ToUtf8(wide);
            d.Send(SCI_BEGINUNDOACTION);
            if (d.Send(SCI_GETLENGTH)) {
                int eol = static_cast<int>(d.Send(SCI_GETEOLMODE));
                const char* separator = eol == SC_EOL_CRLF ? "\r\n" : eol == SC_EOL_CR ? "\r" : "\n";
                d.Send(SCI_APPENDTEXT, strlen(separator), reinterpret_cast<LPARAM>(separator));
            }
            d.Send(SCI_APPENDTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
            d.Send(SCI_ENDUNDOACTION); d.Send(SCI_GOTOPOS, d.Send(SCI_GETLENGTH)); Refresh();
        } catch (const std::exception& error) { Error(error); }
        break;
    }
    return 0;
}
LRESULT MainFrame::OnCopyData(UINT, WPARAM, LPARAM l, BOOL&) {
    auto* data = reinterpret_cast<COPYDATASTRUCT*>(l);
    if (!data || (data->dwData != 0x4D503434 && data->dwData != 0x4D503451 && data->dwData != 0x4D503452) || !data->lpData || data->cbData < sizeof(wchar_t)
        || data->cbData > 4 * 1024 * 1024 || data->cbData % sizeof(wchar_t)) return FALSE;
    auto* text = static_cast<const wchar_t*>(data->lpData);
    if (text[data->cbData / sizeof(wchar_t) - 1]) return FALSE;
    try {
        if (data->dwData == 0x4D503451) {
            for (int i = 0; i < tabs.GetPageCount(); ++i) {
                auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
                if (!d->path.empty() && _wcsicmp(d->path.c_str(), text) == 0) {
                    if (IsWindowEnabled()) { tabs.SetActivePage(i); ShowWindow(SW_RESTORE); SetForegroundWindow(m_hWnd); }
                    return TRUE;
                }
            }
            return FALSE;
        }
        if (runtime.operationDepth || runtime.inBackgroundWork || !IsWindowEnabled()) {
            runtime.pendingCommands.emplace_back(data->dwData, text); return TRUE;
        }
        FrameOperation operation(runtime, m_hWnd);
        std::wstring payload = text; auto delimiter = payload.find(L'\n');
        auto directory = delimiter == std::wstring::npos ? std::filesystem::current_path() : std::filesystem::path(payload.substr(0, delimiter));
        std::optional<std::filesystem::path> onlyFile;
        if (data->dwData == 0x4D503452) {
            auto next = payload.find(L'\n', delimiter + 1); if (next == std::wstring::npos) return FALSE;
            onlyFile = payload.substr(delimiter + 1, next - delimiter - 1); delimiter = next;
        }
        auto options = ParseCommandLine(delimiter == std::wstring::npos ? payload : payload.substr(delimiter + 1));
        if (onlyFile) options.files = {*onlyFile};
        for (auto& path : options.files) if (path.is_relative()) path = directory / path;
        ApplyCommandLine(*this, options);
        if (IsIconic() || !IsWindowVisible()) { SetTrayVisible(*this, false); ShowWindow(SW_RESTORE); }
        SetForegroundWindow(m_hWnd); return TRUE;
    } catch (const std::exception& error) { Error(error); return FALSE; }
}
LRESULT MainFrame::OnDeferredCommand(UINT, WPARAM, LPARAM, BOOL&) {
    if (runtime.operationDepth || runtime.inBackgroundWork || !IsWindowEnabled()) return 0;
    if (!runtime.pendingTabCloses.empty()) {
        FrameOperation operation(runtime, m_hWnd);
        const HWND child = runtime.pendingTabCloses.front();
        runtime.pendingTabCloses.erase(runtime.pendingTabCloses.begin());
        for (int i = 0; i < tabs.GetPageCount(); ++i)
            if (tabs.GetPageHWND(i) == child) { Close(i); break; }
        return 0;
    }
    if (runtime.pendingCommands.empty()) return 0;
    auto pending = std::move(runtime.pendingCommands.front()); runtime.pendingCommands.erase(runtime.pendingCommands.begin());
    COPYDATASTRUCT data{pending.first, static_cast<DWORD>((pending.second.size() + 1) * sizeof(wchar_t)), pending.second.data()};
    BOOL handled = TRUE; OnCopyData(WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), handled); return 0;
}
LRESULT MainFrame::OnContextMenu(UINT, WPARAM source, LPARAM coordinates, BOOL&) {
    HWND target = reinterpret_cast<HWND>(source);
    int popup = 1;
    for (int i = 0; i < tabs.GetPageCount(); ++i) {
        auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
        if (d && (target == d->hwnd || ::IsChild(d->hwnd, target))) { tabs.SetActivePage(i); popup = 0; break; }
    }
    HMENU root = LoadMenuW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDR_POPUPMENU));
    HMENU menu = GetSubMenu(root, popup); UpdateCommandUI();
    UpdateMenuCommands(*this, menu);
    POINT point{GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)};
    if (point.x == -1 && point.y == -1) {
        if (auto* d = Active()) {
            auto pos = d->Send(SCI_GETCURRENTPOS);
            point.x = static_cast<LONG>(d->Send(SCI_POINTXFROMPOSITION, 0, pos));
            point.y = static_cast<LONG>(d->Send(SCI_POINTYFROMPOSITION, 0, pos) + d->Send(SCI_TEXTHEIGHT, 0));
            ::ClientToScreen(d->hwnd, &point);
        } else GetCursorPos(&point);
    }
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, m_hWnd, nullptr);
    DestroyMenu(root); if (command) SendMessage(WM_COMMAND, command); return 0;
}
LRESULT MainFrame::OnMenuSelect(UINT, WPARAM w, LPARAM, BOOL&) {
    if (HIWORD(w) == 0xFFFF || (HIWORD(w) & MF_POPUP)) {
        ::SendMessageW(m_hWndStatusBar, SB_SIMPLE, FALSE, 0); return 0;
    }
    wchar_t help[512]{};
    if (LoadStringW(_Module.GetModuleInstance(), LOWORD(w), help, _countof(help))) {
        ::SendMessageW(m_hWndStatusBar, SB_SIMPLE, TRUE, 0);
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 255 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(help));
    }
    return 0;
}

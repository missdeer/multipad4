#pragma once
#include "../src/MainFrame.h"
#include "../plugins/PluginApi.h"
#include "QAArtifacts.h"
#include <source_location>
inline HWND OpenDocumentsTestWindow() {
    HWND result = nullptr;
    auto find = [](HWND window, LPARAM context) -> BOOL {
        wchar_t name[96]{}; GetClassNameW(window, name, _countof(name));
        if (wcscmp(name, L"MultiPad4.OpenDocumentsPane") == 0) *reinterpret_cast<HWND*>(context) = window;
        return TRUE;
    };
    std::pair<WNDENUMPROC, HWND*> state{find, &result};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND top, LPARAM context) -> BOOL {
        auto* state = reinterpret_cast<std::pair<WNDENUMPROC, HWND*>*>(context);
        state->first(top, reinterpret_cast<LPARAM>(state->second));
        EnumChildWindows(top, state->first, reinterpret_cast<LPARAM>(state->second));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
    return result;
}

inline void RunOpenDocumentsPaneTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("OpenDocuments plugin integration failed at line " + std::to_string(where.line())); };
    const PluginHost::Plugin* loaded = nullptr;
    for (const auto& plugin : frame.plugins.plugins) if (plugin.module == L"OpenDocuments.dll") loaded = &plugin;
    HWND pane = OpenDocumentsTestWindow();
    if (frame.plugins.onlyModule == L"__no_plugins__.dll") { require(!pane && !loaded); return; }
#ifdef MULTIPAD4_TEST_OPEN_DOCUMENTS
    require(loaded != nullptr);
#endif
    if (!loaded) { require(!pane); return; }
    require(pane == nullptr);
    require(frame.plugins.Execute(loaded->items[0]._cmdID));
    pane = OpenDocumentsTestWindow();
    require(pane != nullptr);
    require((GetWindowLongPtrW(pane, GWL_STYLE) & WS_VISIBLE) != 0);
    HWND list = GetDlgItem(pane, 100);
    auto verify = [&] {
        frame.Refresh();
        require(SendMessageW(list, LB_GETCOUNT, 0, 0) == frame.tabs.GetPageCount());
        require(SendMessageW(list, LB_GETCURSEL, 0, 0) == frame.tabs.GetActivePage());
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) {
            auto* doc = static_cast<EditorDocument*>(frame.tabs.GetPageData(i));
            auto length = SendMessageW(list, LB_GETTEXTLEN, i, 0);
            require(length >= 0);
            std::wstring label(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(label.data())); label.resize(length);
            require(label == doc->Title());
            require(SendMessageW(list, LB_GETITEMDATA, i, 0) == static_cast<LRESULT>(frame.plugins.Buffer(doc)));
        }
    };
    verify();
    auto* document = frame.NewDocument(); require(document != nullptr);
    document->untitled = L"插件测试 — untitled";
    document->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("modified")); verify();
    const int created = frame.tabs.GetActivePage();
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    SendMessageW(pane, WM_COMMAND, MAKEWPARAM(100, LBN_DBLCLK), reinterpret_cast<LPARAM>(list));
    require(frame.tabs.GetActivePage() == 0); verify();
    frame.tabs.SetActivePage(created); document->Send(SCI_SETSAVEPOINT);
    require(frame.Close(created)); verify();
    for (int command : {2, 3, 4, 1}) {
        require(frame.plugins.Execute(loaded->items[command]._cmdID));
        require((GetWindowLongPtrW(pane, GWL_STYLE) & WS_CHILD) != 0 ? command != 4 : command == 4);
        require(command == 4 ? GetWindow(pane, GW_OWNER) == frame.m_hWnd : frame.IsChild(pane));
        frame.UpdateLayout();
        CaptureQAWindow(pane, command == 4 ? L"OpenDocuments-floating" : L"OpenDocuments-docked");
    }
    require(frame.plugins.Execute(loaded->items[0]._cmdID)); require(!IsWindowVisible(pane));
    require(frame.plugins.Execute(loaded->items[0]._cmdID));
    // Metadata uses the same resource fields as the plugin manager.
    auto dll = frame.plugins.home / L"OpenDocuments" / L"OpenDocuments.dll";
    DWORD ignored = 0; DWORD bytes = GetFileVersionInfoSizeW(dll.c_str(), &ignored); require(bytes != 0);
    std::vector<BYTE> version(bytes); require(GetFileVersionInfoW(dll.c_str(), 0, bytes, version.data()) != FALSE);
    for (auto field : {L"PluginName", L"FileVersion", L"FileDescription", L"Comments", L"Author", L"Homepage", L"LegalCopyright", L"License"}) {
        std::wstring key = L"\\StringFileInfo\\040904B0\\" + std::wstring(field);
        void* value = nullptr; UINT size = 0;
        require(VerQueryValueW(version.data(), key.c_str(), &value, &size) && size > 1);
    }
}

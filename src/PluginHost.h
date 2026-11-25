#pragma once
#include "App.h"
#include "../3rdparty/notepad-plus-plus-plugin-api/PluginInterface.h"
#include "../3rdparty/notepad-plus-plus-plugin-api/Docking.h"
#include <filesystem>
#include <span>
class MainFrame;
struct EditorDocument;
class PluginPane : public PaneWindow<PluginPane> {
public:
    static constexpr auto WindowClass = L"MultiPad4.PluginPane";
    HWND client = nullptr, owner = nullptr;
    int dialogId = 0;
    std::wstring module;
    LRESULT Dispatch(UINT message, WPARAM w, LPARAM l, BOOL& handled) {
        switch (message) {
        case WM_SIZE: return OnSize(message, w, l, handled);
        case WM_CLOSE: return OnClose(message, w, l, handled);
        }
        handled = FALSE;
        return 0;
    }
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL& handled);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
};
class PluginHost {
public:
    struct Plugin {
        HMODULE library = nullptr;
        std::wstring name, module;
        FuncItem* items = nullptr;
        int count = 0;
        PBENOTIFIED notify = nullptr;
        PMESSAGEPROC message = nullptr;
        // Optional MultiPad4 extensions; absent exports preserve N++ behavior.
        BOOL (__cdecl* preTranslate)(MSG*) = nullptr;
        BOOL (__cdecl* commandEnabled)(UINT) = nullptr;
        void (__cdecl* populateEditorContextMenu)(HMENU) = nullptr;
        HMENU submenu = nullptr;
        bool supportsFastView = false;
    };
    MainFrame* frame = nullptr;
    HWND proxy = nullptr, secondProxy = nullptr;
    HMENU menu = nullptr;
    std::filesystem::path home, config;
    std::vector<Plugin> plugins;
    std::vector<std::wstring> errors;
    std::vector<HWND> modeless;
    std::vector<std::unique_ptr<PluginPane>> panes;
    HACCEL accelerators = nullptr;
    UINT nextCommand = 46000;
    int nextMarker = 20, nextIndicator = 20;
    UINT_PTR activeBuffer = 0;
    std::wstring onlyModule;
    std::span<const std::wstring_view> bundledModules;
    bool enabled = true, ready = false, stopped = false;
    void Initialize(MainFrame& owner);
    void Ready();
    void Shutdown();
    void Activated();
    void Notify(UINT code, UINT_PTR buffer = 0);
    void ScintillaNotification(const SCNotification& notification);
    void UpdateMenu(HMENU submenu);
    void PopulateEditorContextMenu(HMENU submenu);
    bool Execute(UINT id);
    bool PreTranslate(MSG* message);
    LRESULT Message(UINT message, WPARAM w, LPARAM l);
    LRESULT SciMessage(UINT message, WPARAM w, LPARAM l);
    EditorDocument* Document(UINT_PTR id);
    UINT_PTR Buffer(const EditorDocument* document) const;
    static LRESULT CALLBACK ProxyProc(HWND, UINT, WPARAM, LPARAM);
    static sptr_t Direct(sptr_t, unsigned int, uptr_t, sptr_t);
    static sptr_t DirectStatus(sptr_t, unsigned int, uptr_t, sptr_t, int*);
};

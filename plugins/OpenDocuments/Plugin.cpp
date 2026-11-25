#include "DocumentsPane.h"
#include "MenuStrings.h"
#include "../DockPaneState.h"
#include <memory>
namespace {
HINSTANCE instance = nullptr;
NppData host{};
std::unique_ptr<DockHost> pane;
bool initialized = false;
std::filesystem::path configPath;
void Start() {
    if (pane || !initialized) return;
    pane = std::make_unique<DockHost>();
    pane->Create(host._nppHandle, instance);
    if (!pane->pane.IsWindow()) pane.reset();
    if (pane) {
        if (!pluginLayout::Restore(configPath, L"OpenDocuments", pane->pane, host._nppHandle, pane->visible))
            pane->pane.Show();
        pane->Layout();
    }
}
void Stop() {
    if (pane) {
        if (pane->pane.IsWindow()) {
            pluginLayout::Save(configPath, L"OpenDocuments", pane->pane, pane->visible);
            pane->pane.DestroyWindow();
        }
        pane.reset();
    }
    if (initialized) {
        UnregisterClassW(L"MultiPad4.OpenDocumentsPane", instance);
        initialized = false;
    }
}
void Toggle() { if (pane) pane->Toggle(); else { Start(); if (pane && !pane->visible) pane->Toggle(); } }
void Left() { Start(); if (pane) pane->SetSide(DockSide::Left); }
void Right() { Start(); if (pane) pane->SetSide(DockSide::Right); }
void Bottom() { Start(); if (pane) pane->SetSide(DockSide::Bottom); }
void Float() { Start(); if (pane) pane->SetSide(DockSide::Floating); }
FuncItem commands[]{ {{}, Toggle}, {{}, Left}, {{}, Right}, {{}, Bottom}, {{}, Float} };
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { instance = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() { return L"OpenDocuments"; }
extern "C" __declspec(dllexport) void setInfo(NppData data) {
    host = data; initialized = instance != nullptr;
    configPath = pluginLayout::ConfigPath(host._nppHandle, L"OpenDocuments.ini");
}
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    constexpr UINT labels[]{IDS_OPEN_DOCUMENTS, IDS_DOCK_LEFT, IDS_DOCK_RIGHT, IDS_DOCK_BOTTOM, IDS_FLOAT};
    static_assert(_countof(labels) == _countof(commands));
    for (size_t index = 0; index < _countof(commands); ++index) {
        auto& label = commands[index]._itemName;
        LoadStringW(instance, labels[index], label, _countof(label));
    }
    *count = _countof(commands);
    return commands;
}
extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
    if (notification->nmhdr.code == NPPN_READY && !pluginLayout::Read(configPath, L"OpenDocuments").empty()) Start();
    else if (notification->nmhdr.code == NPPN_SHUTDOWN) Stop();
    else if (pane && notification->nmhdr.code == MPN_DOCUMENTSCHANGED) {
        if (notification->nmhdr.idFrom) pane->pane.UpdateSelection(); else pane->pane.Refresh();
    }
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL multipadSupportsFastView() { return TRUE; }
extern "C" __declspec(dllexport) BOOL multipadPreTranslateMessage(MSG* message) {
    if (pane && message->hwnd == pane->pane.list && message->message == WM_KEYDOWN && message->wParam == VK_RETURN) {
        pane->pane.ActivateSelection(); return TRUE;
    }
    return FALSE;
}

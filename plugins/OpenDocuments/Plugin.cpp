#include "DocumentsPane.h"
#include "../DockPaneState.h"
#include <memory>
#include <DockImpl.cpp>
WTL::CAppModule _Module;
namespace {
HINSTANCE instance = nullptr;
NppData host{};
std::unique_ptr<DockHost> pane;
bool initialized = false;
std::filesystem::path configPath;
void Start() {
    if (pane || !initialized) return;
    pane = std::make_unique<DockHost>();
    pane->Create(host._nppHandle);
    if (!pane->pane.IsWindow()) pane.reset();
    if (pane) {
        pluginLayout::Restore(configPath, L"OpenDocuments", pane->pane, host._nppHandle, pane->visible);
        pane->Layout();
    }
}
void Stop() {
    if (pane) {
        if (pane->pane.IsWindow()) {
            pluginLayout::Save(configPath, L"OpenDocuments", pane->pane, pane->visible);
            if (pane->pane.IsDocking()) pane->pane.Undock();
            pane->pane.DestroyWindow();
        }
        pane.reset();
    }
    if (initialized) {
        UnregisterClassW(L"MultiPad4.OpenDocumentsPane", instance);
        _Module.Term(); initialized = false;
    }
}
void Toggle() { if (pane) pane->Toggle(); else { Start(); if (pane && !pane->visible) pane->Toggle(); } }
void Left() { Start(); if (pane) pane->SetSide(DockSide::Left); }
void Right() { Start(); if (pane) pane->SetSide(DockSide::Right); }
void Bottom() { Start(); if (pane) pane->SetSide(DockSide::Bottom); }
void Float() { Start(); if (pane) pane->SetSide(DockSide::Floating); }
FuncItem commands[]{ {L"打开的文档", Toggle}, {L"停靠到左侧", Left},
    {L"停靠到右侧", Right}, {L"停靠到底部", Bottom}, {L"浮动", Float} };
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { instance = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() { return L"OpenDocuments"; }
extern "C" __declspec(dllexport) void setInfo(NppData data) {
    host = data; initialized = SUCCEEDED(_Module.Init(nullptr, instance));
    configPath = pluginLayout::ConfigPath(host._nppHandle, L"OpenDocuments.ini");
}
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) { *count = _countof(commands); return commands; }
extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
    if (notification->nmhdr.code == NPPN_READY && !pluginLayout::Read(configPath, L"OpenDocuments").empty()) Start();
    else if (notification->nmhdr.code == NPPN_SHUTDOWN) Stop();
    else if (pane && notification->nmhdr.code == MPN_DOCUMENTSCHANGED) pane->pane.Refresh();
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL multipadSupportsFastView() { return TRUE; }
extern "C" __declspec(dllexport) BOOL multipadPreTranslateMessage(MSG* message) {
    if (pane && message->hwnd == pane->pane.list && message->message == WM_KEYDOWN && message->wParam == VK_RETURN) {
        pane->pane.ActivateSelection(); return TRUE;
    }
    return FALSE;
}

#include "MatepathPane.h"
#include "../DockPaneState.h"
#include <memory>
#include <DockImpl.cpp>

WTL::CAppModule _Module;
namespace {
HINSTANCE instance = nullptr;
NppData host;
std::unique_ptr<MatepathPane> pane;
std::wstring configPath;
bool initialized = false;
bool testMode = false;
std::wstring Read(const wchar_t* key, const wchar_t* fallback = L"") {
    if (testMode) return fallback;
    wchar_t value[32768]{};
    GetPrivateProfileStringW(L"Matepath", key, fallback, value, _countof(value), configPath.c_str());
    return value;
}
void Write(const wchar_t* key, const std::wstring& value) {
    // Create Unicode INI files so Chinese paths and filters round-trip.
    HANDLE file = CreateFileW(configPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const WORD bom = 0xFEFF; DWORD written = 0;
        WriteFile(file, &bom, sizeof(bom), &written, nullptr);
        CloseHandle(file);
    }
    WritePrivateProfileStringW(L"Matepath", key, value.c_str(), configPath.c_str());
}
void Start() {
    if (pane || !initialized) return;
    configPath = pluginLayout::ConfigPath(host._nppHandle, L"Matepath.ini").wstring();
    testMode = configPath.empty();
    pane = std::make_unique<MatepathPane>();
    pane->CreatePane(host._nppHandle);
    if (!pane->IsWindow()) { pane.reset(); return; }
    pane->fileSpec = Read(L"Filter", L"*.*");
    if (pane->fileSpec.empty() || pane->fileSpec.size() > 126) pane->fileSpec = L"*.*";
    pane->pattern.SetWindowTextW(pane->fileSpec.c_str());
    pane->showHidden = Read(L"Hidden") == L"1";
    pane->reverse = Read(L"Reverse") == L"1";
    const auto sort = Read(L"Sort", L"0");
    if (sort.size() == 1 && sort[0] >= L'0' && sort[0] <= L'3') pane->sort = sort[0] - L'0';
    const auto folder = Read(L"Directory");
    if (folder.empty() || !pane->Navigate(folder)) pane->RefreshItems();
    if (!pluginLayout::Restore(configPath, L"Matepath", *pane, host._nppHandle, pane->visible)) {
        const auto side = Read(L"Side", L"1");
        if (side.size() == 1 && side[0] >= L'0' && side[0] <= L'3') pane->SetSide(static_cast<DockSide>(side[0] - L'0'));
        if (Read(L"Visible", L"1") == L"0") pane->Toggle();
    } else {
        // CurrentSide() uses the cached side while hidden.
        dockwins::DFDOCKPOSEX state{}; pane->GetDockingWindowPlacement(&state);
        pane->side = state.dockPos.hdr.hBar == HNONDOCKBAR ? DockSide::Floating
            : (state.dockPos.dwDockSide & 3) == dockwins::CDockingSide::sLeft ? DockSide::Left
            : (state.dockPos.dwDockSide & 3) == dockwins::CDockingSide::sRight ? DockSide::Right : DockSide::Bottom;
        pane->Layout();
    }
}
void Stop() {
    if (pane) {
        if (!testMode) {
            pluginLayout::Save(configPath, L"Matepath", *pane, pane->visible);
            Write(L"Directory", pane->directory.wstring());
            Write(L"Filter", pane->fileSpec);
            Write(L"Hidden", pane->showHidden ? L"1" : L"0");
            Write(L"Reverse", pane->reverse ? L"1" : L"0");
            Write(L"Sort", std::to_wstring(pane->sort));
            Write(L"Side", std::to_wstring(static_cast<int>(pane->CurrentSide())));
            Write(L"Visible", pane->visible ? L"1" : L"0");
        }
        pane->DestroyPane();
        pane.reset();
    }
    if (initialized) {
        UnregisterClassW(L"MultiPad4.MatepathPane", instance);
        _Module.Term(); initialized = false;
    }
}
void Toggle() { Start(); if (pane) pane->Toggle(); }
void Follow() { Start(); if (pane) pane->FollowActive(); }
FuncItem commands[]{ {L"文件浏览器", Toggle}, {L"定位活动文档", Follow} };
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { instance = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() { return L"Matepath"; }
extern "C" __declspec(dllexport) void setInfo(NppData data) {
    host = data;
    initialized = SUCCEEDED(_Module.Init(nullptr, instance));
}
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) { *count = _countof(commands); return commands; }
extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
    if (notification->nmhdr.code == NPPN_READY) Start();
    else if (notification->nmhdr.code == NPPN_SHUTDOWN) Stop();
}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL multipadPreTranslateMessage(MSG* message) {
    return pane && pane->IsWindow() ? pane->PreTranslateMessage(message) : FALSE;
}

extern "C" __declspec(dllexport) BOOL multipadSupportsFastView() { return TRUE; }

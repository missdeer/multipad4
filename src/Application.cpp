#include "Application.h"
#include "MainFrame.h"

#include <shellapi.h>
#include <fstream>
#include "StyleManager.h"
#include "Localization.h"
#include "CommandLine.h"
#include "Session.h"
#include "FrameCommands.h"
#include "EditAdvanced.h"

namespace {
// Serialize discovery through initial command processing so simultaneous launches
// cannot both decide that no main window exists yet.
class StartupLock {
    HANDLE mutex = nullptr;
    bool owned = false;
public:
    bool Acquire() {
        mutex = CreateMutexW(nullptr, FALSE, L"Local\\MultiPad4.Startup");
        if (!mutex) return false;
        DWORD wait = WaitForSingleObject(mutex, 15000);
        owned = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        return owned;
    }
    void Release() {
        if (owned) { ReleaseMutex(mutex); owned = false; }
    }
    ~StartupLock() { Release(); if (mutex) CloseHandle(mutex); }
};

HWND FindReusableWindow(const AppSettings& settings) {
    const auto portableDirectory = settings.portable ? settings.path.parent_path().parent_path() : std::filesystem::path{};
    HWND window = nullptr;
    while ((window = FindWindowExW(nullptr, window, L"MultiPad4.MainFrame", nullptr))) {
        const bool portable = GetPropW(window, MainFrame::PortableWindowProperty) != nullptr;
        if (portable != settings.portable) continue;
        if (!portable) return window;
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        const UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
        wchar_t executable[32768];
        DWORD length = _countof(executable);
        if (process && QueryFullProcessImageNameW(process.get(), 0, executable, &length)) {
            const auto directory = std::filesystem::path(std::wstring_view(executable, length)).parent_path();
            if (CompareStringOrdinal(directory.c_str(), -1, portableDirectory.c_str(), -1, TRUE) == CSTR_EQUAL)
                return window;
        }
    }
    return nullptr;
}

void FinishPluginMaintenance(const PluginMaintenance& task) {
    std::string failure;
    const auto log = task.directory / L"result.log";
    try {
        StartupLock lock;
        if (!lock.Acquire()) throw std::runtime_error("Could not synchronize plugin maintenance with MultiPad4 startup.");
        ExecutePluginMaintenance(task);
    } catch (const std::exception& error) {
        failure = error.what();
    }
    {
        std::ofstream output(log, std::ios::binary);
        output << (failure.empty() ? "Plugin operation completed." : failure) << '\n';
    }
    wchar_t title[256]{};
    LoadStringW(GetModuleHandleW(nullptr), IDS_PLUGINADM_TITLE, title, _countof(title));
    if (!failure.empty()) {
        auto message = std::wstring(L"Plugin operation failed.");
        try { message += L"\n\n" + files::FromUtf8(failure); } catch (const std::exception&) {}
        message += L"\n\nLog: " + log.wstring();
        MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
    }
    wchar_t exe[32768]{};
    const auto length = GetModuleFileNameW(nullptr, exe, _countof(exe));
    const auto session = task.directory / L"session.ini";
    const auto args = L"/n /session \"" + session.wstring() + L"\"";
    const auto hr = OleInitialize(nullptr);
    const bool started = length && length < _countof(exe) &&
        reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exe, args.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
    if (SUCCEEDED(hr)) OleUninitialize();
    if (!started) {
        const auto message = L"Could not restart MultiPad4.\nSession: " + session.wstring();
        MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
    }
}
}
int RunApplication(HINSTANCE instance,int show,const ApplicationStartup& startup) {
    int count = 0;
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> arguments(CommandLineToArgvW(GetCommandLineW(), &count), LocalFree);
    auto args = arguments.get();

    if (count == 4 && wcscmp(args[1], L"--apply-update") == 0) {
        StartupLock lock;
        return lock.Acquire() ? RunAppUpdateWorker(args[2], args[3]) : 1;
    }
    const auto updateRestart = TakeAppUpdateRestart();

    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr)) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    SetCurrentProcessExplicitAppUserModelID(L"MultiPad4.Editor");
    extern UINT g_uSystemDPI; g_uSystemDPI = GetDpiForSystem();
    if (!Scintilla_RegisterClasses(instance)) { OleUninitialize(); return 2; }

    int result = 0;
    std::optional<PluginMaintenance> pendingPluginMaintenance;
    std::unique_ptr<StagedAppUpdate> pendingUpdate;
    std::filesystem::path updateSettingsPath;
    {
        MainFrame frame;
        StartupLock startupLock;
        bool startupReady = startup.isolated || startupLock.Acquire();
        frame.plugins.enabled=startup.pluginsEnabled;
        frame.plugins.onlyModule=startup.onlyPlugin;
        frame.plugins.bundledModules=startup.bundledModules;
        frame.settings.Initialize(startup.isolated);
        SetThreadUILanguage(startup.isolated ? MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) : LanguageForCommand(frame.settings.Get(IDM_LANG_USER_DEFAULT, IDM_LANG_USER_DEFAULT)));
        SetConfigurationUserDirectory(frame.settings.path.parent_path());
        if (frame.settings.persistent) LoadStylePreferences(frame.settings.path.c_str());
        if (frame.settings.persistent) LoadEditPreferences(frame.settings.path);
        ParsedCommandLine options;
        bool reused = !startupReady;
        if (!startupReady) {
            MessageBoxW(nullptr, L"Cannot synchronize with another MultiPad4 startup. Please try again.", L"MultiPad4", MB_ICONERROR);
            result = 6;
        }
        if (startupReady && !startup.isolated) {
            try {
                options = ParseCommandLine(GetCommandLineW());
                if (!options.appId.empty()) SetCurrentProcessExplicitAppUserModelID(options.appId.c_str());
                if (RelaunchElevatedIfRequested(options)) reused = true;
                bool singleInstance = options.singleFileInstance || frame.settings.Get(IDM_VIEW_SINGLEFILEINSTANCE, 1) != 0;
                bool reuse = options.reuseWindow >= 0 ? options.reuseWindow != 0 : singleInstance || frame.settings.Get(IDM_VIEW_REUSEWINDOW) != 0;
                if (!reused && reuse && !updateRestart) {
                    if (HWND existing = FindReusableWindow(frame.settings)) {
                        auto text = std::filesystem::current_path().wstring() + L"\n" + GetCommandLineW();
                        COPYDATASTRUCT data{0x4D503434, static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)), text.data()};
                        DWORD_PTR accepted = 0; AllowSetForegroundWindow(ASFW_ANY);
                        reused = SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000, &accepted) && accepted;
                        if (!reused && singleInstance) {
                            MessageBoxW(nullptr, L"The existing MultiPad4 window could not accept the command. Please try again when it is available.", L"MultiPad4", MB_ICONERROR);
                            reused = true; result = 6;
                        }
                    }
                }
            } catch (const std::exception& error) {
                auto text = files::FromUtf8(error.what()); MessageBoxW(nullptr, text.c_str(), L"Command Line", MB_ICONERROR); reused = true; result = 5;
            }
        }
        frame.appUpdater.enabled = MULTIPAD4_UPDATE_ASSET[0] && frame.settings.persistent && !options.print && !options.help;
        if (updateRestart) frame.appUpdater.nextCheck = std::chrono::steady_clock::now() + std::chrono::hours(24);
        RECT rect{80, 60, 1280, 860};
        if (reused) {}
        else if (!frame.Create(rect, instance)) result = 3;
        else if(startup.run)result=startup.run(frame);
        else {
            frame.ShowWindow(frame.runtime.restoredShowCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : show); frame.UpdateWindow();
            frame.exitSessionEligible = !options.print && !options.help && options.sessionFile.empty() && options.reuseWindow != 0;
            bool restoredUpdate = false;
            try {
                if (updateRestart) {
                    const auto snapshot = UpdateSessionPath(frame, updateRestart->directory);
                    if (std::filesystem::exists(snapshot)) {
                        RestoreSessionSnapshot(frame, snapshot);
                        std::error_code ignored;
                        std::filesystem::remove(snapshot, ignored);
                    }
                    restoredUpdate = true;
                } else RestoreExitSession(frame);
            } catch (const std::exception& error) { frame.Error(error); }
            try { ApplyCommandLine(frame, options); } catch (const std::exception& error) { frame.Error(error); }
            if (frame.documents.empty()) frame.NewDocument();
            frame.plugins.Ready();
            startupLock.Release();
            if (restoredUpdate) CleanUpAppUpdate(*updateRestart);
            if (options.help) ExecuteFrameCommand(frame, IDM_CMDLINE_HELP);
            if (options.print) frame.PostMessage(WM_CLOSE);
            MSG message{};
            BOOL received;
            while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                if (frame.PreTranslateMessage(&message)) continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            result = received < 0 ? 7 : static_cast<int>(message.wParam);
            if (!frame.IsWindow()) {
                pendingPluginMaintenance = std::move(frame.pendingPluginMaintenance);
                pendingUpdate = std::move(frame.appUpdater.ready);
                if (pendingUpdate) updateSettingsPath = std::move(frame.settings.path);
            }
        }
    }
    Scintilla_ReleaseResources();
    if(startup.beforeOleShutdown)result=startup.beforeOleShutdown(result);
    OleUninitialize();
    if (pendingPluginMaintenance) FinishPluginMaintenance(*pendingPluginMaintenance);
    if (pendingUpdate) LaunchAppUpdateWorker(*pendingUpdate, updateSettingsPath);
    return result;
}

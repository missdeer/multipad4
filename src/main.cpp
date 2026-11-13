#include "MainFrame.h"
#include "SmokeTest.h"
#include "../tests/LargeFileTests.h"
#include "../tests/PluginFeatureTests.h"
#include <shellapi.h>
#include <cstdio>
#include <fstream>
#include "StyleManager.h"
#include "Localization.h"
#include "CommandLine.h"
#include "FrameCommands.h"
#include "EditAdvanced.h"
#include "../tests/QAArtifacts.h"
WTL::CAppModule _Module;
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr)) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    _Module.Init(nullptr, instance);
    SetCurrentProcessExplicitAppUserModelID(L"MultiPad4.Editor");
    extern UINT g_uSystemDPI; g_uSystemDPI = GetDpiForSystem();
    if (!Scintilla_RegisterClasses(instance)) { _Module.Term(); OleUninitialize(); return 2; }
    int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    bool pluginTest = count == 2 && wcscmp(args[1], L"--plugin-test") == 0;
    bool largeTest = count == 2 && wcscmp(args[1], L"--large-file-test") == 0;
    bool smoke = count == 2 && wcscmp(args[1], L"--smoke-test") == 0;
    bool report = count == 3 && wcscmp(args[1], L"--migration-report") == 0;
    bool render = count == 3 && wcscmp(args[1], L"--qa-render") == 0;
    if (render) qaArtifactDirectory = std::filesystem::absolute(args[2]);
    int result = 0;
    {
        CMessageLoop loop; _Module.AddMessageLoop(&loop);
        MainFrame frame;
        frame.plugins.enabled = !(largeTest || smoke || report || render);
        frame.settings.Initialize(largeTest || smoke || report || render || pluginTest);
        SetThreadUILanguage(largeTest || smoke || report || render || pluginTest ? MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) : LanguageForCommand(frame.settings.Get(IDM_LANG_USER_DEFAULT, IDM_LANG_USER_DEFAULT)));
        if (frame.settings.persistent) LoadStylePreferences(frame.settings.path.c_str());
        if (frame.settings.persistent) LoadEditPreferences(frame.settings.path);
        ParsedCommandLine options;
        bool reused = false;
        if (!largeTest && !smoke && !report && !render && !pluginTest) {
            try {
                options = ParseCommandLine(GetCommandLineW());
                if (!options.appId.empty()) SetCurrentProcessExplicitAppUserModelID(options.appId.c_str());
                if (RelaunchElevatedIfRequested(options)) reused = true;
                if (!reused && options.reuseWindow != 0 && !options.files.empty() && (options.singleFileInstance || frame.settings.Get(IDM_VIEW_SINGLEFILEINSTANCE, 1))) {
                    std::erase_if(options.files, [&](const auto& file) {
                        auto absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(file));
                        for (HWND candidate = FindWindowExW(nullptr, nullptr, L"MultiPad4.MainFrame", nullptr); candidate;
                            candidate = FindWindowExW(nullptr, candidate, L"MultiPad4.MainFrame", nullptr)) {
                            auto text = absolute.wstring(); COPYDATASTRUCT data{0x4D503451, static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)), text.data()};
                            DWORD_PTR accepted = 0;
                            if (SendMessageTimeoutW(candidate, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 2000, &accepted) && accepted) {
                                auto payload = std::filesystem::current_path().wstring() + L"\n" + absolute.wstring() + L"\n" + GetCommandLineW();
                                COPYDATASTRUCT command{0x4D503452, static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t)), payload.data()};
                                DWORD_PTR applied = 0;
                                return SendMessageTimeoutW(candidate, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&command), SMTO_ABORTIFHUNG, 5000, &applied) && applied;
                            }
                        }
                        return false;
                    });
                    if (options.files.empty()) reused = true;
                }
                bool reuse = options.reuseWindow >= 0 ? options.reuseWindow != 0 : frame.settings.Get(IDM_VIEW_REUSEWINDOW) != 0;
                if (!reused && reuse) {
                    HWND existing = FindWindowW(L"MultiPad4.MainFrame", nullptr);
                    if (existing) {
                        auto text = std::filesystem::current_path().wstring() + L"\n" + GetCommandLineW();
                        COPYDATASTRUCT data{0x4D503434, static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)), text.data()};
                        DWORD_PTR accepted = 0; AllowSetForegroundWindow(ASFW_ANY);
                        reused = SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000, &accepted) && accepted;
                    }
                }
            } catch (const std::exception& error) {
                auto text = files::FromUtf8(error.what()); MessageBoxW(nullptr, text.c_str(), L"Command Line", MB_ICONERROR); reused = true; result = 5;
            }
        }
        RECT rect{80, 60, 1280, 860};
        if (reused) {}
        else if (!frame.CreateEx(nullptr, rect)) result = 3;
        else if (largeTest || smoke || report || render || pluginTest) {
            if (pluginTest) { frame.ShowWindow(SW_SHOWNORMAL); frame.ShowWindow(SW_SHOWNORMAL); frame.NewDocument(); }
            frame.plugins.Ready();
            try {
                if (largeTest) { RunLargeFileTests(frame); }
                else if (pluginTest) {
                    RunPluginFeatureTests(frame);
                } else if (smoke || render) { if (render) frame.ShowWindow(SW_SHOWNORMAL); RunSmokeTest(frame); }
                else {
                    frame.NewDocument();
                    std::ofstream out{std::filesystem::path(args[2])};
                    out << "{\n";
                    for (UINT id = 40000; id < 40800; ++id) {
                        auto state = frame.State(id);
                        out << (id == 40000 ? "" : ",\n") << "\"" << id << "\": {\"supported\":" << (state.supported ? "true" : "false")
                            << ",\"enabled\":" << (state.enabled ? "true" : "false") << "}";
                    }
                    out << "\n}\n";
                }
            } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); result = 4; }
            frame.plugins.Shutdown();
            frame.matepath.DestroyPane();
            if (frame.dock.pane.IsWindow()) { if (frame.dock.pane.IsDocking()) frame.dock.pane.Undock(); frame.dock.pane.DestroyWindow(); }
            frame.DestroyWindow();
        } else {
            frame.ShowWindow(frame.runtime.restoredShowCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : show); frame.UpdateWindow();
            try { ApplyCommandLine(frame, options); } catch (const std::exception& error) { frame.Error(error); }
            if (frame.documents.empty()) frame.NewDocument();
            frame.plugins.Ready();
            if (options.help) ExecuteFrameCommand(frame, IDM_CMDLINE_HELP);
            if (options.print) frame.PostMessage(WM_CLOSE);
            result = loop.Run();
        }
        _Module.RemoveMessageLoop();
    }
    if (args) LocalFree(args);
    Scintilla_ReleaseResources(); _Module.Term(); OleUninitialize(); return result;
}

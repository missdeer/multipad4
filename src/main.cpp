#include "MainFrame.h"
#ifdef MULTIPAD4_TESTING
#include "SmokeTest.h"
#include "../tests/PluginIndexTests.h"
#include "../tests/SearchRetirementTests.h"
#include "../tests/TabStripTests.h"
#include "../tests/FrameRefreshTests.h"
#include "../tests/EditTransformTests.h"
#include "../tests/CommentTests.h"
#include "../tests/ModelinePreferenceTests.h"
#include "../tests/PrivateClipboard.h"
#include "../tests/RichTextTests.h"
#include "../tests/DpiFontTests.h"
#include "../tests/AutoSaveCaptureTests.h"
#include "../tests/SaveWhitespaceTests.h"
#include "../tests/StatusStatisticsTests.h"
#include "../tests/StatusPresentationTests.h"
#ifdef MULTIPAD4_INDENT_TEST
#include "../tests/IndentReadTests.h"
#endif
#include "../tests/OrdinarySearchTests.h"
#include "../tests/LargeFileTests.h"
#include "../tests/PluginFeatureTests.h"
#include "../tests/LexerBaselineTests.h"
#include "../tests/ConfigurationBenchmark.h"
#endif
#include <shellapi.h>
#include <cstdio>
#include <fstream>
#include "StyleManager.h"
#include "Localization.h"
#include "CommandLine.h"
#include "Session.h"
#include "FrameCommands.h"
#include "EditAdvanced.h"
#ifdef MULTIPAD4_TESTING
#include "../tests/QAArtifacts.h"
#endif
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
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int count = 0;
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> arguments(CommandLineToArgvW(GetCommandLineW(), &count), LocalFree);
    auto args = arguments.get();
#ifdef MULTIPAD4_TESTING
    std::unique_ptr<PrivateClipboard> privateClipboard;
    if (count == 2 && (wcscmp(args[1], L"--rich-text-test") == 0 || wcscmp(args[1], L"--rich-text-benchmark") == 0)) {
        try { privateClipboard = std::make_unique<PrivateClipboard>(); }
        catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
    }
#endif
    if (count == 4 && wcscmp(args[1], L"--apply-update") == 0) {
        StartupLock lock;
        return lock.Acquire() ? RunAppUpdateWorker(args[2], args[3]) : 1;
    }
    const auto updateRestart = TakeAppUpdateRestart();
#ifdef MULTIPAD4_TESTING
    if (count == 3 && wcscmp(args[1], L"--app-update-test") == 0) {
        try { TestAppUpdate(args[2]); }
        catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
        return 0;
    }
#endif
    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr)) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    SetCurrentProcessExplicitAppUserModelID(L"MultiPad4.Editor");
    extern UINT g_uSystemDPI; g_uSystemDPI = GetDpiForSystem();
    if (!Scintilla_RegisterClasses(instance)) { OleUninitialize(); return 2; }
#ifdef MULTIPAD4_TESTING
    bool pluginTest = count == 2 && wcscmp(args[1], L"--plugin-test") == 0;
    bool ordinaryTest = count == 2 && wcscmp(args[1], L"--ordinary-search-test") == 0;
    bool ordinaryBenchmark = count == 3 && wcscmp(args[1], L"--ordinary-search-benchmark") == 0;
    bool tabStripTest = (count == 3 && wcscmp(args[1], L"--tab-strip-test") == 0) || (count == 2 && wcscmp(args[1], L"--tab-strip-layout-test") == 0);
    bool searchRetirementTest = count == 2 && wcscmp(args[1], L"--search-retirement-test") == 0;
    bool statusTest = count == 2 && wcscmp(args[1], L"--status-statistics-test") == 0;
    bool statusBenchmark = count == 3 && wcscmp(args[1], L"--status-statistics-benchmark") == 0;
    bool commentTest = count == 2 && wcscmp(args[1], L"--comment-test") == 0;
    bool commentBenchmark = count == 2 && wcscmp(args[1], L"--comment-benchmark") == 0;
    bool statusPresentation = count == 2 && wcscmp(args[1], L"--status-presentation-test") == 0;
    bool statusPresentationBenchmark = count == 2 && wcscmp(args[1], L"--status-presentation-benchmark") == 0;
    bool indentTest = false, indentBenchmark = false;
#ifdef MULTIPAD4_INDENT_TEST
    indentTest = count == 2 && wcscmp(args[1], L"--indent-read-test") == 0;
    indentBenchmark = count == 2 && wcscmp(args[1], L"--indent-read-benchmark") == 0;
#endif
    bool largeTest = count == 2 && wcscmp(args[1], L"--large-file-test") == 0;
    bool noPluginsTest = count == 2 && wcscmp(args[1], L"--no-plugins-test") == 0;
    bool lexerBaseline = count == 3 && (wcscmp(args[1], L"--export-lexer-baseline") == 0 || wcscmp(args[1], L"--check-lexer-baseline") == 0);
    bool pluginIndexTest=count==3&&wcscmp(args[1],L"--plugin-index-test")==0;
    bool settingsProfileTest=count==3&&wcscmp(args[1],L"--settings-profile-test")==0;
    bool configurationTest=count==4&&wcscmp(args[1],L"--configuration-test")==0;
    bool themeReport=count==4&&wcscmp(args[1],L"--theme-report")==0;
    bool benchmark=count==3&&wcscmp(args[1],L"--configuration-benchmark")==0;
    bool configurationMode=configurationTest||themeReport||benchmark;
    bool refreshTest = count == 2 && wcscmp(args[1], L"--frame-refresh-test") == 0;
    bool refreshBenchmark = count == 3 && wcscmp(args[1], L"--frame-refresh-benchmark") == 0;
    bool modelineTest = count == 2 && wcscmp(args[1], L"--modeline-preference-test") == 0;
    bool modelineBenchmark = count == 2 && wcscmp(args[1], L"--modeline-preference-benchmark") == 0;
    bool transformTest = count == 2 && wcscmp(args[1], L"--edit-transform-test") == 0;
    bool transformBenchmark = count == 2 && wcscmp(args[1], L"--edit-transform-benchmark") == 0;
    bool saveWhitespaceTest = count == 2 && (wcscmp(args[1], L"--save-whitespace-test") == 0 || wcscmp(args[1], L"--save-whitespace-benchmark") == 0);
    bool dpiFontTest = count == 2 && wcscmp(args[1], L"--dpi-font-test") == 0;
    bool autosaveCaptureTest = count == 2 && (wcscmp(args[1], L"--autosave-capture-test") == 0 || wcscmp(args[1], L"--autosave-capture-benchmark") == 0);
    bool richTextTest = count == 2 && wcscmp(args[1], L"--rich-text-test") == 0;
    bool richTextBenchmark = count == 2 && wcscmp(args[1], L"--rich-text-benchmark") == 0;
    bool replacementTest = count == 2 && wcscmp(args[1], L"--file-replacement-test") == 0;
    bool smoke = pluginIndexTest ||
        settingsProfileTest ||
        dpiFontTest ||
        saveWhitespaceTest ||
        autosaveCaptureTest ||
        richTextTest ||
        richTextBenchmark ||
        modelineTest ||
        modelineBenchmark ||
        tabStripTest ||
        transformTest ||
        transformBenchmark ||
        searchRetirementTest ||
        refreshTest ||
        refreshBenchmark ||
        indentTest ||
        indentBenchmark ||
        replacementTest ||
        statusTest ||
        statusBenchmark ||
        ordinaryTest ||
        ordinaryBenchmark ||
        configurationMode ||
        lexerBaseline ||
        noPluginsTest ||
        (count == 2 && wcscmp(args[1], L"--smoke-test") == 0);
    bool report = count == 3 && wcscmp(args[1], L"--migration-report") == 0;
    if (statusPresentation || statusPresentationBenchmark) smoke = true;
    bool render = count == 3 && wcscmp(args[1], L"--qa-render") == 0;
    if (commentTest || commentBenchmark) smoke = true;
    if (render) qaArtifactDirectory = std::filesystem::absolute(args[2]);
#else
    constexpr bool autosaveCaptureTest=false, dpiFontTest=false, saveWhitespaceTest=false, richTextTest=false, richTextBenchmark=false, modelineTest=false, modelineBenchmark=false, tabStripTest=false, transformTest=false, transformBenchmark=false, searchRetirementTest=false, refreshTest=false, refreshBenchmark=false, indentTest=false, indentBenchmark=false, pluginTest=false, statusTest=false, statusBenchmark=false, ordinaryTest=false, ordinaryBenchmark=false;
    constexpr bool largeTest=false, lexerBaseline=false, configurationMode=false, smoke=false, report=false, render=false;
#endif
    int result = 0;
    std::optional<PluginMaintenance> pendingPluginMaintenance;
    std::unique_ptr<StagedAppUpdate> pendingUpdate;
    std::filesystem::path updateSettingsPath;
    {
        MainFrame frame;
        StartupLock startupLock;
        bool startupReady = largeTest || smoke || report || render || pluginTest || startupLock.Acquire();
        frame.plugins.enabled = !(saveWhitespaceTest || autosaveCaptureTest || richTextTest || richTextBenchmark || modelineTest || modelineBenchmark || tabStripTest || transformTest || transformBenchmark || searchRetirementTest || refreshTest || refreshBenchmark || indentTest || indentBenchmark || statusTest || statusBenchmark || ordinaryTest || ordinaryBenchmark || largeTest || report || lexerBaseline || configurationMode);
#ifdef MULTIPAD4_TESTING
        if (statusPresentation || statusPresentationBenchmark) frame.plugins.enabled = false;
        if (smoke || render) {
            frame.plugins.onlyModule = noPluginsTest ? L"__no_plugins__.dll" : L"__bundled__";
            frame.plugins.bundledModules = bundledTestModules;
        }
        if (commentTest || commentBenchmark) frame.plugins.enabled = false;
#endif
        frame.settings.Initialize(largeTest || smoke || report || render || pluginTest);
        SetThreadUILanguage(largeTest || smoke || report || render || pluginTest ? MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) : LanguageForCommand(frame.settings.Get(IDM_LANG_USER_DEFAULT, IDM_LANG_USER_DEFAULT)));
        SetConfigurationUserDirectory(frame.settings.path.parent_path());
        if (frame.settings.persistent) LoadStylePreferences(frame.settings.path.c_str());
        if (frame.settings.persistent) LoadEditPreferences(frame.settings.path);
        ParsedCommandLine options;
        bool reused = !startupReady;
        if (!startupReady) {
            MessageBoxW(nullptr, L"Cannot synchronize with another MultiPad4 startup. Please try again.", L"MultiPad4", MB_ICONERROR);
            result = 6;
        }
        if (startupReady && !largeTest && !smoke && !report && !render && !pluginTest) {
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
#ifdef MULTIPAD4_TESTING
        else if (largeTest || smoke || report || render || pluginTest) {
            if (pluginTest) { frame.ShowWindow(SW_SHOWNORMAL); frame.ShowWindow(SW_SHOWNORMAL); frame.NewDocument(); }
            frame.plugins.Ready();
            try {
#ifdef MULTIPAD4_INDENT_TEST
                if (indentTest || indentBenchmark) { RunIndentReadTests(frame, indentBenchmark); }
                else
#endif
                if (tabStripTest) { RunTabStripTests(frame, count == 3 ? args[2] : nullptr); }
                else if (searchRetirementTest) { RunSearchRetirementTests(frame); }
                else if (modelineTest || modelineBenchmark) { RunModelinePreferenceTests(frame, modelineBenchmark); }
                else if (transformTest || transformBenchmark) { RunEditTransformTests(frame, transformBenchmark); }
                else if (saveWhitespaceTest) { RunSaveWhitespaceTests(frame, wcscmp(args[1], L"--save-whitespace-benchmark") == 0); }
                else if (dpiFontTest) { RunDpiFontTests(frame); }
                else if (autosaveCaptureTest) { RunAutoSaveCaptureTests(frame, wcscmp(args[1], L"--autosave-capture-benchmark") == 0); }
                else if (richTextTest || richTextBenchmark) { RunRichTextTests(frame, richTextBenchmark); }
                else if (refreshTest) { RunFrameRefreshTests(frame); }
                else if (refreshBenchmark) { RunFrameRefreshBenchmark(frame, wcscmp(args[2], L"baseline") == 0); }
                else if (statusPresentation || statusPresentationBenchmark) { RunStatusPresentationTests(frame, statusPresentationBenchmark); }
                else if (statusTest || statusBenchmark) { RunStatusStatisticsTests(frame, statusBenchmark ? args[2] : nullptr); }
                else if (commentTest || commentBenchmark) { RunCommentTests(frame, commentBenchmark); }
                else if (ordinaryTest) { RunOrdinarySearchTests(frame); }
                else if (ordinaryBenchmark) { RunOrdinarySearchBenchmark(frame, args[2]); }
                else if(pluginIndexTest){RunPluginIndexTests(frame,args[2]);}
                else if(settingsProfileTest){RunSettingsProfileTests(args[2]);}
                else if(configurationTest){RunConfigurationFeatureTests(frame,args[2],args[3]);}
                else if(themeReport){RunThemeValidationReport(frame,args[2],args[3]);}
                else if (replacementTest) { RunSearchDialogFeatureTests(frame); }
                else if(benchmark){RunConfigurationBenchmark(frame,args[2]);}
                else if (lexerBaseline) { RunLexerBaseline(frame, args[2], wcscmp(args[1], L"--check-lexer-baseline") == 0); }
                else if (largeTest) { RunLargeFileTests(frame); }
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
            if ((smoke || render) && (MatepathTestWindow() || GetModuleHandleW(L"Matepath.dll"))) {
                fprintf(stderr, "Matepath plugin did not unload cleanly\n"); result = 4;
            }
            if ((smoke || render) && (OpenDocumentsTestWindow() || GetModuleHandleW(L"OpenDocuments.dll"))) {
                fprintf(stderr, "OpenDocuments plugin did not unload cleanly\n"); result = 4;
            }
            if (frame.IsWindow()) frame.DestroyWindow();
        }
#endif
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
#ifdef MULTIPAD4_TESTING
    if (privateClipboard && !privateClipboard->RestoreClipboard()) { fprintf(stderr, "Cannot restore private test clipboard.\n"); result = 4; }
#endif
    OleUninitialize();
    if (pendingPluginMaintenance) FinishPluginMaintenance(*pendingPluginMaintenance);
    if (pendingUpdate) LaunchAppUpdateWorker(*pendingUpdate, updateSettingsPath);
    return result;
}

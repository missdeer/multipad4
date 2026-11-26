#include "../src/Application.h"
#include "../src/MainFrame.h"
#include "SmokeTest.h"
#include "DpiFontTests.h"
#include "ThemeEnumerationTests.h"
#include "PrintingSeparatorTests.h"
#include "DeferredQueueTests.h"
#include "PluginIndexTests.h"
#include "SearchRetirementTests.h"
#include "TabStripTests.h"
#include "FrameRefreshTests.h"
#include "EditTransformTests.h"
#include "CommentTests.h"
#include "ModelinePreferenceTests.h"
#include "PrivateClipboard.h"
#include "RichTextTests.h"
#include "SessionCaptureTests.h"
#include "AutoSaveCaptureTests.h"
#include "SaveWhitespaceTests.h"
#include "StatusStatisticsTests.h"
#include "StatusPresentationTests.h"
#ifdef MULTIPAD4_INDENT_TEST
#include "IndentReadTests.h"
#endif
#include "OrdinarySearchTests.h"
#include "LargeFileTests.h"
#include "PluginFeatureTests.h"
#include "LexerBaselineTests.h"
#include "ConfigurationBenchmark.h"
#include "QAArtifacts.h"
#include <shellapi.h>
#include <cstdio>
#include <fstream>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    int count=0;
    std::unique_ptr<wchar_t*,decltype(&LocalFree)> arguments(CommandLineToArgvW(GetCommandLineW(),&count),LocalFree);
    auto args=arguments.get();
    std::unique_ptr<PrivateClipboard> privateClipboard;
    if (count == 2 && (wcscmp(args[1], L"--rich-text-test") == 0 || wcscmp(args[1], L"--rich-text-benchmark") == 0)) {
        try { privateClipboard = std::make_unique<PrivateClipboard>(); }
        catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
    }
    if(count==3 && wcscmp(args[1],L"--app-update-test")==0){
        const auto updateRestart=TakeAppUpdateRestart();
        try {TestAppUpdate(args[2]);}catch(const std::exception& error){fprintf(stderr,"%s\n",error.what());return 1;}
        return 0;
    }
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
    bool printingSeparatorTest=count==3&&wcscmp(args[1],L"--printing-separator-test")==0;
    bool deferredQueueTest=count==3&&wcscmp(args[1],L"--deferred-queue-test")==0;
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
    bool sessionCaptureTest = count == 2 && (wcscmp(args[1], L"--session-capture-test") == 0 || wcscmp(args[1], L"--session-capture-benchmark") == 0);
    bool dpiFontTest=count==2&&wcscmp(args[1],L"--dpi-font-test")==0;
    bool themeEnumerationTest=count==2&&(wcscmp(args[1],L"--theme-enumeration-test")==0||wcscmp(args[1],L"--theme-enumeration-benchmark")==0);
    bool autosaveCaptureTest = count == 2 && (wcscmp(args[1], L"--autosave-capture-test") == 0 || wcscmp(args[1], L"--autosave-capture-benchmark") == 0);
    bool richTextTest = count == 2 && wcscmp(args[1], L"--rich-text-test") == 0;
    bool richTextBenchmark = count == 2 && wcscmp(args[1], L"--rich-text-benchmark") == 0;
    bool replacementTest = count == 2 && wcscmp(args[1], L"--file-replacement-test") == 0;
    bool smoke =
        dpiFontTest ||
        pluginIndexTest ||
        settingsProfileTest ||
        sessionCaptureTest ||
        saveWhitespaceTest ||
        themeEnumerationTest ||
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
        (count == 2 && wcscmp(args[1], L"--smoke-test") == 0) ||
        printingSeparatorTest ||
        deferredQueueTest;
    bool report = count == 3 && wcscmp(args[1], L"--migration-report") == 0;
    if (statusPresentation || statusPresentationBenchmark) smoke = true;
    bool render = count == 3 && wcscmp(args[1], L"--qa-render") == 0;
    if (commentTest || commentBenchmark) smoke = true;
    if (render) qaArtifactDirectory = std::filesystem::absolute(args[2]);
    ApplicationStartup startup;
    startup.isolated=largeTest || smoke || report || render || pluginTest;
    startup.pluginsEnabled = !(themeEnumerationTest || autosaveCaptureTest || sessionCaptureTest || saveWhitespaceTest || richTextTest || richTextBenchmark || modelineTest || modelineBenchmark || tabStripTest || transformTest || transformBenchmark || searchRetirementTest || refreshTest || refreshBenchmark || indentTest || indentBenchmark || statusTest || statusBenchmark || ordinaryTest || ordinaryBenchmark || largeTest || report || lexerBaseline || configurationMode);
    if (statusPresentation || statusPresentationBenchmark) startup.pluginsEnabled = false;
    if (smoke || render) {
        startup.onlyPlugin = noPluginsTest ? L"__no_plugins__.dll" : L"__bundled__";
        startup.bundledModules = bundledTestModules;
    }
    if (commentTest || commentBenchmark) startup.pluginsEnabled = false;
    if(startup.isolated)startup.run=[&](MainFrame& frame){
        int result=0;

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
            else if(dpiFontTest){RunDpiFontTests(frame);}
            else if (sessionCaptureTest) { RunSessionCaptureTests(frame, wcscmp(args[1], L"--session-capture-benchmark") == 0); }
            else if(themeEnumerationTest){RunThemeEnumerationTests(frame,wcscmp(args[1],L"--theme-enumeration-benchmark")==0);}
            else if (autosaveCaptureTest) { RunAutoSaveCaptureTests(frame, wcscmp(args[1], L"--autosave-capture-benchmark") == 0); }
            else if (richTextTest || richTextBenchmark) { RunRichTextTests(frame, richTextBenchmark); }
            else if (refreshTest) { RunFrameRefreshTests(frame); }
            else if (refreshBenchmark) { RunFrameRefreshBenchmark(frame, wcscmp(args[2], L"baseline") == 0); }
            else if (statusPresentation || statusPresentationBenchmark) { RunStatusPresentationTests(frame, statusPresentationBenchmark); }
            else if (statusTest || statusBenchmark) { RunStatusStatisticsTests(frame, statusBenchmark ? args[2] : nullptr); }
            else if (commentTest || commentBenchmark) { RunCommentTests(frame, commentBenchmark); }
            else if (ordinaryTest) { RunOrdinarySearchTests(frame); }
            else if (ordinaryBenchmark) { RunOrdinarySearchBenchmark(frame, args[2]); }
            else if(printingSeparatorTest){RunPrintingSeparatorTests(frame,args[2]);}
            else if(deferredQueueTest){RunDeferredQueueTests(frame,args[2]);}
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
        return result;
    };
    if(privateClipboard)startup.beforeOleShutdown=[&](int result){
        if(!privateClipboard->RestoreClipboard()){fprintf(stderr,"Cannot restore private test clipboard.\n");return 4;}
        return result;
    };
    return RunApplication(instance,show,startup);
}

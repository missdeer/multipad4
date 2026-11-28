#pragma once
#include "MainFrame.h"
#include "ResultPageTests.h"
#include "CompletionBudgetTests.h"
#include "FileAssociations.h"
#include <stdexcept>
#include <source_location>
#include "MigrationSmokeTest.h"
#include "EditorFeatureTests.h"
#include "StyleFeatureTests.h"
#include "StyleDialogTests.h"
#include "FileCommandSmokeTest.h"
#include "EditDialogTests.h"
#include "RuntimeFeatureTests.h"
#include "FileDialogTests.h"
#include "RuntimeDialogTests.h"
#include "SearchDialogFeatureTests.h"
#include "SessionFeatureTests.h"
#include "AutoSaveFeatureTests.h"
#include "PerformanceFeatureTests.h"
#include "TabPreferenceTests.h"
#include "SettingsCancelTests.h"
#include "LongLinePreferenceTests.h"
#include "AutoSaveSettingsTests.h"
#include "SystemMRUPolicyTests.h"
#include "LanguagePreferenceTests.h"
#include "BundledPluginSmokeTests.h"
#include "SystemIntegrationRegistryTests.h"
#include "CloseReentrancyTests.h"
#include "ResizeGripTests.h"
#include "ToolbarOpenTests.h"
#include "EncodingTreePresentationTests.h"
#include "QAArtifacts.h"
#include "MDILayoutTests.h"
#include "UIConsistencyTests.h"
#include "FrameworkCompatibilityTests.h"
#include "MatepathPaneTests.h"
#include "OpenDocumentsPaneTests.h"
#include "PluginPaneStateTests.h"
#include "LinkFeatureTests.h"
// Runs against actual Win32 and Scintilla windows on the application's UI thread.
inline void RunSmokeTest(MainFrame& frame) {
    RunLinkFeatureTests(frame);
    RunWindowMenuTests(frame);
    RunDockLayoutTests(frame);
    auto require = [](bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("Editor integration assertion failed at line " + std::to_string(where.line())); };
    auto dir = std::filesystem::temp_directory_path() / (L"MultiPad4-smoke-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    try {
        RunPluginPaneStateTests(frame, dir);
        RunSearchAvailabilityTests(frame);
        wchar_t className[32]{}; GetClassNameW(frame.m_hWndMDIClient, className, 32);
        require(wcscmp(className, L"MDIClient") == 0);
        ::SetFocus(frame.m_hWndMDIClient);
        frame.NewDocument(); auto* first = frame.Active(); require(first != nullptr);
        require(::GetFocus() == first->hwnd);
        require((::GetWindowLongPtrW(frame.tabs.GetPageHWND(0), GWL_EXSTYLE) & WS_EX_MDICHILD) != 0);
        auto windowIcon = [&](int page) { return frame.tabs.Child(page)->GetIcon(FALSE); };
        const HICON untitledIcon = windowIcon(0);
        require(untitledIcon && DocumentIconResourceId({}) == IDI_DEFAULT_FILE);
        require(DocumentIconResourceId(L".babelrc") != IDI_DEFAULT_FILE);
        CaptureQAWindow(frame, L"MDI-icon-untitled");
        first->Send(SCI_ADDTEXT, 5, reinterpret_cast<LPARAM>("alpha"));
        require(first->Text() == "alpha" && first->Modified());
        first->Send(SCI_UNDO); require(first->Text().empty());
        first->Send(SCI_REDO); require(first->Text() == "alpha");
        first->path = dir / L"中文.cpp"; require(frame.Save(*first)); require(!first->Modified());
        require(first->language == L"C/C++");
        const HICON cppIcon = windowIcon(0);
        require(cppIcon && cppIcon != untitledIcon && DocumentIconResourceId(L"source.CPP") == DocumentIconResourceId(first->path) &&
            DocumentIconResourceId(first->path) != IDI_DEFAULT_FILE);
        CaptureQAWindow(frame, L"MDI-icon-cpp");
        first->Send(SCI_COLOURISE, 0, -1);
        frame.NewDocument(); auto* second = frame.Active(); require(second && second != first);
        second->Send(SCI_ADDTEXT, 4, reinterpret_cast<LPARAM>("beta"));
        require(first->Text() == "alpha" && second->Text() == "beta");
        second->path = dir / L"second.txt"; require(frame.Save(*second));
        const HICON textIcon = windowIcon(1);
        require(textIcon && textIcon != cppIcon && DocumentIconResourceId(second->path) != IDI_DEFAULT_FILE);
        CaptureQAWindow(frame, L"MDI-icon-txt");
        const auto unknownPath = dir / L"unknown.multipad4-test";
        require(frame.SaveTo(*second, unknownPath, false));
        require(windowIcon(1) && windowIcon(1) != textIcon && DocumentIconResourceId(unknownPath) == IDI_DEFAULT_FILE);
        CaptureQAWindow(frame, L"MDI-icon-unknown");
        require(frame.SaveTo(*second, dir / L"second.txt", false));
        RunCloseReentrancyTests(frame);
        RunMDILayoutTests(frame);
        frame.Refresh(); CaptureQAWindow(frame, L"MDI-initial");
        require(frame.GetMDITabCtrl().MoveItem(0, 1));
        require(frame.tabs.GetPageData(1) == first && frame.tabs.GetPageData(0) == second);
        require(frame.GetMDITabCtrl().MoveItem(1, 0));
        frame.tabs.SetActivePage(0); frame.Refresh(); require(frame.Active() == first);
        frame.SendMessage(WM_COMMAND, ID_WINDOW_CASCADE);
        require(frame.tabs.GetPageCount() == 2);
        frame.SendMessage(WM_COMMAND, ID_WINDOW_TILE_VERT);
        require(frame.tabs.GetPageCount() == 2);
        frame.MDIMaximize(frame.tabs.GetPageHWND(0));
        require(frame.Open(first->path) && frame.tabs.GetPageCount() == 2);
        // Compare lexer output from the actual fork, rather than just language labels.
        first->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("int value = 42; // comment\n"));
        first->ConfigureLexer(); require(first->Send(SCI_GETSTYLEINDEXAT, 0) == SCE_C_WORD2);
        first->Send(SCI_SETSAVEPOINT); require(frame.Save(*first));
        first->search.findText = L"value";
        first->Send(SCI_SETSEL, 0, 0); frame.FindNext();
        require(first->Send(SCI_GETSELECTIONSTART) == 4 && first->Send(SCI_GETSELECTIONEND) == 9);
        RunMigrationSmokeTest(frame);
        RunUIConsistencyTests(frame);
        RunUIRoutingTests(frame);
        RunMDIShortcutTests(frame);
        frame.NewDocument(); auto* featureDocument = frame.Active();
        RunBundledPluginSmokeTests(frame, *featureDocument, dir);
        RunEditorFeatureTests(*featureDocument);
        RunEditDialogTests(*featureDocument);
        RunStyleFeatureTests(*featureDocument);
        RunCompletionBudgetTests(frame);
        RunStyleDialogTests(frame, *featureDocument);
        require(frame.Close(frame.tabs.GetActivePage()));
        RunFileCommandSmokeTest(frame);
        RunFileDialogTests(frame);
        RunResizeGripTests(frame);
        RunEncodingTreePresentationTests(frame);
        RunRuntimeFeatureTests(frame);
        RunRuntimeDialogTests(frame);
        RunSearchDialogFeatureTests(frame);
        RunSearchResultTargetTests(frame);
        RunResultPageTests(frame);
        RunToolbarOpenTests(frame);
        RunSessionFeatureTests(frame);
        RunAutoSaveFeatureTests(frame);
        RunPerformanceFeatureTests(frame);
        RunTabPreferenceTests(frame);
        RunSettingsCancelTests(frame);
        RunLongLinePreferenceTests(frame);
        RunAutoSaveSettingsTests(frame);
        RunSystemMRUPolicyTests(frame);
        RunLanguagePreferenceTests(frame);
        RunSystemIntegrationRegistryTests();
        RunFileAssociationRegistryTests();
        frame.MDIMaximize(frame.tabs.GetPageHWND(frame.tabs.GetActivePage()));
        frame.UpdateLayout(); frame.Refresh(); CaptureQAWindow(frame, L"MDI-frame");
        RunOpenDocumentsPaneTests(frame);
        RunMatepathPaneTests(frame, dir);
        while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1));
        RunTabCloseTests(frame, dir);
        require(frame.documents.empty());
        RunExitSessionTests(frame, dir);
        auto text = files::Read(dir / L"中文.cpp");
        require(text.utf8 == "int value = 42; // comment\n");
        std::filesystem::remove_all(dir);
    } catch (...) { std::error_code ec; std::filesystem::remove_all(dir, ec); throw; }
}

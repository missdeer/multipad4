#pragma once
#include "MainFrame.h"
#include <stdexcept>
#include <source_location>
#include "MigrationSmokeTest.h"
#include "../tests/EditorFeatureTests.h"
#include "../tests/StyleFeatureTests.h"
#include "../tests/StyleDialogTests.h"
#include "FileCommandSmokeTest.h"
#include "../tests/EditDialogTests.h"
#include "../tests/RuntimeFeatureTests.h"
#include "../tests/FileDialogTests.h"
#include "../tests/RuntimeDialogTests.h"
#include "../tests/SearchDialogFeatureTests.h"
#include "../tests/SessionFeatureTests.h"
#include "../tests/AutoSaveFeatureTests.h"
#include "../tests/TabPreferenceTests.h"
#include "../tests/SettingsCancelTests.h"
#include "../tests/LongLinePreferenceTests.h"
#include "../tests/AutoSaveSettingsTests.h"
#include "../tests/SystemMRUPolicyTests.h"
#include "../tests/LanguagePreferenceTests.h"
#include "../tests/SystemIntegrationRegistryTests.h"
#include "../tests/CloseReentrancyTests.h"
#include "../tests/ResizeGripTests.h"
#include "../tests/SearchHelpTests.h"
#include "../tests/ToolbarOpenTests.h"
#include "../tests/EncodingTreePresentationTests.h"
#include "../tests/QAArtifacts.h"
#include "../tests/MDILayoutTests.h"
#include "../tests/UIConsistencyTests.h"
#include "../tests/FrameworkCompatibilityTests.h"
#include "../tests/MatepathPaneTests.h"
// Runs against actual WTL and Scintilla windows on the application's UI thread.
inline void RunSmokeTest(MainFrame& frame) {
    RunFrameworkCompatibilityTests();
    RunFloatingMoveTests(frame);
    RunDragDockTests(frame);
    auto require = [](bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("Editor integration assertion failed at line " + std::to_string(where.line())); };
    auto dir = std::filesystem::temp_directory_path() / (L"MultiPad4-smoke-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    try {
        wchar_t className[32]{}; GetClassNameW(frame.m_hWndMDIClient, className, 32);
        require(wcscmp(className, L"MDIClient") == 0);
        frame.NewDocument(); auto* first = frame.Active(); require(first != nullptr);
        require((::GetWindowLongPtrW(frame.tabs.GetPageHWND(0), GWL_EXSTYLE) & WS_EX_MDICHILD) != 0);
        first->Send(SCI_ADDTEXT, 5, reinterpret_cast<LPARAM>("alpha"));
        require(first->Text() == "alpha" && first->Modified());
        first->Send(SCI_UNDO); require(first->Text().empty());
        first->Send(SCI_REDO); require(first->Text() == "alpha");
        first->path = dir / L"中文.cpp"; require(frame.Save(*first)); require(!first->Modified());
        require(first->language == L"C/C++");
        first->Send(SCI_COLOURISE, 0, -1);
        frame.NewDocument(); auto* second = frame.Active(); require(second && second != first);
        second->Send(SCI_ADDTEXT, 4, reinterpret_cast<LPARAM>("beta"));
        require(first->Text() == "alpha" && second->Text() == "beta");
        second->path = dir / L"second.txt"; require(frame.Save(*second));
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
        RunEditorFeatureTests(*featureDocument);
        RunEditDialogTests(*featureDocument);
        RunStyleFeatureTests(*featureDocument);
        RunStyleDialogTests(frame, *featureDocument);
        require(frame.Close(frame.tabs.GetActivePage()));
        RunFileCommandSmokeTest(frame);
        RunFileDialogTests(frame);
        RunResizeGripTests(frame);
        RunEncodingTreePresentationTests(frame);
        RunRuntimeFeatureTests(frame);
        RunRuntimeDialogTests(frame);
        RunSearchDialogFeatureTests(frame);
        RunSearchHelpTests(frame);
        RunToolbarOpenTests(frame);
        RunSessionFeatureTests(frame);
        RunAutoSaveFeatureTests(frame);
        RunTabPreferenceTests(frame);
        RunSettingsCancelTests(frame);
        RunLongLinePreferenceTests(frame);
        RunAutoSaveSettingsTests(frame);
        RunSystemMRUPolicyTests(frame);
        RunLanguagePreferenceTests(frame);
        RunSystemIntegrationRegistryTests();
        frame.MDIMaximize(frame.tabs.GetPageHWND(frame.tabs.GetActivePage()));
        frame.UpdateLayout(); frame.Refresh(); CaptureQAWindow(frame, L"MDI-frame");
        for (auto side : {DockSide::Right, DockSide::Bottom, DockSide::Floating, DockSide::Left}) {
            frame.dock.SetSide(side); require(frame.dock.side == side);
            require((frame.dock.pane.GetStyle() & WS_CHILD) != 0 ? side != DockSide::Floating : side == DockSide::Floating);
            require(side == DockSide::Floating ? !frame.dock.pane.IsDocking() : frame.dock.pane.IsDocking());
            require(side == DockSide::Floating ? ::GetWindow(frame.dock.pane, GW_OWNER) == frame.m_hWnd : frame.IsChild(frame.dock.pane));
        }
        frame.dock.Toggle(); require(!frame.dock.visible); frame.dock.Toggle(); require(frame.dock.visible);
        RunMatepathPaneTests(frame, dir);
        while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1));
        require(frame.documents.empty());
        auto text = files::Read(dir / L"中文.cpp");
        require(text.utf8 == "int value = 42; // comment\n");
        std::filesystem::remove_all(dir);
    } catch (...) { std::error_code ec; std::filesystem::remove_all(dir, ec); throw; }
}


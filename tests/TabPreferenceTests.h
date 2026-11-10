#pragma once
#include "../src/MainFrame.h"
#include "../src/FrameCommands.h"
#include "../src/FileCommands.h"
#include "StyleDialogTests.h"
#include <source_location>

inline void RunTabPreferenceTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Tab preference assertion failed at line " + std::to_string(where.line()));
    };
    const auto savedDefaults = frame.defaultStyle;
    const auto savedSettings = frame.settings;
    const int initialCount = frame.tabs.GetPageCount();
    struct SavedDocument { EditorDocument* document; DocumentStyleState style; FileVariableState variables; };
    std::vector<SavedDocument> savedDocuments;
    for (auto& document : frame.documents) savedDocuments.push_back({document.get(), document->style, document->fileVariables});
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-tab-scope-" + std::to_wstring(GetCurrentProcessId()));
    auto profile = directory / L"saved.ini";
    SaveStylePreferences(profile);
    auto activate = [&](EditorDocument* document) {
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) {
            if (frame.tabs.GetPageData(i) == document) { frame.tabs.SetActivePage(i); frame.Refresh(); return; }
        }
        throw std::runtime_error("Tab test document disappeared");
    };
    auto settings = [&](EditorDocument* document, std::function<void(HWND)> callback) {
        activate(document);
        using namespace StyleDialogTest;
        owner = frame; fill = std::move(callback); activated = false; ticks = 0; callbackError = nullptr; finishCommand = IDOK;
        UINT_PTR timer = SetTimer(nullptr, 0, 10, Timer);
        if (!timer) throw std::runtime_error("Cannot create tab setting test timer");
        frame.SendMessage(WM_COMMAND, IDM_VIEW_TABSETTINGS);
        KillTimer(nullptr, timer); fill = {}; owner = nullptr;
        if (callbackError) std::rethrow_exception(callbackError);
        require(activated);
    };
    auto restore = [&] {
        while (frame.tabs.GetPageCount() > initialCount) {
            auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetPageCount() - 1));
            document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
            if (!frame.Close(frame.tabs.GetPageCount() - 1)) break;
        }
        LoadStylePreferences(profile);
        frame.defaultStyle = savedDefaults; frame.settings = savedSettings;
        for (auto& [document, style, variables] : savedDocuments) {
            document->style = style; document->fileVariables = variables;
            document->Send(SCI_SETTABINDENTS, style.tabIndents);
            document->Send(SCI_SETBACKSPACEUNINDENTS, style.backspaceMask);
        }
        std::error_code error; std::filesystem::remove_all(directory, error);
    };
    try {
        // Use the normal startup baseline regardless of earlier smoke dialogs.
        LoadStylePreferences(directory / L"baseline.ini");
        auto* first = frame.NewDocument(); require(first != nullptr);
        settings(first, [](HWND dialog) {
            SetDlgItemInt(dialog, IDC_GLOBAL_TAB_WIDTH, 4, FALSE); SetDlgItemInt(dialog, IDC_GLOBAL_INDENT_WIDTH, 2, FALSE);
            CheckDlgButton(dialog, IDC_GLOBAL_TAB_AS_SPACE, BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_SCHEME_USE_GLOBAL_TAB, BST_CHECKED);
            CheckDlgButton(dialog, IDC_FILE_USE_SCHEME_TAB, BST_CHECKED);
            CheckDlgButton(dialog, IDC_TAB_INDENT, BST_CHECKED);
            CheckDlgButton(dialog, IDC_BACKSPACE_UNINDENT, BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_BACKSPACE_SMARTDEL, BST_CHECKED);
            CheckDlgButton(dialog, IDC_DETECT_INDENTATION, BST_CHECKED);
        });
        auto* second = frame.NewDocument(); require(second && second->Send(SCI_GETUSETABS));
        activate(first); frame.SendMessage(WM_COMMAND, IDM_VIEW_TABSASSPACES);
        require(!first->Send(SCI_GETUSETABS) && first->style.tabsOverride);
        require(second->Send(SCI_GETUSETABS));
        require(!frame.defaultStyle.tabsOverride && frame.defaultStyle.useSchemeTabs);
        auto* third = frame.NewDocument(); require(third && third->Send(SCI_GETUSETABS));
        require(SelectDocumentScheme(*first, L"cpp"));
        require(!first->Send(SCI_GETUSETABS));
        require(SelectDocumentScheme(*first, L"txt"));
        require(!first->Send(SCI_GETUSETABS));
        auto checkProfile = profile; checkProfile += L".check.ini";
        SaveStylePreferences(checkProfile);
        require(GetPrivateProfileIntW(L"Tabs", L"Spaces", 1, checkProfile.c_str()) == 0);
        std::filesystem::remove(checkProfile);

        // A modeline changes this file's TabIndents, never the saved default.
        second->Load({"# tab-always-indent: nil; tab-width: 9; indent-tabs-mode: nil\n"});
        ApplyFileVariables(*second);
        activate(second); frame.SendMessage(WM_COMMAND, IDM_VIEW_SHOWWHITESPACE);
        require(frame.defaultStyle.tabIndents && frame.defaultStyle.useSchemeTabs);
        auto* fourth = frame.NewDocument();
        require(fourth && fourth->Send(SCI_GETTABWIDTH) == 4 && fourth->Send(SCI_GETINDENT) == 2);
        require(fourth->Send(SCI_GETUSETABS) && fourth->Send(SCI_GETTABINDENTS));

        settings(first, [](HWND dialog) {
            CheckDlgButton(dialog, IDC_FILE_USE_SCHEME_TAB, BST_UNCHECKED);
            SetDlgItemInt(dialog, IDC_FILE_TAB_WIDTH, 7, FALSE); SetDlgItemInt(dialog, IDC_FILE_INDENT_WIDTH, 3, FALSE);
            CheckDlgButton(dialog, IDC_FILE_TAB_AS_SPACE, BST_CHECKED);
            CheckDlgButton(dialog, IDC_TAB_INDENT, BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_BACKSPACE_UNINDENT, BST_CHECKED);
            CheckDlgButton(dialog, IDC_BACKSPACE_SMARTDEL, BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_DETECT_INDENTATION, BST_UNCHECKED);
        });
        require(first->Send(SCI_GETTABWIDTH) == 7 && !first->style.useSchemeTabs);
        require(!fourth->Send(SCI_GETTABINDENTS) && !frame.defaultStyle.tabIndents);
        settings(first, [](HWND dialog) { CheckDlgButton(dialog, IDC_TAB_INDENT, BST_CHECKED); });
        require(!second->Send(SCI_GETTABINDENTS) && fourth->Send(SCI_GETTABINDENTS));
        require(second->Send(SCI_GETTABWIDTH) == 9 && !second->Send(SCI_GETUSETABS));
        for (auto* document : {first, second, third, fourth}) {
            require(document->Send(SCI_GETBACKSPACEUNINDENTS) == 1 && !document->style.detectIndentation);
        }
        auto* fifth = frame.NewDocument();
        require(fifth && fifth->Send(SCI_GETTABWIDTH) == 4 && fifth->Send(SCI_GETINDENT) == 2 && fifth->Send(SCI_GETUSETABS));
        require(fifth->Send(SCI_GETBACKSPACEUNINDENTS) == 1 && !fifth->style.detectIndentation);
        require(frame.settings.ReadString(L"Editor Defaults", L"BackspaceMask") == L"1");
        require(frame.settings.ReadString(L"Editor Defaults", L"DetectIndentation") == L"0");

        // Scheme defaults also resolve afresh, independently of the File group.
        require(SelectDocumentScheme(*third, L"cpp"));
        settings(third, [](HWND dialog) {
            CheckDlgButton(dialog, IDC_SCHEME_USE_GLOBAL_TAB, BST_UNCHECKED);
            SetDlgItemInt(dialog, IDC_SCHEME_TAB_WIDTH, 6, FALSE); SetDlgItemInt(dialog, IDC_SCHEME_INDENT_WIDTH, 3, FALSE);
            CheckDlgButton(dialog, IDC_SCHEME_TAB_AS_SPACE, BST_CHECKED);
            CheckDlgButton(dialog, IDC_FILE_USE_SCHEME_TAB, BST_CHECKED);
        });
        auto* sixth = frame.NewDocument(); require(sixth != nullptr);
        require(SelectDocumentScheme(*sixth, L"cpp"));
        require(sixth->Send(SCI_GETTABWIDTH) == 6 && sixth->Send(SCI_GETINDENT) == 3 && !sixth->Send(SCI_GETUSETABS));
        require(!sixth->style.tabsOverride && sixth->style.useSchemeTabs);
        restore();
    } catch (...) { restore(); throw; }
}

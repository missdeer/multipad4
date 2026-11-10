#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>
inline void RunSearchDialogFeatureTests(MainFrame &frame) {
    auto require = [](bool condition, std::source_location source = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error("Search dialog assertion at line " + std::to_string(source.line()));
    };
    auto *document = frame.NewDocument();
    require(document != nullptr);
    document->Load({"alpha alpha"});
    document->search = {};
    auto readFind = [&] {
        HWND field = GetDlgItem(frame.searchDialog.hwnd, IDC_FINDTEXT);
        int length = GetWindowTextLengthW(field);
        std::wstring value(length + 1, L'\0');
        GetWindowTextW(field, value.data(), length + 1);
        value.resize(length);
        return value;
    };
    frame.settings.Set(IDM_SET_SELECTIONASFINDTEXT, 1);
    document->Load({"x"});
    document->Send(SCI_SELECTALL);
    frame.ShowSearch(false);
    require(readFind() == L"x");
    frame.searchDialog.Close();
    document->Load({"中文😀"});
    document->Send(SCI_SELECTALL);
    frame.ShowSearch(false);
    require(readFind() == L"中文😀");
    frame.searchDialog.Close();
    document->Load({"alpha alpha"});
    document->search = {};
    document->Send(SCI_GOTOPOS, 0);
    frame.ShowSearch(false);
    require(frame.searchDialog.hwnd != nullptr);
    require(CopyAcceleratorTableW(frame.searchDialog.accelerators, nullptr, 0) == 8);
    SetDlgItemTextW(frame.searchDialog.hwnd, IDC_FINDTEXT, L"alpha");
    CheckDlgButton(frame.searchDialog.hwnd, IDC_FINDCLOSE, BST_CHECKED);
    SendMessageW(frame.searchDialog.hwnd, WM_COMMAND, IDACC_REPLACE, 0);
    require(frame.searchDialog.replacing && document->search.findText == L"alpha");
    require(document->search.closeAfterFind && !document->search.closeAfterReplace);
    require(IsDlgButtonChecked(frame.searchDialog.hwnd, IDC_FINDCLOSE) == BST_UNCHECKED);
    SendMessageW(frame.searchDialog.hwnd, WM_COMMAND, IDACC_FINDNEXT, 0);
    require(document->Send(SCI_GETSELECTIONSTART) == 0 && document->Send(SCI_GETSELECTIONEND) == 5);
    require(frame.searchDialog.hwnd != nullptr);
    SendMessageW(frame.searchDialog.hwnd, WM_COMMAND, IDACC_FINDNEXT, 0);
    require(document->Send(SCI_GETSELECTIONSTART) == 6);
    CheckDlgButton(frame.searchDialog.hwnd, IDC_FINDCLOSE, BST_CHECKED);
    SendMessageW(frame.searchDialog.hwnd, WM_COMMAND, IDACC_FIND, 0);
    require(!frame.searchDialog.replacing && document->search.closeAfterReplace);
    require(IsDlgButtonChecked(frame.searchDialog.hwnd, IDC_FINDCLOSE) == BST_CHECKED);
    CheckDlgButton(frame.searchDialog.hwnd, IDC_FINDCLOSE, BST_UNCHECKED);
    SetDlgItemTextW(frame.searchDialog.hwnd, IDC_FINDTEXT, L"pending query");
    CaptureQAWindow(frame.searchDialog.hwnd, L"Search-dialog-final");
    frame.searchDialog.Close();
    require(document->search.findText == L"pending query");
    require(frame.Close(frame.tabs.GetActivePage()));
}

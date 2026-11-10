#pragma once
#include "../src/MainFrame.h"
#include <shlobj.h>
#include "Dlapi.h"
#include "QAArtifacts.h"

inline void RunMatepathPaneTests(MainFrame& frame, const std::filesystem::path& root) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Matepath regression at line " + std::to_string(where.line()));
    };
    auto& pane = frame.matepath;
    auto requireBackground = [&] {
        RECT client{}; pane.GetClientRect(&client);
        CClientDC windowDC(pane);
        CDC memory; require(memory.CreateCompatibleDC(windowDC) != nullptr);
        CBitmap bitmap; require(bitmap.CreateCompatibleBitmap(windowDC, client.right, client.bottom) != nullptr);
        const auto oldBitmap = memory.SelectBitmap(bitmap);
        // Seed uncovered space with a stale-content colour. The framework's
        // default erase handler reports success without changing these pixels.
        memory.FillSolidRect(&client, GetSysColor(COLOR_BTNFACE) ^ 0x00FFFFFF);
        pane.SendMessage(WM_ERASEBKGND, reinterpret_cast<WPARAM>(memory.m_hDC));
        const auto actual = memory.GetPixel(0, 0); // Margin outside all child controls.
        memory.SelectBitmap(oldBitmap);
        require(actual == GetSysColor(COLOR_BTNFACE));
    };
    const auto previous = pane.directory;
    const auto cwd = std::filesystem::current_path();
    const auto folder = root / L"浏览目录";
    std::filesystem::create_directories(folder / L"子目录");
    std::ofstream(folder / L"中文.txt") << "matepath text";
    std::ofstream(folder / L"other.cpp") << "int other;";
    std::ofstream(folder / L"hidden.txt") << "hidden";
    require(SetFileAttributesW((folder / L"hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE);
    pane.showHidden = false;
    require(pane.Navigate(folder));
    require(pane.list.GetItemCount() == 3);
    const auto historySize = pane.history.size();
    require(!pane.Navigate(folder / L"missing"));
    require(pane.directory == folder && pane.history.size() == historySize && pane.list.GetItemCount() == 3);
    require(!pane.Navigate(std::wstring(MAX_PATH + 5, L'x')));
    pane.pattern.SetWindowTextW(L"*.txt"); pane.SendMessage(WM_COMMAND, MatepathPane::Filter);
    require(pane.list.GetItemCount() == 2); // Folders remain navigable through a file filter.
    pane.SendMessage(WM_COMMAND, MatepathPane::Hidden); require(pane.list.GetItemCount() == 3);
    pane.SendMessage(WM_COMMAND, MatepathPane::Hidden);
    require(DirList_SelectItem(pane.list, nullptr, (folder / L"子目录").c_str()));
    pane.ActivateSelection(); require(pane.directory == folder / L"子目录");
    pane.SendMessage(WM_COMMAND, MatepathPane::Back); require(pane.directory == folder);
    pane.SendMessage(WM_COMMAND, MatepathPane::Forward); require(pane.directory == folder / L"子目录");
    pane.SendMessage(WM_COMMAND, MatepathPane::Up); require(pane.directory == folder);
    require(DirList_SelectItem(pane.list, nullptr, (folder / L"中文.txt").c_str()));
    const auto count = frame.documents.size();
    auto* original = frame.Active(); const auto originalText = original->Text();
    pane.ActivateSelection();
    require(frame.documents.size() == count + 1 && frame.Active()->Text() == "matepath text");
    require(original->Text() == originalText);
    pane.ActivateSelection(); require(frame.documents.size() == count + 1);
    frame.SendMessage(WM_COMMAND, ID_MATEPATH_FOLLOW);
    DirListItem selected{}; selected.mask = DLI_FILENAME;
    require(DirList_GetItem(pane.list, -1, &selected) >= 0);
    require(std::filesystem::path(selected.szFileName) == folder / L"中文.txt");
    for (auto side : {DockSide::Left, DockSide::Bottom, DockSide::Floating, DockSide::Right}) {
        pane.SetSide(side);
        require(side == DockSide::Floating ? !pane.IsDocking() : pane.IsDocking());
        require(pane.CurrentSide() == side);
        requireBackground();
        pane.Toggle(); require(!pane.visible && pane.CurrentSide() == side);
        pane.Toggle(); require(pane.visible && pane.CurrentSide() == side);
        requireBackground();
    }
    pane.Toggle(); require(!pane.visible); pane.Toggle(); require(pane.visible);
    frame.UpdateLayout(); CaptureQAWindow(frame, L"Matepath-docked");
    pane.SetSide(DockSide::Floating); CaptureQAWindow(pane, L"Matepath-floating");
    pane.SetSide(DockSide::Right);
    require(frame.Close(frame.tabs.GetActivePage()));
    pane.fileSpec = L"*.*"; pane.pattern.SetWindowTextW(L"*.*");
    require(pane.Navigate(previous));
    require(std::filesystem::current_path() == cwd);
}

#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>
inline void WaitForOrdinarySearch(SearchDialog& dialog) {
    const auto deadline = GetTickCount64() + 30000;
    while (dialog.SearchBusy()) {
        MSG message{};
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        else MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        if (GetTickCount64() >= deadline) throw std::runtime_error("Ordinary search did not complete.");
    }
}
inline void WaitForFileSearch(SearchDialog& dialog) {
    const auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline) {
        SendMessageW(dialog.hwnd, WM_TIMER, 1, 0);
        wchar_t status[512]{};
        GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, status, 512);
        if (std::wstring_view(status).starts_with(L"Completed.")) return;
        Sleep(1);
    }
    throw std::runtime_error("File search did not complete.");
}
inline void StartFileReplacement(SearchDialog& dialog) {
    const auto timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR, DWORD) {
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
            wchar_t title[64]{}; GetWindowTextW(window, title, 64);
            if (std::wstring(title) == L"Replace in Files" && GetDlgItem(window, IDOK)) PostMessageW(window, WM_COMMAND, IDOK, 0);
            return TRUE;
        }, 0);
    });
    if (!timer) throw std::runtime_error("Cannot confirm replacement test.");
    dialog.Execute(IDC_SEARCH_REPLACE_FILES); KillTimer(nullptr, timer);
}
inline void RunSearchAvailabilityTests(MainFrame& frame) {
    auto require = [](bool condition) {
        if (!condition) throw std::runtime_error("Search must remain available without document text.");
    };
    require(frame.Active() == nullptr);
    for (UINT command : {static_cast<UINT>(ID_FIND), static_cast<UINT>(IDM_EDIT_REPLACE)}) {
        require(frame.State(command).enabled);
        frame.SendMessage(WM_COMMAND, command);
        auto* document = frame.Active();
        require(document && document->Text().empty());
        require(frame.searchDialog.hwnd && frame.searchDialog.document == document);
        require(frame.searchDialog.replacing == (command == IDM_EDIT_REPLACE));
        for (bool readOnly : {false, true}) {
            document->Send(SCI_SETREADONLY, readOnly);
            frame.Refresh();
            for (UINT searchCommand : {static_cast<UINT>(ID_FIND), static_cast<UINT>(IDM_EDIT_REPLACE)}) {
                require(frame.State(searchCommand).enabled);
                require(!(GetMenuState(frame.GetMenu(), searchCommand, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED)));
                frame.SendMessage(WM_COMMAND, searchCommand);
                require(frame.searchDialog.replacing == (searchCommand == IDM_EDIT_REPLACE));
            }
        }
        require(frame.Close(frame.tabs.GetActivePage()));
        require(frame.Active() == nullptr && !frame.searchDialog.hwnd);
    }
}
inline void RunSearchResultTargetTests(MainFrame& frame) {
    auto require = [](bool condition, std::source_location where = std::source_location::current()) {
        if (!condition) throw std::runtime_error("Search result target regression at line " + std::to_string(where.line()));
    };
    auto* source = frame.NewDocument(); require(source != nullptr);
    source->Load({"No matching content in the source document."}); source->search = {};
    frame.ShowSearch(false);
    auto& dialog = frame.searchDialog;
    const HWND window = dialog.hwnd;
    SetDlgItemTextW(window, IDC_FINDTEXT, L"needle");
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-search-target-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    files::WriteAtomic(directory / L"result.txt", {"needle / needle\n"});
    dialog.SetPage(2);
    SetDlgItemTextW(window, IDC_SEARCH_DIRECTORY, directory.c_str());
    SetDlgItemTextW(window, IDC_SEARCH_FILTER, L"*.txt");
    auto& results = frame.searchResults; results.Clear();
    SendMessageW(GetDlgItem(window, IDC_SEARCH_FILES), BM_CLICK, 0, 0);
    WaitForFileSearch(dialog);
    require(results.lines.size() == 3 && results.lines[2].segments.size() == 2);
    SCNotification click{};
    click.nmhdr.hwndFrom = results.editor; click.nmhdr.code = SCN_DOUBLECLICK;
    click.position = SendMessageW(results.editor, SCI_POSITIONFROMLINE, 2, 0) + results.lines[2].segments[0].first;
    SendMessageW(results.m_hWnd, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&click));
    auto* opened = frame.Active(); require(opened && opened != source && opened->path.filename() == L"result.txt");
    require(dialog.hwnd == window && dialog.document == opened && dialog.page == 2);
    dialog.SetPage(0);
    SendMessageW(GetDlgItem(window, IDOK), BM_CLICK, 0, 0);
    require(opened->Send(SCI_GETSELECTIONSTART) == 9 && opened->Send(SCI_GETSELECTIONEND) == 15);
    SendMessageW(GetDlgItem(window, IDC_FINDPREV), BM_CLICK, 0, 0);
    require(opened->Send(SCI_GETSELECTIONSTART) == 0 && opened->Send(SCI_GETSELECTIONEND) == 6);
    results.Clear();
    SendMessageW(GetDlgItem(window, IDC_FINDALL), BM_CLICK, 0, 0); WaitForOrdinarySearch(dialog);
    require(results.lines.size() == 3 && results.lines[2].segments.size() == 2);
    require(results.targets[2].identity == opened->searchIdentity);
    require(source->Text() == "No matching content in the source document.");
    // F3 from the editor must read text currently typed in the open dialog.
    SetDlgItemTextW(window, IDC_FINDTEXT, L" / ");
    frame.SendMessage(WM_COMMAND, ID_FINDNEXT);
    require(opened->Send(SCI_GETSELECTIONSTART) == 6 && opened->Send(SCI_GETSELECTIONEND) == 9);
    // A tab switch without pumping posted activation messages is also safe.
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i)
        if (frame.tabs.GetPageData(i) == source) frame.tabs.SetActivePage(i);
    SetDlgItemTextW(window, IDC_FINDTEXT, L"source");
    SendMessageW(GetDlgItem(window, IDOK), BM_CLICK, 0, 0);
    require(dialog.document == source && source->Send(SCI_GETSELECTIONEND) - source->Send(SCI_GETSELECTIONSTART) == 6);
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i)
        if (frame.tabs.GetPageData(i) == opened) frame.tabs.SetActivePage(i);
    dialog.SetPage(1);
    SetDlgItemTextW(window, IDC_FINDTEXT, L"needle"); SetDlgItemTextW(window, IDC_REPLACETEXT, L"fixed");
    opened->Send(SCI_SETSEL, 0, 6);
    SendMessageW(GetDlgItem(window, IDC_REPLACE), BM_CLICK, 0, 0);
    require(opened->Text() == "fixed / needle\n");
    require(source->Text() == "No matching content in the source document.");
    opened->Send(SCI_UNDO); require(opened->Text() == "needle / needle\n");
    // Disk has two matches; the unsaved editor buffer is authoritative.
    opened->Load({"needle needle needle"});
    dialog.SetPage(2); results.Clear();
    dialog.Execute(IDC_SEARCH_FILES); WaitForFileSearch(dialog);
    require(results.lines.size() == 3 && results.lines[2].segments.size() == 3);
    require(opened->Text() == "needle needle needle");
    require(files::Read(directory / L"result.txt").utf8 == "needle / needle\n");
    opened->Send(SCI_SETSAVEPOINT);
    dialog.Close(); require(frame.Close(frame.tabs.GetActivePage()));
    require(frame.Active() == source); require(frame.Close(frame.tabs.GetActivePage()));
    std::filesystem::remove_all(directory);
    results.Clear(); results.Hide(); results.visible = false;
}
inline void RunSearchDialogFeatureTests(MainFrame& frame) {
    auto require = [](bool condition, std::source_location where = std::source_location::current()) {
        if (!condition) throw std::runtime_error("Search regression at line " + std::to_string(where.line()));
    };
    auto* d = frame.NewDocument(); require(d != nullptr);
    d->Load({"中文😀 alpha alpha\nsecond alpha\n"}); d->search = {};
    d->Send(SCI_SETSEL, 0, 10); frame.SendMessage(WM_COMMAND, ID_FIND);
    auto& dialog = frame.searchDialog; require(dialog.hwnd && dialog.document == d);
    COMBOBOXINFO findCombo{}; findCombo.cbSize = sizeof(findCombo);
    require(GetComboBoxInfo(GetDlgItem(dialog.hwnd, IDC_FINDTEXT), &findCombo));
    require(GetFocus() == findCombo.hwndItem);
    SetFocus(GetDlgItem(dialog.hwnd, IDC_SEARCH_TABS));
    SendMessageW(dialog.hwnd, WM_COMMAND, MAKEWPARAM(IDACC_FIND, 1), 0);
    require(GetFocus() == findCombo.hwndItem);
    require(TabCtrl_GetItemCount(GetDlgItem(dialog.hwnd, IDC_SEARCH_TABS)) == 4);
    wchar_t selection[32]{}; GetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, selection, 32); require(std::wstring(selection) == L"中文😀");
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"alpha");
    frame.searchResults.Clear(); dialog.Execute(IDC_FINDALL); WaitForOrdinarySearch(dialog);
    auto& results = frame.searchResults;
    require(results.visible && results.lines.size() == 4);
    require(results.lines[2].segments.size() == 2);
    auto send = [&](UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(results.editor, m, w, l); };
    require(send(SCI_GETREADONLY) != 0);
    require(send(SCI_GETFOLDLEVEL, 0) & SC_FOLDLEVELHEADERFLAG);
    require((send(SCI_GETFOLDLEVEL, 1) & SC_FOLDLEVELNUMBERMASK) == SC_FOLDLEVELBASE + 1);
    auto hitStart = send(SCI_POSITIONFROMLINE, 2) + results.lines[2].segments[0].first;
    require(send(SCI_GETSTYLEINDEXAT, hitStart) == SearchResultLexer::Match);
    require(send(SCI_GETSTYLEINDEXAT, send(SCI_POSITIONFROMLINE, 2) + 6) == SearchResultLexer::LineNumber);
    require(results.Navigate(2, 1) && d->Send(SCI_GETSELECTIONSTART) == 17);
    require(results.Navigate(3) && d->Send(SCI_GETSELECTIONSTART) == 30);
    CaptureQAWindow(dialog.hwnd, L"Search-Find"); CaptureQAWindow(frame, L"Search-results");
    dialog.SetPage(1); SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"beta");
    dialog.Execute(IDC_REPLACEALL); require(d->Text() == "中文😀 beta beta\nsecond beta\n");
    d->Send(SCI_UNDO); require(d->Text() == "中文😀 alpha alpha\nsecond alpha\n");
    CheckRadioButton(dialog.hwnd, IDC_SEARCH_NORMAL, IDC_SEARCH_REGEX, IDC_SEARCH_REGEX);
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"(alpha)"); SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"<$1>");
    dialog.Execute(IDC_REPLACEALL); require(d->Text() == "中文😀 <alpha> <alpha>\nsecond <alpha>\n");
    CaptureQAWindow(dialog.hwnd, L"Search-Replace");
    d->Load({"one\ntwo\n"}); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"(?=.)"); dialog.ReadFields();
    require(d->CollectMatches().size() == 6);
    SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"X");
    d->Send(SCI_SETSEL, 0, 0); dialog.Execute(IDOK); dialog.Execute(IDC_REPLACE);
    require(d->Text() == "Xone\ntwo\n" && d->Send(SCI_GETSELECTIONSTART) == 2);
    d->Load({"one\ntwo\n"});
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"["); SendMessageW(dialog.hwnd, WM_COMMAND, IDC_FINDALL, 0);
    wchar_t status[256]{}; GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, status, 256); require(std::wstring(status).find(L"Invalid") != std::wstring::npos);
    CheckRadioButton(dialog.hwnd, IDC_SEARCH_NORMAL, IDC_SEARCH_REGEX, IDC_SEARCH_EXTENDED);
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"\\n"); dialog.ReadFields(); require(d->CollectMatches().size() == 2);
    dialog.SetPage(3); dialog.Execute(IDC_SEARCH_MARK); WaitForOrdinarySearch(dialog); require(d->Send(SCI_INDICATORALLONFOR, 3) & (1 << 12));
    dialog.Execute(IDC_SEARCH_CLEAR_MARK); require(!(d->Send(SCI_INDICATORALLONFOR, 3) & (1 << 12)));
    CaptureQAWindow(dialog.hwnd, L"Search-Mark");
    CheckRadioButton(dialog.hwnd, IDC_SEARCH_NORMAL, IDC_SEARCH_REGEX, IDC_SEARCH_NORMAL);
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"one");
    auto* other = frame.NewDocument(); other->Load({"one one"});
    require(dialog.document == other); dialog.Execute(IDC_SEARCH_ALL_OPEN); WaitForOrdinarySearch(dialog);
    require(other->search.findText == L"one");
    dialog.SetPage(2);
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-search-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory / L"sub");
    files::WriteAtomic(directory / L"first.txt", {"one one", files::Encoding::Utf16LE});
    files::WriteAtomic(directory / L"sub" / L"second.txt", {"one", files::Encoding::Utf8Bom});
    files::WriteAtomic(directory / L"ignored.bin", {"one"});
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_DIRECTORY, directory.c_str()); SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"*.txt");
    results.Clear(); dialog.Execute(IDC_SEARCH_FILES);
    WaitForFileSearch(dialog);
    require(results.lines.size() == 5);
    require(files::Read(directory / L"first.txt").encoding == files::Encoding::Utf16LE);
    CaptureQAWindow(dialog.hwnd, L"Search-Files");
    auto rectangle = [](HWND window) { RECT r{}; GetWindowRect(window, &r); return r; };
    const auto clientSize = rectangle(dialog.hwnd);
    const auto fieldSize = rectangle(GetDlgItem(dialog.hwnd, IDC_FINDTEXT));
    const auto buttonSize = rectangle(GetDlgItem(dialog.hwnd, IDC_SEARCH_FILES));
    const auto closeSize = rectangle(GetDlgItem(dialog.hwnd, IDCANCEL));
    require(closeSize.top < rectangle(GetDlgItem(dialog.hwnd, IDC_SEARCH_DIRECTORY)).top);
    require(closeSize.left == buttonSize.left);
    require((clientSize.right - clientSize.left) < MulDiv(700, GetDpiForWindow(dialog.hwnd), 96));
    require((clientSize.bottom - clientSize.top) < MulDiv(400, GetDpiForWindow(dialog.hwnd), 96));
    require(LOWORD(SendMessageW(dialog.hwnd, DM_GETDEFID, 0, 0)) == IDC_SEARCH_FILES);
    SetWindowPos(dialog.hwnd, nullptr, 0, 0, clientSize.right - clientSize.left + 120,
        clientSize.bottom - clientSize.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const auto resizedField = rectangle(GetDlgItem(dialog.hwnd, IDC_FINDTEXT));
    require(resizedField.right - fieldSize.right == 120);
    require(rectangle(GetDlgItem(dialog.hwnd, IDC_SEARCH_FILES)).left - buttonSize.left == 120);
    require(resizedField.right < rectangle(GetDlgItem(dialog.hwnd, IDC_SEARCH_SWAP)).left);
    CaptureQAWindow(dialog.hwnd, L"Search-Files-wide");
    SetWindowPos(dialog.hwnd, nullptr, 0, 0, clientSize.right - clientSize.left,
        clientSize.bottom - clientSize.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CheckDlgButton(dialog.hwnd, IDC_SEARCH_TRANSPARENT, BST_CHECKED);
    CheckRadioButton(dialog.hwnd, IDC_SEARCH_TRANSPARENT_LOST, IDC_SEARCH_TRANSPARENT_ALWAYS, IDC_SEARCH_TRANSPARENT_ALWAYS);
    SendMessageW(dialog.hwnd, WM_COMMAND, IDC_SEARCH_TRANSPARENT_ALWAYS, 0);
    BYTE alpha = 0; DWORD flags = 0;
    require(GetLayeredWindowAttributes(dialog.hwnd, nullptr, &alpha, &flags) && alpha == 180);
    CheckRadioButton(dialog.hwnd, IDC_SEARCH_TRANSPARENT_LOST, IDC_SEARCH_TRANSPARENT_ALWAYS, IDC_SEARCH_TRANSPARENT_LOST);
    SendMessageW(dialog.hwnd, WM_COMMAND, IDC_SEARCH_TRANSPARENT_LOST, 0);
    SendMessageW(dialog.hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    require(GetLayeredWindowAttributes(dialog.hwnd, nullptr, &alpha, &flags) && alpha == 255);
    SendMessageW(dialog.hwnd, WM_ACTIVATE, WA_INACTIVE, 0);
    require(GetLayeredWindowAttributes(dialog.hwnd, nullptr, &alpha, &flags) && alpha == 180);
    CheckDlgButton(dialog.hwnd, IDC_SEARCH_TRANSPARENT, BST_UNCHECKED);
    SendMessageW(dialog.hwnd, WM_COMMAND, IDC_SEARCH_TRANSPARENT, 0);
    require(!(GetWindowLongPtrW(dialog.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED));
    SendMessageW(dialog.hwnd, WM_COMMAND, IDC_SEARCH_EXPAND, 0);
    require(!IsWindowVisible(GetDlgItem(dialog.hwnd, IDC_SEARCH_MODE_GROUP)));
    require(rectangle(dialog.hwnd).bottom - rectangle(dialog.hwnd).top < clientSize.bottom - clientSize.top);
    SendMessageW(dialog.hwnd, WM_COMMAND, IDC_SEARCH_EXPAND, 0);
    require(IsWindowVisible(GetDlgItem(dialog.hwnd, IDC_SEARCH_MODE_GROUP)));
    require(results.Navigate(2));
    require(frame.Active()->path.extension() == L".txt");
    require(dialog.document == frame.Active());
    // Closing a result document closes its bound modeless dialog. Return to
    // the source tab before closing the result to continue file-search checks.
    const int resultIndex = frame.tabs.GetActivePage();
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i)
        if (frame.tabs.GetPageData(i) == other) frame.tabs.SetActivePage(i);
    frame.Refresh();
    require(frame.Close(resultIndex));
    SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"changed");
    StartFileReplacement(dialog);
    WaitForFileSearch(dialog);
    auto replaced = files::Read(directory / L"first.txt");
    require(replaced.utf8 == "changed changed" && replaced.encoding == files::Encoding::Utf16LE);
    require(files::Read(directory / L"sub" / L"second.txt").encoding == files::Encoding::Utf8Bom);
    require(files::Read(directory / L"ignored.bin").utf8 == "one");
    auto replacementRow = [&](const std::filesystem::path& path) {
        for (size_t i = results.targets.size(); i-- > 0;) if (results.targets[i].path == path && results.targets[i].match.replacementTarget) return i;
        throw std::runtime_error("Missing replacement navigation target.");
    };
    // Closed UTF-16 results navigate using committed metadata and replacement spans.
    require(results.Navigate(replacementRow(directory / L"first.txt"), 1));
    require(frame.Active()->Range(frame.Active()->Send(SCI_GETSELECTIONSTART), frame.Active()->Send(SCI_GETSELECTIONEND) - frame.Active()->Send(SCI_GETSELECTIONSTART)) == "changed");
    const int replacedIndex = frame.tabs.GetActivePage();
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == other) frame.tabs.SetActivePage(i);
    frame.Refresh(); require(frame.Close(replacedIndex));
    const auto growingPath = directory / L"nav-growing.txt";
    files::WriteAtomic(growingPath, {"one one", files::Encoding::Utf16LE});
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"nav-growing.txt");
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"one"); SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"growing\nreplacement");
    StartFileReplacement(dialog); WaitForFileSearch(dialog);
    require(results.Navigate(replacementRow(growingPath), 1));
    auto* growingDocument = frame.Active();
    require(growingDocument->Range(growingDocument->Send(SCI_GETSELECTIONSTART), growingDocument->Send(SCI_GETSELECTIONEND) - growingDocument->Send(SCI_GETSELECTIONSTART)) == "growing\nreplacement");
    const int growingIndex = frame.tabs.GetActivePage();
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == other) frame.tabs.SetActivePage(i);
    frame.Refresh(); require(frame.Close(growingIndex));
    // Hard-link candidates resolve to the unsaved live buffer for both read and replacement.
    const auto alias = directory / L"first.alias";
    require(CreateHardLinkW(alias.c_str(), (directory / L"first.txt").c_str(), nullptr));
    require(frame.Open(directory / L"first.txt")); auto* aliasDocument = frame.Active(); aliasDocument->Load({"needle in live buffer"});
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"*.alias"); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"needle");
    results.Clear(); dialog.Execute(IDC_SEARCH_FILES); WaitForFileSearch(dialog);
    require(std::any_of(results.targets.begin(), results.targets.end(), [&](const auto& target) { return target.identity == aliasDocument->searchIdentity; }));
    aliasDocument->runtime.monitorMode = 0; SnapshotDocument(*aliasDocument);
    const auto external = directory / L"external.new"; files::WriteAtomic(external, {"changed changed"});
    require(MoveFileExW(external.c_str(), aliasDocument->path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"first.txt");
    results.Clear(); dialog.Execute(IDC_SEARCH_FILES); WaitForFileSearch(dialog);
    require(std::any_of(results.targets.begin(), results.targets.end(), [&](const auto& target) { return target.identity == aliasDocument->searchIdentity; }));
    SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"updated");
    auto replaceAlias = [&] { StartFileReplacement(dialog); WaitForFileSearch(dialog); };
    // A newly created alias misses the old ID cache; the final live check must publish no hits.
    const auto currentAlias = directory / L"current.alias";
    require(CreateHardLinkW(currentAlias.c_str(), aliasDocument->path.c_str(), nullptr));
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"current.alias"); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"changed");
    results.Clear(); replaceAlias();
    wchar_t conflict[512]{}; GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, conflict, 512);
    require(std::wstring_view(conflict).find(L"0 matches") != std::wstring_view::npos && std::wstring_view(conflict).find(L"1 files/folders skipped") != std::wstring_view::npos);
    require(aliasDocument->Text() == "needle in live buffer" && files::Read(currentAlias).utf8 == "changed changed");
    require(std::none_of(results.lines.begin(), results.lines.end(), [](const auto& line) { return line.style == SearchResultLexer::Default; }));
    // The worker's fresh identity index must recover a stale dialog miss for a new hard link.
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"needle");
    results.Clear(); dialog.Execute(IDC_SEARCH_FILES); WaitForFileSearch(dialog);
    require(std::any_of(results.targets.begin(), results.targets.end(), [&](const auto& target) { return target.identity == aliasDocument->searchIdentity; }));
    require(aliasDocument->Text() == "needle in live buffer" && files::Read(currentAlias).utf8 == "changed changed");
    require(DeleteFileW(currentAlias.c_str()));
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"first.txt"); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"needle");
    replaceAlias();
    require(aliasDocument->Text() == "updated in live buffer" && files::Read(alias).utf8 == "changed changed");
    aliasDocument->Send(SCI_UNDO); require(aliasDocument->Text() == "needle in live buffer"); aliasDocument->Send(SCI_SETSAVEPOINT);
    const int aliasIndex = frame.tabs.GetActivePage();
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == other) frame.tabs.SetActivePage(i);
    frame.Refresh(); require(frame.Close(aliasIndex));
    // Open buffers use the searched revision and retain a single undo action.
    require(frame.Open(directory / L"first.txt"));
    auto* opened = frame.Active(); opened->Load({"changed unsaved changed", files::Encoding::Utf16LE});
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"changed");
    SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"buffer");
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"first.txt");
    StartFileReplacement(dialog);
    WaitForFileSearch(dialog);
    require(opened->Text() == "buffer unsaved buffer");
    require(files::Read(directory / L"first.txt").utf8 == "changed changed");
    require(results.Navigate(replacementRow(opened->path), 1));
    require(opened->Range(opened->Send(SCI_GETSELECTIONSTART), opened->Send(SCI_GETSELECTIONEND) - opened->Send(SCI_GETSELECTIONSTART)) == "buffer");
    opened->Send(SCI_UNDO); require(opened->Text() == "changed unsaved changed");
    // Prepared edits preserve markers, folds and selection like the sequential editor.
    {
        auto* oracle = frame.NewDocument(); require(oracle);
        const auto markerScheme = opened->style.scheme;
        const std::string marked = "void f() {\nchanged line;\nuntouched two;\nchanged tail;\n}\n";
        auto prepare = [&](EditorDocument& target) {
            require(SelectDocumentScheme(target, L"C/C++")); target.Load({marked});
            target.Send(SCI_SETPROPERTY, reinterpret_cast<WPARAM>("fold"), reinterpret_cast<LPARAM>("1"));
            target.Send(SCI_COLOURISE, 0, -1);
            target.Send(SCI_MARKERADD, 0, 0); target.Send(SCI_MARKERADD, 1, 0);
            target.Send(SCI_MARKERADD, 2, 3); target.Send(SCI_MARKERADD, 3, 4);
            target.Send(SCI_SETFOLDEXPANDED, 0, FALSE); target.Send(SCI_HIDELINES, 1, 1);
            target.Send(SCI_SETSEL, 28, 31);
        };
        prepare(*opened); prepare(*oracle);
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == opened) frame.tabs.SetActivePage(i);
        frame.Refresh();
        SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"grown\nnext"); dialog.ReadFields(); oracle->search = opened->search;
        require(oracle->ReplaceAll() == 2);
        StartFileReplacement(dialog); WaitForFileSearch(dialog);
        auto compare = [&] {
            opened->Send(SCI_COLOURISE, 0, -1); oracle->Send(SCI_COLOURISE, 0, -1);
            require(opened->Text() == oracle->Text());
            require(opened->Send(SCI_GETSELECTIONSTART) == oracle->Send(SCI_GETSELECTIONSTART) && opened->Send(SCI_GETSELECTIONEND) == oracle->Send(SCI_GETSELECTIONEND));
            for (LRESULT line = 0; line < oracle->Send(SCI_GETLINECOUNT); ++line) {
                require(opened->Send(SCI_MARKERGET, line) == oracle->Send(SCI_MARKERGET, line));
                require(opened->Send(SCI_GETFOLDLEVEL, line) == oracle->Send(SCI_GETFOLDLEVEL, line));
                require(opened->Send(SCI_GETFOLDEXPANDED, line) == oracle->Send(SCI_GETFOLDEXPANDED, line));
                require(opened->Send(SCI_GETLINEVISIBLE, line) == oracle->Send(SCI_GETLINEVISIBLE, line));
            }
        };
        compare(); opened->Send(SCI_UNDO); oracle->Send(SCI_UNDO); compare(); require(opened->Text() == marked);
        opened->style.scheme = markerScheme; opened->ConfigureLexer();
        oracle->Send(SCI_SETSAVEPOINT);
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == oracle) { require(frame.Close(i)); break; }
        SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"buffer");
    }
    // NUL is valid editor content. Growing multiline replacements retain post-edit targets.
    const std::string nulInput("changed\0changed", 15);
    opened->Send(SCI_SETTARGETRANGE, 0, opened->Send(SCI_GETLENGTH));
    opened->Send(SCI_REPLACETARGET, nulInput.size(), reinterpret_cast<LPARAM>(nulInput.data()));
    SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"growing\nreplacement");
    StartFileReplacement(dialog); WaitForFileSearch(dialog);
    require(opened->Text() == std::string("growing\nreplacement\0growing\nreplacement", 39));
    require(results.Navigate(replacementRow(opened->path), 1));
    require(opened->Range(opened->Send(SCI_GETSELECTIONSTART), opened->Send(SCI_GETSELECTIONEND) - opened->Send(SCI_GETSELECTIONSTART)) == "growing\nreplacement");
    opened->Send(SCI_UNDO); require(opened->Text() == nulInput);
    SetDlgItemTextW(dialog.hwnd, IDC_REPLACETEXT, L"buffer");

    // Edit after the first snapshot slice: the batch must report a conflict and continue.
    opened->Load({std::string(1024 * 1024, 'x') + "changed"});
    StartFileReplacement(dialog);
    const auto conflictDeadline = GetTickCount64() + 10000;
    bool copying = false;
    while (GetTickCount64() < conflictDeadline) {
        SendMessageW(dialog.hwnd, WM_TIMER, 1, 0);
        wchar_t progress[512]{}; GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, progress, 512);
        if (std::wstring_view(progress).starts_with(L"Replacing:")) { copying = true; break; }
        Sleep(1);
    }
    require(copying); opened->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("edited during snapshot"));
    WaitForFileSearch(dialog);
    require(opened->Text() == "edited during snapshot" && files::Read(directory / L"first.txt").utf8 == "changed changed");
    wchar_t conflictStatus[512]{}; GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, conflictStatus, 512);
    require(std::wstring_view(conflictStatus).find(L"1 files/folders skipped") != std::wstring_view::npos);
    // A failed commit must immediately replay opens reserved during that commit.
    const auto failurePath = directory / L"failure.txt", queuedPath = directory / L"queued.txt";
    files::WriteAtomic(failurePath, {"changed"}); files::WriteAtomic(queuedPath, {"queued"});
    require(SetFileAttributesW(failurePath.c_str(), FILE_ATTRIBUTE_READONLY));
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"failure.txt");
    StartFileReplacement(dialog);
    const auto commitDeadline = GetTickCount64() + 10000;
    while (!dialog.CommittingReplacement() && GetTickCount64() < commitDeadline) { SendMessageW(dialog.hwnd, WM_TIMER, 1, 0); Sleep(1); }
    require(dialog.CommittingReplacement());
    const auto pageCount = frame.tabs.GetPageCount();
    require(!frame.Open(queuedPath) && frame.tabs.GetPageCount() == pageCount);
    SendMessageW(dialog.hwnd, WM_TIMER, 1, 0);
    const auto replayDeadline = GetTickCount64() + 10000;
    while (frame.tabs.GetPageCount() == pageCount && GetTickCount64() < replayDeadline) { SendMessageW(dialog.hwnd, WM_TIMER, 1, 0); Sleep(1); }
    require(frame.tabs.GetPageCount() == pageCount + 1 && frame.Active()->path == queuedPath);
    WaitForFileSearch(dialog);
    require(files::Read(failurePath).utf8 == "changed"); require(SetFileAttributesW(failurePath.c_str(), FILE_ATTRIBUTE_NORMAL));
    const int queuedIndex = frame.tabs.GetActivePage();
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == opened) frame.tabs.SetActivePage(i);
    frame.Refresh(); require(frame.Close(queuedIndex));

    // Retiring cancellation must not replace the status of a different search page.
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"first.txt");
    opened->Load({std::string(1024 * 1024, 'x') + "changed"}); StartFileReplacement(dialog);
    copying = false;
    const auto cancelDeadline = GetTickCount64() + 10000;
    while (GetTickCount64() < cancelDeadline) {
        SendMessageW(dialog.hwnd, WM_TIMER, 1, 0); wchar_t progress[512]{};
        GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, progress, 512);
        if (std::wstring_view(progress).starts_with(L"Replacing:")) { copying = true; break; } Sleep(1);
    }
    require(copying); dialog.SetPage(0); dialog.Status(L"New action."); dialog.ServiceReplacement();
    wchar_t retainedStatus[512]{}; GetDlgItemTextW(dialog.hwnd, IDC_SEARCH_STATUS, retainedStatus, 512);
    require(std::wstring_view(retainedStatus) == L"New action.");
    opened->Send(SCI_SETSAVEPOINT);
    dialog.Close(); require(!dialog.hwnd && !dialog.document);
    require(frame.Close(frame.tabs.GetActivePage()));
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == other) frame.tabs.SetActivePage(i);

    const auto originalLanguage = GetThreadUILanguage();
    struct RestoreSearchLanguage { LANGID language; ~RestoreSearchLanguage() { SetThreadUILanguage(language); } } languageRestore{originalLanguage};
    SetThreadUILanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
    SearchDialog localized;
    localized.Open(frame, *d, false);
    localized.SetPage(2);
    wchar_t title[128]{}; GetWindowTextW(localized.hwnd, title, 128);
    require(std::wstring(title) == L"文件中查找");
    SetDlgItemTextW(localized.hwnd, IDC_SEARCH_FILTER, L"*.csv");
    SetDlgItemTextW(localized.hwnd, IDC_FINDTEXT, L"test");
    CaptureQAWindow(localized.hwnd, L"Search-Files-Chinese");
    localized.Close();
    SetThreadUILanguage(originalLanguage);
    std::filesystem::remove_all(directory);
    other->Send(SCI_SETSAVEPOINT); require(frame.Close(frame.tabs.GetActivePage()));
    d->Send(SCI_SETSAVEPOINT); require(frame.Close(frame.tabs.GetActivePage()));
    results.Clear(); results.Hide(); results.visible = false;
}

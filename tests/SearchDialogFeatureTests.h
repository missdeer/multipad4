#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>
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
    for (int i = 0; i < 10; ++i) SendMessageW(window, WM_TIMER, 1, 0);
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
    SendMessageW(GetDlgItem(window, IDC_FINDALL), BM_CLICK, 0, 0);
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
    d->Send(SCI_SETSEL, 0, 10); frame.ShowSearch(false);
    auto& dialog = frame.searchDialog; require(dialog.hwnd && dialog.document == d);
    require(TabCtrl_GetItemCount(GetDlgItem(dialog.hwnd, IDC_SEARCH_TABS)) == 4);
    wchar_t selection[32]{}; GetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, selection, 32); require(std::wstring(selection) == L"中文😀");
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"alpha");
    frame.searchResults.Clear(); dialog.Execute(IDC_FINDALL);
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
    dialog.SetPage(3); dialog.Execute(IDC_SEARCH_MARK); require(d->Send(SCI_INDICATORALLONFOR, 3) & (1 << 12));
    dialog.Execute(IDC_SEARCH_CLEAR_MARK); require(!(d->Send(SCI_INDICATORALLONFOR, 3) & (1 << 12)));
    CaptureQAWindow(dialog.hwnd, L"Search-Mark");
    CheckRadioButton(dialog.hwnd, IDC_SEARCH_NORMAL, IDC_SEARCH_REGEX, IDC_SEARCH_NORMAL);
    SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"one");
    auto* other = frame.NewDocument(); other->Load({"one one"});
    require(dialog.document == other); dialog.Execute(IDC_SEARCH_ALL_OPEN);
    require(other->search.findText == L"one");
    dialog.SetPage(2);
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-search-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory / L"sub");
    files::WriteAtomic(directory / L"first.txt", {"one one", files::Encoding::Utf16LE});
    files::WriteAtomic(directory / L"sub" / L"second.txt", {"one", files::Encoding::Utf8Bom});
    files::WriteAtomic(directory / L"ignored.bin", {"one"});
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_DIRECTORY, directory.c_str()); SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"*.txt");
    results.Clear(); dialog.Execute(IDC_SEARCH_FILES);
    for (int i = 0; i < 20; ++i) SendMessageW(dialog.hwnd, WM_TIMER, 1, 0);
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
    const auto timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR, DWORD) {
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
            wchar_t title[64]{}; GetWindowTextW(window, title, 64);
            if (std::wstring(title) == L"Replace in Files" && GetDlgItem(window, IDOK)) PostMessageW(window, WM_COMMAND, IDOK, 0);
            return TRUE;
        }, 0);
    });
    require(timer != 0); dialog.Execute(IDC_SEARCH_REPLACE_FILES); KillTimer(nullptr, timer);
    for (int i = 0; i < 20; ++i) SendMessageW(dialog.hwnd, WM_TIMER, 1, 0);
    auto replaced = files::Read(directory / L"first.txt");
    require(replaced.utf8 == "changed changed" && replaced.encoding == files::Encoding::Utf16LE);
    require(files::Read(directory / L"sub" / L"second.txt").encoding == files::Encoding::Utf8Bom);
    require(files::Read(directory / L"ignored.bin").utf8 == "one");
    dialog.Close(); require(!dialog.hwnd && !dialog.document);
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

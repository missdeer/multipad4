// Notepad++ FindReplaceDlg interaction adapted to MDI document contexts.
// Layout provenance and licensing: THIRD_PARTY.md.
#include "SearchDialog.h"
#include "MainFrame.h"
#include "EditSelection.h"
#include <shlobj.h>
#include <shlwapi.h>
#include "ScintillaBatch.h"
namespace {
constexpr UINT ContinueOrdinarySearch = WM_APP + 0x210;
std::wstring Field(HWND window, int id) {
    HWND control = GetDlgItem(window, id); int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    value.resize(GetWindowTextW(control, value.data(), length + 1)); return value;
}
void Remember(std::vector<std::wstring>& history, const std::wstring& value) {
    if (value.empty()) return;
    std::erase(history, value); history.insert(history.begin(), value);
    if (history.size() > 32) history.resize(32);
}
void Fill(HWND window, int id, const std::vector<std::wstring>& history, const std::wstring& value) {
    for (const auto& item : history) SendDlgItemMessageW(window, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    SetDlgItemTextW(window, id, value.c_str());
}
struct UseSearch {
    EditorDocument& document; SearchState original;
    UseSearch(EditorDocument& d, const SearchState& state) : document(d), original(d.search) { d.search = state; }
    ~UseSearch() { document.search = std::move(original); }
};
}
SearchDialog::~SearchDialog() {
    if (replaceWork) {
        replaceWork->cancel.store(true);
        try { if (replaceWork->task.Valid()) replaceWork->task.Get(); } catch (...) {}
    }
}
BOOL SearchDialog::PreTranslateMessage(MSG* message) {
    if (!hwnd || (message->hwnd != hwnd && !IsChild(hwnd, message->hwnd))) return FALSE;
    if (accelerators && TranslateAcceleratorW(hwnd, accelerators, message)) return TRUE;
    return IsDialogMessageW(hwnd, message);
}
void SearchDialog::Open(MainFrame& frame, EditorDocument& target, bool replace) {
    if (hwnd && document == &target) {
        SetPage(replace ? 1 : 0);
        SetDlgItemTextW(hwnd, IDC_FINDTEXT, target.search.findText.c_str());
    } else {
        Close(); owner = &frame; document = &target; replacing = replace; page = replace ? 1 : 0;
        accelerators = LoadAcceleratorsW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_ACCFINDREPLACE));
        hwnd = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SEARCH), frame.m_hWnd, Procedure, reinterpret_cast<LPARAM>(this));
    }
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(GetDlgItem(hwnd, IDC_FINDTEXT));
    }
}
void SearchDialog::Close() {
    if (hwnd && IsWindow(hwnd)) {
        if (ordinaryTask) FinishSearch(true);
        if (fileRunning) FinishFiles(true);
        if (document && owner && std::any_of(owner->documents.begin(), owner->documents.end(), [&](const auto& d) { return d.get() == document; })) ReadFields();
        owner->settings.WriteString(L"Search", L"TransparentAlways", transparentAlways ? L"1" : L"0");
        owner->settings.WriteString(L"Search", L"Opacity", std::to_wstring(opacity));
        GetWindowRect(hwnd, &savedPosition); hasSavedPosition = true; DestroyWindow(hwnd);
    }
    hwnd = nullptr; document = nullptr;
}
void SearchDialog::SyncDocument() {
    if (!hwnd || !owner) return;
    auto* target = owner->Active();
    if (!target || target == document) return;
    // The controls hold the ongoing query; changing tabs must not reload an
    // older query or keep searching the editor from which the dialog opened.
    ReadFields();
    document = target;
    ReadFields();
    document->search.lastMatchStart = document->search.lastMatchEnd = -1;
}
bool SearchDialog::ReadFields() {
    if (!hwnd || !document) return false;
    auto& s = document->search;
    const auto text = Field(hwnd, IDC_FINDTEXT);
    const int flags = (IsDlgButtonChecked(hwnd, IDC_FINDCASE) == BST_CHECKED ? SCFIND_MATCHCASE : 0) |
        (IsDlgButtonChecked(hwnd, IDC_FINDWORD) == BST_CHECKED ? SCFIND_WHOLEWORD : 0);
    bool regex = IsDlgButtonChecked(hwnd, IDC_SEARCH_REGEX) == BST_CHECKED;
    bool extended = IsDlgButtonChecked(hwnd, IDC_SEARCH_EXTENDED) == BST_CHECKED;
    bool dot = IsDlgButtonChecked(hwnd, IDC_FIND_DOTALL) == BST_CHECKED;
    if (s.findText != text || s.flags != flags || s.regex != regex || s.transformBackslashes != extended || s.dotMatchesNewline != dot)
        s.lastMatchStart = s.lastMatchEnd = -1;
    s.findText = text; s.replaceText = Field(hwnd, IDC_REPLACETEXT); s.flags = flags;
    s.regex = regex; s.wildcard = false; s.transformBackslashes = extended; s.dotMatchesNewline = dot;
    s.useBoostRegex = regex; s.wrap = IsDlgButtonChecked(hwnd, IDC_SEARCH_WRAP) == BST_CHECKED;
    s.closeAfterAction = s.closeAfterFind = s.closeAfterReplace = false;
    s.transparent = IsDlgButtonChecked(hwnd, IDC_SEARCH_TRANSPARENT) == BST_CHECKED;
    s.bookmarkMatches = IsDlgButtonChecked(hwnd, IDC_FINDALLBOOKMARK) == BST_CHECKED;
    Remember(s.findHistory, s.findText); Remember(s.replaceHistory, s.replaceText);
    return !s.findText.empty();
}
bool SearchDialog::Chinese() const {
    return PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
}
void SearchDialog::Localize() {
    if (!Chinese()) return;
    const std::pair<int, const wchar_t*> labels[] = {
        {IDC_SEARCH_FIND_LABEL, L"查找目标(&F):"}, {IDC_SEARCH_REPLACE_LABEL, L"替换为(&L):"},
        {IDC_SEARCH_FILTER_LABEL, L"文件类型(&S):"}, {IDC_SEARCH_DIRECTORY_LABEL, L"目录(&E):"},
        {IDC_FINDWORD, L"全词匹配(&W)"}, {IDC_FINDCASE, L"匹配大小写(&C)"},
        {IDC_SEARCH_RECURSIVE, L"包含子目录(&R)"}, {IDC_SEARCH_HIDDEN, L"包含隐藏目录(&H)"},
        {IDC_SEARCH_BACKWARDS, L"反向查找"}, {IDC_SEARCH_WRAP, L"循环查找(&P)"},
        {IDC_SEARCH_SELECTION, L"在选区中(&I)"}, {IDC_FINDALLBOOKMARK, L"标记所在行"},
        {IDC_SEARCH_PURGE, L"每次查找前清除标记"}, {IDC_SEARCH_MODE_GROUP, L"查找模式"},
        {IDC_SEARCH_NORMAL, L"普通(&N)"}, {IDC_SEARCH_EXTENDED, L"扩展(&X) (\\n, \\r, \\t, \\0, \\x...)"},
        {IDC_SEARCH_REGEX, L"正则表达式(&G)"}, {IDC_FIND_DOTALL, L". 匹配新行"},
        {IDC_SEARCH_TRANSPARENT, L"透明度(&Y)"}, {IDC_SEARCH_TRANSPARENT_LOST, L"失去焦点后"},
        {IDC_SEARCH_TRANSPARENT_ALWAYS, L"始终"}, {IDOK, L"查找下一个"},
        {IDC_FINDPREV, L"查找上一个"}, {IDC_SEARCH_COUNT, L"计数"},
        {IDC_FINDALL, L"在当前文档中全部查找"}, {IDC_SEARCH_ALL_OPEN, L"在所有打开的文档中查找"},
        {IDC_REPLACE, L"替换"}, {IDC_REPLACEALL, L"全部替换"},
        {IDC_SEARCH_REPLACE_OPEN, L"在所有打开的文档中替换"}, {IDC_SEARCH_FILES, L"全部查找"},
        {IDC_SEARCH_REPLACE_FILES, L"在文件中替换"}, {IDC_SEARCH_MARK, L"全部标记"},
        {IDC_SEARCH_CLEAR_MARK, L"清除所有标记"}, {IDCANCEL, L"关闭"}
    };
    for (const auto& [id, text] : labels) SetDlgItemTextW(hwnd, id, text);
}
void SearchDialog::Layout() {
    if (!layoutWidth) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    const int dx = client.right - layoutWidth;
    for (const auto& control : controls) {
        RECT r = control.rectangle;
        const int id = GetDlgCtrlID(control.control);
        if (control.right) OffsetRect(&r, dx, 0);
        if (control.stretch) r.right += dx;
        if (id == IDCANCEL && page == 2) {
            RECT units{298, 62, 389, 76};
            MapDialogRect(hwnd, &units);
            r = units;
            OffsetRect(&r, dx, 0);
        }
        if (id == IDC_SEARCH_STATUS && !expanded) {
            RECT units{8, 138, 388, 146};
            MapDialogRect(hwnd, &units);
            r = units;
            r.right += dx;
        }
        if (id == IDC_SEARCH_EXPAND && !expanded) {
            RECT units{393, 122, 409, 136};
            MapDialogRect(hwnd, &units);
            r = units;
            OffsetRect(&r, dx, 0);
        }
        SetWindowPos(control.control, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
void SearchDialog::ResizeHeight() {
    RECT client{}, rectangle{}, units{0, 0, 0, 147};
    GetClientRect(hwnd, &client);
    GetWindowRect(hwnd, &rectangle);
    MapDialogRect(hwnd, &units);
    const int height = expanded ? layoutHeight : units.bottom;
    SetWindowPos(hwnd, nullptr, 0, 0, rectangle.right - rectangle.left,
        height + rectangle.bottom - rectangle.top - client.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
}
void SearchDialog::UpdateTransparency() {
    const bool enabled = IsDlgButtonChecked(hwnd, IDC_SEARCH_TRANSPARENT) == BST_CHECKED;
    for (int id : {IDC_SEARCH_TRANSPARENT_LOST, IDC_SEARCH_TRANSPARENT_ALWAYS, IDC_SEARCH_OPACITY})
        EnableWindow(GetDlgItem(hwnd, id), enabled);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (enabled) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>((active && !transparentAlways) ? 255 : opacity), LWA_ALPHA);
    } else if (style & WS_EX_LAYERED) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }
}
void SearchDialog::WatchMappedTask() {
    pendingIdentity = document->searchIdentity; SetTimer(hwnd, 2, 50, nullptr);
}
void SearchDialog::Status(const std::wstring& text) { SetDlgItemTextW(hwnd, IDC_SEARCH_STATUS, text.c_str()); }
void SearchDialog::SetPage(int value) {
    if (ordinaryTask) FinishSearch(true);
    if (fileRunning) FinishFiles(true);
    page = value; replacing = page == 1;
    TabCtrl_SetCurSel(GetDlgItem(hwnd, IDC_SEARCH_TABS), page);
    const wchar_t* names[] = {L"Find", L"Replace", L"Find in Files", L"Mark"};
    const wchar_t* chineseNames[] = {L"查找", L"替换", L"文件中查找", L"标记"};
    SetWindowTextW(hwnd, Chinese() ? chineseNames[page] : names[page]);
    auto show = [&](int id, bool visible) { ShowWindow(GetDlgItem(hwnd, id), visible ? SW_SHOW : SW_HIDE); };
    show(IDC_REPLACETEXT, page == 1 || page == 2); show(IDC_SEARCH_REPLACE_LABEL, page == 1 || page == 2);
    show(IDC_SEARCH_SWAP, page == 1 || page == 2);

    for (int id : {IDC_SEARCH_FILTER, IDC_SEARCH_FILTER_LABEL, IDC_SEARCH_DIRECTORY, IDC_SEARCH_DIRECTORY_LABEL,
                   IDC_SEARCH_RECURSIVE, IDC_SEARCH_HIDDEN, IDC_SEARCH_BROWSE, IDC_SEARCH_CURRENT_DIRECTORY, IDC_SEARCH_FILTER_HELP, IDC_SEARCH_FILES, IDC_SEARCH_REPLACE_FILES}) show(id, page == 2);
    for (int id : {IDOK, IDC_SEARCH_COUNT, IDC_FINDALL, IDC_SEARCH_ALL_OPEN}) show(id, page == 0);
    show(IDOK, page == 0 || page == 1); show(IDC_FINDPREV, page == 0);
    for (int id : {IDC_REPLACE, IDC_REPLACEALL, IDC_SEARCH_REPLACE_OPEN}) show(id, page == 1);
    for (int id : {IDC_SEARCH_MARK, IDC_SEARCH_CLEAR_MARK, IDC_FINDALLBOOKMARK, IDC_SEARCH_PURGE}) show(id, page == 3);
    show(IDC_SEARCH_BACKWARDS, page < 2); show(IDC_SEARCH_WRAP, page < 2);
    show(IDC_SEARCH_SELECTION, page != 2);
    SendMessageW(hwnd, DM_SETDEFID, page == 2 ? IDC_SEARCH_FILES : page == 3 ? IDC_SEARCH_MARK : IDOK, 0);
    Status(L"");
    for (int id : {IDC_SEARCH_MODE_GROUP, IDC_SEARCH_NORMAL, IDC_SEARCH_EXTENDED, IDC_SEARCH_REGEX,
                   IDC_FIND_DOTALL, IDC_SEARCH_TRANSPARENT, IDC_SEARCH_TRANSPARENT_GROUP,
                   IDC_SEARCH_TRANSPARENT_LOST, IDC_SEARCH_TRANSPARENT_ALWAYS, IDC_SEARCH_OPACITY}) show(id, expanded);
    SetDlgItemTextW(hwnd, IDC_SEARCH_EXPAND, expanded ? L"⌃" : L"⌄");
    ResizeHeight();
}
void SearchDialog::Execute(int command) {
    if (ordinaryTask) { Status(L"A search is running. Close to cancel."); return; }
    if (fileRunning) { Status(L"A file search is running. Close to cancel."); return; }
    // Resolve synchronously too: WM_MDIACTIVATE notifications are posted and
    // may still be queued when a button or accelerator invokes this command.
    SyncDocument();
    if (!document) return;
    if (command == IDC_SEARCH_CLEAR_MARK) {
        if (document->FastView()) {
            document->fastView->StopWork(); document->fastView->SetMarks(document->fastView->Model().Capture(), std::vector<large::SearchHit>{}, false);
        }
        document->Send(SCI_SETINDICATORCURRENT, 12); document->Send(SCI_INDICATORCLEARRANGE, 0, document->Send(SCI_GETLENGTH));
        if (IsDlgButtonChecked(hwnd, IDC_FINDALLBOOKMARK) == BST_CHECKED) document->Send(SCI_MARKERDELETEALL, 0);
        Status(L"Marks cleared."); return;
    }
    if (!ReadFields()) { Status(L"Enter text to find."); return; }
    auto& target = *document;
    bool selection = IsDlgButtonChecked(hwnd, IDC_SEARCH_SELECTION) == BST_CHECKED;
    if (selection && RejectRectangularEdit(target.hwnd)) return;
    if (command == IDC_SEARCH_FILES || command == IDC_SEARCH_REPLACE_FILES) { StartFiles(command == IDC_SEARCH_REPLACE_FILES); return; }
    if (target.FastView() && (command == IDC_SEARCH_COUNT || command == IDC_FINDALL || command == IDC_SEARCH_MARK)) {
        auto* view = target.fastView.get(); auto* targetDocument = &target; auto* frame = owner;
        const auto identity = target.searchIdentity; const auto dialog = hwnd; const bool mark = command == IDC_SEARCH_MARK;
        const bool purge = IsDlgButtonChecked(hwnd, IDC_SEARCH_PURGE) == BST_CHECKED;
        const bool bookmarks = target.search.bookmarkMatches;
        if (command == IDC_FINDALL) owner->searchResults.Begin(target.search.findText);
        const auto resultGeneration = frame->searchResults.generation;
        view->StartEnumerate(target.MappedSearchOptions(), selection, [view, targetDocument, frame, identity, dialog, command, mark, purge, bookmarks, resultGeneration](const auto& snapshot, const auto& hits) {
            if (!std::any_of(frame->documents.begin(), frame->documents.end(), [&](const auto& d) { return d.get() == targetDocument && d->searchIdentity == identity; })) return;
            if (mark) view->SetMarks(snapshot, hits, bookmarks, purge);
            if (command == IDC_FINDALL && frame->searchResults.generation == resultGeneration) { frame->searchResults.AddMapped(*targetDocument, snapshot, hits); frame->searchResults.Finish(hits->Count(), hits->Count() ? 1 : 0); }
            if (frame->searchDialog.hwnd == dialog && frame->searchDialog.document == targetDocument && IsWindow(dialog)) SetDlgItemTextW(dialog, IDC_SEARCH_STATUS, (std::to_wstring(hits->Count()) + L" matches.").c_str());
            frame->Refresh();
        }, command != IDC_SEARCH_COUNT, command == IDC_FINDALL);
        WatchMappedTask(); Status(L"Searching… Press Esc in the document to cancel."); return;
    }
    if (target.FastView() && command == IDC_REPLACEALL) {
        target.fastView->StartReplaceEvery(target.MappedSearchOptions(), target.SearchReplacement(),
            (target.search.regex || target.search.wildcard) && target.search.replaceText != L"^c", selection);
        WatchMappedTask(); Status(L"Replacing… Press Esc in the document to cancel."); return;
    }
    if (command == IDC_SEARCH_COUNT || command == IDC_FINDALL || command == IDC_SEARCH_ALL_OPEN || command == IDC_SEARCH_MARK) {
        StartSearch(command, selection); return;
    }
    if (command == IDC_REPLACEALL || command == IDC_SEARCH_REPLACE_OPEN) {
        if (command == IDC_SEARCH_REPLACE_OPEN && MessageBoxW(hwnd, L"Replace all matches in all opened documents? Each document can be undone separately.", L"Replace", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        size_t count = 0;
        if (command == IDC_SEARCH_REPLACE_OPEN) {
            // Validate all patterns before modifying any document.
            for (const auto& d : owner->documents) { UseSearch state(*d, target.search); d->ValidateSearch(true); }
            for (const auto& d : owner->documents) { UseSearch state(*d, target.search); count += d->ReplaceAll(); }
        } else count = target.ReplaceAll(selection);
        Status(std::to_wstring(count) + L" occurrences replaced."); owner->Refresh(); return;
    }
    bool backwards = command == IDC_FINDPREV || (command == IDOK && IsDlgButtonChecked(hwnd, IDC_SEARCH_BACKWARDS) == BST_CHECKED);
    bool replaced = command == IDC_REPLACE && target.ReplaceOne();
    bool found = false;
    if (target.FastView() && selection) { target.fastView->StartSearch(target.MappedSearchOptions(), backwards, target.search.wrap, true); found = true; }
    else found = target.FindNext(backwards);
    if (target.FastView() && found) WatchMappedTask();
    Status(target.FastView() && found ? L"Searching… Press Esc in the document to cancel." : found ? (replaced ? L"Replaced. Next match found." : L"Match found.") : L"No further match found."); owner->Refresh();
}
void SearchDialog::StartSearch(int command, bool selection) {
    ordinaryTask = std::make_unique<SearchTask>(); auto& task = *ordinaryTask;
    task.token = ++nextSearchToken;
    task.command = command; task.state = document->search; task.selection = selection;
    task.purge = IsDlgButtonChecked(hwnd, IDC_SEARCH_PURGE) == BST_CHECKED;
    if (command == IDC_SEARCH_ALL_OPEN) {
        for (int i = 0; i < owner->tabs.GetPageCount(); ++i)
            task.documents.push_back(static_cast<EditorDocument*>(owner->tabs.GetPageData(i))->searchIdentity);
    } else task.documents.push_back(document->searchIdentity);
    if (command == IDC_FINDALL || command == IDC_SEARCH_ALL_OPEN) owner->searchResults.Begin(task.state.findText);
    task.generation = owner->searchResults.generation;
    Status(L"Searching… Close to cancel."); SetTimer(hwnd, 3, 20, nullptr);
    ContinueSearch();
}
void SearchDialog::ContinueSearch() {
    SearchStep();
    if (ordinaryTask && !ordinaryTask->mapped) PostMessageW(hwnd, ContinueOrdinarySearch, ordinaryTask->token, 0);
}
void SearchDialog::FinishSearch(bool interrupted) {
    if (!ordinaryTask) return;
    auto task = std::move(ordinaryTask); KillTimer(hwnd, 3);
    if (task->mapped) {
        auto found = owner->tabs.documentsById.find(task->identity);
        if (found != owner->tabs.documentsById.end() && found->second->FastView()) found->second->fastView->StopWork();
    }
    if ((task->command == IDC_FINDALL || task->command == IDC_SEARCH_ALL_OPEN) && task->generation == owner->searchResults.generation)
        owner->searchResults.Finish(task->count, task->files);
    Status((interrupted ? L"Interrupted. " : L"Completed. ") + std::to_wstring(task->count) + L" matches in " + std::to_wstring(task->files) + L" documents.");
}
void SearchDialog::SearchStep() {
    if (!ordinaryTask) return;
    auto& task = *ordinaryTask;
    const bool finder = task.command == IDC_FINDALL || task.command == IDC_SEARCH_ALL_OPEN;
    if (finder && task.generation != owner->searchResults.generation) { FinishSearch(true); return; }
    if (task.mapped && task.mappedDone) { task.mapped = false; ++task.next; }
    if (task.next >= task.documents.size()) { FinishSearch(false); return; }
    const auto identity = task.documents[task.next];
    auto found = owner->tabs.documentsById.find(identity);
    if (found == owner->tabs.documentsById.end()) { FinishSearch(true); return; }
    auto& target = *found->second;
    if (task.identity != identity) {
        task.identity = identity; task.revision = target.style.revision;
        UseSearch state(target, task.state);
        if (target.FastView()) {
            task.mapped = true; task.mappedDone = false;
            target.fastView->StartEnumerate(target.MappedSearchOptions(), task.selection && task.command != IDC_SEARCH_ALL_OPEN,
                [this, identity](const auto& snapshot, const auto& hits) {
                    if (!ordinaryTask || ordinaryTask->identity != identity) return;
                    auto& current = *ordinaryTask;
                    auto found = owner->tabs.documentsById.find(identity);
                    if (found == owner->tabs.documentsById.end()) return;
                    if ((current.command == IDC_FINDALL || current.command == IDC_SEARCH_ALL_OPEN) && current.generation != owner->searchResults.generation) return;
                    if (current.command == IDC_SEARCH_MARK) found->second->fastView->SetMarks(snapshot, hits, current.state.bookmarkMatches, current.purge);
                    else if (current.command != IDC_SEARCH_COUNT && hits->Count()) owner->searchResults.AddMapped(*found->second, snapshot, hits);
                    current.count += hits->Count(); if (hits->Count()) ++current.files;
                    current.mappedDone = true;
                }, task.command != IDC_SEARCH_COUNT, finder);
            return;
        }
        task.cursor = target.BeginMatches(task.selection && task.command != IDC_SEARCH_ALL_OPEN);
        task.result = std::make_shared<OrdinarySearchResult>();
        task.result->hits = std::make_shared<large::HitArchive>(task.command != IDC_SEARCH_COUNT);
    }
    if (task.mapped) {
        if (!target.FastView() || (!target.fastView->Busy() && !task.mappedDone)) { FinishSearch(true); return; }
        return;
    }
    if (target.FastView() || target.style.revision != task.revision) { FinishSearch(true); return; }
    auto& result = *task.result;
    const bool done = target.MatchStep(task.cursor, [&](LRESULT start, LRESULT end) {
        if (task.command == IDC_SEARCH_COUNT) { result.hits->AddCount(1); return; }
        const auto line = finder || task.state.bookmarkMatches ? target.Send(SCI_LINEFROMPOSITION, start) : 0;
        const auto lineStart = finder ? target.Send(SCI_POSITIONFROMLINE, line) : 0;
        result.hits->Append({static_cast<large::Position>(start), static_cast<large::Position>(end), {}, static_cast<large::Position>(line), static_cast<large::Position>(lineStart)});
        if (finder) {
            const auto limit = (std::max)(end, target.Send(SCI_GETLINEENDPOSITION, line));
            if (result.lines.empty() || result.lines.back().number != line)
                result.lines.push_back({line, lineStart, std::make_shared<const std::string>(target.Range(lineStart, limit - lineStart))});
            else if (static_cast<size_t>(limit - lineStart) > result.lines.back().text->size())
                result.lines.back().text = std::make_shared<const std::string>(target.Range(lineStart, limit - lineStart));
        }
    });
    if (!done) return;
    if (task.command == IDC_SEARCH_MARK) {
        struct Span { LRESULT start, end; };
        std::vector<Span> spans; spans.reserve(result.hits->Count());
        std::vector<LRESULT> bookmarks;
        for (size_t offset = 0; offset < result.hits->Count(); offset += 512)
            for (const auto& hit : result.hits->Page(offset, 512)) {
                spans.push_back({static_cast<LRESULT>(hit.start), static_cast<LRESULT>(hit.end)});
                if (task.state.bookmarkMatches && (bookmarks.empty() || bookmarks.back() != static_cast<LRESULT>(hit.line))) bookmarks.push_back(static_cast<LRESULT>(hit.line));
            }
        {
            struct Batch { EditorDocument& document;
                Batch(EditorDocument& value) : document(value) { document.Send(BeginIndicatorBatch); }
                ~Batch() { document.Send(EndIndicatorBatch); }
            } batch(target);
            target.Send(SCI_SETINDICATORCURRENT, 12); target.Send(SCI_INDICSETSTYLE, 12, INDIC_ROUNDBOX);
            target.Send(SCI_INDICSETFORE, 12, RGB(235, 160, 0));
            if (task.purge) {
                target.Send(SCI_INDICATORCLEARRANGE, 0, target.Send(SCI_GETLENGTH));
            }
            for (const auto& span : spans) target.Send(SCI_INDICATORFILLRANGE, span.start, span.end - span.start);
        }
        if (task.purge && task.state.bookmarkMatches) target.Send(SCI_MARKERDELETEALL, 0);
        for (const auto line : bookmarks) target.Send(SCI_MARKERADD, line, 0);
    } else if (finder && result.hits->Count()) owner->searchResults.AddOrdinary(target, task.result);
    task.count += result.hits->Count(); if (result.hits->Count()) ++task.files;
    task.result.reset(); ++task.next;
    if (task.next == task.documents.size()) FinishSearch(false);
}
void SearchDialog::StartFiles(bool replace) {
    ServiceReplacement();
    if (replaceWork) { Status(L"The previous replacement is stopping. Please retry shortly."); return; }
    auto path = std::filesystem::path(Field(hwnd, IDC_SEARCH_DIRECTORY));
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) { Status(L"Select an existing directory."); return; }
    if (replace && MessageBoxW(hwnd, L"Replace matching text in files on disk using the replacement shown here? This cannot be undone. Open files are changed only in their editor buffers.", L"Replace in Files", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
    if (replace) document->ValidateSearch(true);
    // Read-only queries are compiled against an empty snapshot by FileSearch.
    fileSearch = document->search; replaceFiles = replace;
    fileFilter = Field(hwnd, IDC_SEARCH_FILTER); if (fileFilter.empty()) fileFilter = L"*.*";
    recursive = IsDlgButtonChecked(hwnd, IDC_SEARCH_RECURSIVE) == BST_CHECKED;
    hidden = IsDlgButtonChecked(hwnd, IDC_SEARCH_HIDDEN) == BST_CHECKED;
    fileHits = foundFiles = fileErrors = 0;
    files::DirectorySearchOptions options{std::filesystem::absolute(path), fileFilter, recursive, hidden};
    if (replace) {
        replacePattern = document->SearchReplacePattern();
        replaceWalker = std::make_shared<files::SearchWalker>(options);
        SetTimer(owner->m_hWnd, 0x282, 10, nullptr);
    }
    else {
        std::vector<std::filesystem::path> openPaths;
        for (const auto& d : owner->documents) if (!d->path.empty()) openPaths.push_back(d->path);
        fileTask = std::make_unique<files::FileSearch>(options, document->MappedSearchOptions(), std::move(openPaths));
    }
    fileRunning = true; owner->searchResults.Begin(fileSearch.findText); fileGeneration = owner->searchResults.generation;
    SetDlgItemTextW(hwnd, IDCANCEL, Chinese() ? L"停止 / 关闭" : L"Stop / Close"); SetTimer(hwnd, 1, 10, nullptr);
    Status(L"Searching files...");
}
void SearchDialog::FinishFiles(bool cancelled) {
    KillTimer(hwnd, 1); fileRunning = false;
    if (openSearch) {
        openSearch->cancel.store(true);
        if (openSearch->mapped) {
            auto found = owner->tabs.documentsById.find(openSearch->identity);
            if (found != owner->tabs.documentsById.end() && found->second->FastView()) found->second->fastView->StopWork();
        }
        if (openSearch->search.Valid()) openSearch->search.Wait();
        openSearch.reset();
    }
    if (fileTask) { fileTask->Stop(); fileErrors += fileTask->Errors(); fileTask.reset(); }
    if (replaceWork) replaceWork->cancel.store(true);
    replaceWalker.reset();
    if ((!replaceFiles || !replaceWork) && owner->searchResults.generation == fileGeneration) owner->searchResults.Finish(fileHits, foundFiles);
    Status((cancelled ? L"Stopped. " : L"Completed. ") + std::to_wstring(fileHits) + L" matches; " + std::to_wstring(fileErrors) + L" files/folders skipped due to errors.");
    SetDlgItemTextW(hwnd, IDCANCEL, Chinese() ? L"关闭" : L"Close"); owner->Refresh();
}
void SearchDialog::FileStep() {
    if (!fileRunning) return;
    if (owner->searchResults.generation != fileGeneration) { FinishFiles(true); return; }
    if (replaceFiles) { ReplaceFileStep(); return; }
    if (openSearch) {
        auto task = openSearch;
        auto found = owner->tabs.documentsById.find(task->identity);
        if (found == owner->tabs.documentsById.end() || found->second->style.revision != task->revision) {
            task->cancel.store(true);
            if (task->mapped && found != owner->tabs.documentsById.end() && found->second->FastView()) found->second->fastView->StopWork();
            if (task->search.Valid()) task->search.Wait();
            openSearch.reset(); ++fileErrors; return;
        }
        auto& target = *found->second;
        if (task->mapped) {
            if (!target.FastView() || !target.fastView->Busy()) { ++fileErrors; openSearch.reset(); }
            return;
        }
        if (!task->search.Valid()) {
            if (task->offset < task->length) {
                auto bytes = target.Range(task->offset, (std::min)(size_t(256 * 1024), task->length - task->offset));
                task->offset += bytes.size(); task->text->append(bytes);
                return;
            }
            auto snapshot = large::Document::FromUtf8(task->text)->Capture();
            UseSearch state(target, fileSearch); auto options = target.MappedSearchOptions();
            task->search = work::Submit([ptr = task.get(), snapshot, options] { return files::SearchSnapshot(snapshot, options, ptr->cancel); });
            return;
        }
        if (!task->search.Ready()) return;
        try {
            auto result = task->search.Get(); result.path = task->path;
            if (result.archive->Count()) {
                fileHits += result.archive->Count(); ++foundFiles;
                owner->searchResults.AddFile(std::move(result), task->identity, task->revision);
            }
        } catch (const std::exception&) { ++fileErrors; }
        openSearch.reset();
        return;
    }
    files::FileSearchResult result;
    if (fileTask->Poll(result)) {
        EditorDocument* open = nullptr;
        for (const auto& d : owner->documents) {
            std::error_code error;
            if (!d->path.empty() && std::filesystem::equivalent(d->path, result.path, error)) { open = d.get(); break; }
        }
        if (open) {
            UseSearch state(*open, fileSearch);
            if (open->FastView()) {
                // The view owns cancellation and validates its snapshot before publishing.
                auto identity = open->searchIdentity; auto generation = fileGeneration;
                openSearch = std::make_shared<OpenSearch>(); openSearch->identity = identity;
                openSearch->revision = open->style.revision; openSearch->mapped = true;
                open->fastView->StartEnumerate(open->MappedSearchOptions(), false,
                    [this, identity, generation](const auto& snapshot, const auto& hits) {
                        if (!fileRunning || generation != owner->searchResults.generation || !openSearch || openSearch->identity != identity) return;
                        auto found = owner->tabs.documentsById.find(identity);
                        if (found != owner->tabs.documentsById.end() && hits->Count()) {
                            owner->searchResults.AddMapped(*found->second, snapshot, hits); fileHits += hits->Count(); ++foundFiles;
                        }
                        openSearch.reset();
                    }, true, true);
            } else {
                openSearch = std::make_shared<OpenSearch>();
                openSearch->identity = open->searchIdentity; openSearch->revision = open->style.revision;
                openSearch->path = open->path; openSearch->length = static_cast<size_t>(open->Send(SCI_GETLENGTH));
                openSearch->text->reserve(openSearch->length);
            }
        } else if (!result.openDocument && result.archive) {
            fileHits += result.archive->Count(); ++foundFiles;
            owner->searchResults.AddFile(std::move(result));
        }
    }
    if (!openSearch && fileTask->Done()) FinishFiles(false);

}
bool SearchDialog::CommittingReplacement() const {
    return replaceWork && replaceWork->stage == ReplaceWork::Stage::Commit && replaceWork->task.Valid();
}
bool SearchDialog::DeferOpen(const std::filesystem::path& path, std::optional<files::Encoding> encoding) {
    if (!CommittingReplacement()) return false;
    deferredOpens.push_back({path, encoding}); return true;
}
void SearchDialog::ServiceReplacement() {
    if (fileRunning && replaceFiles) return;
    if (replaceWork && (!replaceWork->task.Valid() || replaceWork->task.Ready())) {
        auto task = replaceWork;
        try { if (task->task.Valid()) task->task.Get(); }
        catch (...) { if (!task->cancel.load() || task->committed) ++fileErrors; }
        fileErrors += std::exchange(task->errors, 0);
        if (task->committed) {
            fileHits += task->result.hits.archive->Count(); ++foundFiles;
            if (owner->searchResults.generation == fileGeneration) owner->searchResults.AddFile(std::move(task->result.hits));
        }
        replaceWork.reset();
        if (owner->searchResults.generation == fileGeneration) owner->searchResults.Finish(fileHits, foundFiles);
        if (hwnd && IsWindow(hwnd) && page == 2 && owner->searchResults.generation == fileGeneration) {
            Status(L"Stopped. " + std::to_wstring(fileHits) + L" matches; " + std::to_wstring(fileErrors) + L" files/folders skipped due to errors.");
        }
    }
    if (!CommittingReplacement()) {
        auto pending = std::move(deferredOpens); deferredOpens.clear();
        for (const auto& request : pending) owner->Open(request.path, request.encoding);
    }
    if (!replaceWork && deferredOpens.empty()) KillTimer(owner->m_hWnd, 0x282);
}
void SearchDialog::ReplaceFileStep() {
    if (!replaceWork) {
        replaceWork = std::make_shared<ReplaceWork>();
        auto task = replaceWork; auto walker = replaceWalker;
        task->task = work::Submit([task = task.get(), walker] { task->path = walker->Next(task->cancel, task->errors); });
        return;
    }
    auto task = replaceWork;
    if (task->task.Valid() && !task->task.Ready()) return;
    try {
        if (task->task.Valid()) task->task.Get();
        fileErrors += std::exchange(task->errors, 0);
        if (task->stage == ReplaceWork::Stage::Commit) {
            if (task->committed) {
                fileHits += task->result.hits.archive->Count(); ++foundFiles;
                owner->searchResults.AddFile(std::move(task->result.hits));
            }
            replaceWork.reset();
            auto pending = std::move(deferredOpens); deferredOpens.clear();
            for (const auto& request : pending) owner->Open(request.path, request.encoding);
            return;
        }
        if (!task->path) { replaceWork.reset(); FinishFiles(false); return; }
        EditorDocument* open = nullptr;
        for (const auto& d : owner->documents) {
            std::error_code error;
            if (!d->path.empty() && std::filesystem::equivalent(d->path, *task->path, error)) { open = d.get(); break; }
        }
        if (task->stage == ReplaceWork::Stage::Walk) {
            Status(L"Replacing: " + task->path->wstring());
            if (open) {
                if (open->FastView()) {
                    // Mapped documents retain their owner-thread matcher and native encoding semantics.
                    UseSearch state(*open, fileSearch); auto matches = open->CollectMatches();
                    if (!matches.empty()) { fileHits += open->ReplaceAll(); ++foundFiles; owner->searchResults.Add(*open, matches); }
                    replaceWork.reset(); return;
                }
                task->identity = open->searchIdentity; task->revision = open->style.revision;
                task->length = static_cast<size_t>(open->Send(SCI_GETLENGTH));
                task->input = std::make_shared<std::string>();
            }
            task->stage = ReplaceWork::Stage::Prepare;
        }
        if (task->input && !task->result.hits.archive) {
            auto found = owner->tabs.documentsById.find(task->identity);
            if (found == owner->tabs.documentsById.end() || found->second->style.revision != task->revision)
                throw std::runtime_error("Replacement conflict; buffer changed.");
            if (task->offset < task->length) {
                auto bytes = found->second->Range(task->offset, (std::min)(size_t(256 * 1024), task->length - task->offset));
                task->offset += bytes.size(); task->input->append(bytes); return;
            }
        }
        if (!task->result.hits.archive) {
            const auto pattern = replacePattern;
            task->task = work::Submit([task = task.get(), pattern] {
                files::Text input;
                if (task->input) input.utf8 = std::move(*task->input);
                else {
                    task->version = files::ReplacementVersion(*task->path);
                    input = files::ReadReplacement(*task->path, task->cancel);
                    if (input.utf8.find('\0') != std::string::npos) { task->result.hits.archive = std::make_shared<large::HitArchive>(); return; } // Closed binary files are excluded; open buffers support NUL.
                    if (files::ReplacementVersion(*task->path) != task->version) throw std::runtime_error("Replacement conflict; file changed.");
                }
                task->result = files::ReplaceContents(std::move(input), pattern, task->cancel);
                task->result.hits.path = *task->path;
                task->result.hits.originalSize = task->version.size; task->result.hits.originalWriteTime = task->version.write;
                if (!task->result.hits.archive) task->result.hits.archive = std::make_shared<large::HitArchive>();
            }); return;
        }
        if (!task->result.replacements) { replaceWork.reset(); return; }
        if (task->identity) {
            auto found = owner->tabs.documentsById.find(task->identity);
            if (found == owner->tabs.documentsById.end() || found->second->style.revision != task->revision || found->second->FastView())
                throw std::runtime_error("Replacement conflict; buffer changed.");
            auto& target = *found->second;
            if (target.Send(SCI_GETREADONLY)) throw std::runtime_error("Replacement buffer is read-only.");
            target.Send(SCI_BEGINUNDOACTION);
            target.Send(SCI_SETTARGETRANGE, 0, target.Send(SCI_GETLENGTH));
            target.Send(SCI_REPLACETARGET, task->result.text.utf8.size(), reinterpret_cast<LPARAM>(task->result.text.utf8.data()));
            target.Send(SCI_ENDUNDOACTION);
            fileHits += task->result.hits.archive->Count(); ++foundFiles;
            owner->searchResults.AddFile(std::move(task->result.hits), task->identity, target.style.revision); replaceWork.reset(); owner->Refresh(); return;
        }
        if (open) throw std::runtime_error("Replacement conflict; file became open.");
        // New opens are queued from this owner-thread check until commit completion.
        task->stage = ReplaceWork::Stage::Commit;
        task->task = work::Submit([task = task.get()] {
            files::CommitReplacement(*task->path, task->result.text, task->version, task->cancel, [task] {
                task->committed = true;
                const auto version = files::ReplacementVersion(*task->path);
                task->result.hits.originalSize = version.size; task->result.hits.originalWriteTime = version.write;
            });
        });
    } catch (const std::exception& error) {
        ++fileErrors;
        if (task->committed) {
            fileHits += task->result.hits.archive->Count(); ++foundFiles;
            if (owner->searchResults.generation == fileGeneration) owner->searchResults.AddFile(std::move(task->result.hits));
        }
        if (task->path && owner->searchResults.generation == fileGeneration)
            owner->searchResults.Skipped(*task->path, error.what());
        replaceWork.reset();
        auto pending = std::move(deferredOpens); deferredOpens.clear();
        for (const auto& request : pending) owner->Open(request.path, request.encoding);
    }
}
INT_PTR CALLBACK SearchDialog::Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* dialog = reinterpret_cast<SearchDialog*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            dialog = reinterpret_cast<SearchDialog*>(l); dialog->hwnd = window; SetWindowLongPtrW(window, DWLP_USER, l);
            const wchar_t* names[] = {L"Find", L"Replace", L"Find in Files", L"Mark"};
            const wchar_t* chineseNames[] = {L"查找", L"替换", L"文件中查找", L"标记"};
            for (int index = 0; index < 4; ++index) {
                const auto* name = dialog->Chinese() ? chineseNames[index] : names[index];
                TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(name);
                TabCtrl_InsertItem(GetDlgItem(window, IDC_SEARCH_TABS), TabCtrl_GetItemCount(GetDlgItem(window, IDC_SEARCH_TABS)), &item);
            }
            const auto& s = dialog->document->search;
            Fill(window, IDC_FINDTEXT, s.findHistory, s.findText); Fill(window, IDC_REPLACETEXT, s.replaceHistory, s.replaceText);
            auto check = [&](int id, bool value) { CheckDlgButton(window, id, value ? BST_CHECKED : BST_UNCHECKED); };
            check(IDC_FINDCASE, s.flags & SCFIND_MATCHCASE); check(IDC_FINDWORD, s.flags & SCFIND_WHOLEWORD);
            check(IDC_SEARCH_WRAP, s.wrap); check(IDC_FIND_DOTALL, s.dotMatchesNewline);
            CheckRadioButton(window, IDC_SEARCH_NORMAL, IDC_SEARCH_REGEX, s.regex ? IDC_SEARCH_REGEX : s.transformBackslashes ? IDC_SEARCH_EXTENDED : IDC_SEARCH_NORMAL);
            check(IDC_SEARCH_TRANSPARENT, s.transparent);
            dialog->transparentAlways = dialog->owner->settings.ReadString(L"Search", L"TransparentAlways") == L"1";
            const auto alpha = dialog->owner->settings.ReadString(L"Search", L"Opacity", L"180");
            dialog->opacity = std::clamp(_wtoi(alpha.c_str()), 50, 255);
            CheckRadioButton(window, IDC_SEARCH_TRANSPARENT_LOST, IDC_SEARCH_TRANSPARENT_ALWAYS,
                dialog->transparentAlways ? IDC_SEARCH_TRANSPARENT_ALWAYS : IDC_SEARCH_TRANSPARENT_LOST);
            SendDlgItemMessageW(window, IDC_SEARCH_OPACITY, TBM_SETRANGE, TRUE, MAKELPARAM(50, 255));
            SendDlgItemMessageW(window, IDC_SEARCH_OPACITY, TBM_SETPOS, TRUE, dialog->opacity);
            dialog->Localize();
            dialog->UpdateTransparency();
            check(IDC_FINDALLBOOKMARK, s.bookmarkMatches); check(IDC_SEARCH_PURGE, true); check(IDC_SEARCH_RECURSIVE, true);
            for (const auto* mask : {L"*.*", L"*.txt", L"*.cpp;*.h", L"*.csv"})
                SendDlgItemMessageW(window, IDC_SEARCH_FILTER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mask));
            SetDlgItemTextW(window, IDC_SEARCH_FILTER, L"*.*");
            auto directory = dialog->document->path.empty() ? std::filesystem::current_path() : dialog->document->path.parent_path();
            SendDlgItemMessageW(window, IDC_SEARCH_DIRECTORY, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(directory.c_str()));
            SetDlgItemTextW(window, IDC_SEARCH_DIRECTORY, directory.c_str());
            if (dialog->hasSavedPosition) SetWindowPos(window, nullptr, dialog->savedPosition.left, dialog->savedPosition.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            else CenterWindow(window, dialog->owner->m_hWnd);
            RECT client{}; GetClientRect(window, &client); dialog->layoutWidth = client.right; dialog->layoutHeight = client.bottom;
            dialog->controls.clear();
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
                RECT r{}; GetWindowRect(child, &r); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2);
                int id = GetDlgCtrlID(child);
                bool stretch = id == IDC_FINDTEXT || id == IDC_REPLACETEXT || id == IDC_SEARCH_DIRECTORY || id == IDC_SEARCH_FILTER || id == IDC_SEARCH_TABS || id == IDC_SEARCH_STATUS;
                bool right = id == IDC_SEARCH_SWAP || id == IDC_SEARCH_FILTER_HELP || id == IDC_SEARCH_BROWSE || id == IDC_SEARCH_CURRENT_DIRECTORY || (!stretch && r.left > client.right * 2 / 3);
                dialog->controls.push_back({child, r, right, stretch});
            }
            dialog->SetPage(dialog->page); return TRUE;
        }
        if (!dialog) return FALSE;
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) { dialog->Close(); return TRUE; }
        if (message == WM_NCDESTROY) { if (dialog->ordinaryTask) dialog->FinishSearch(true); dialog->hwnd = nullptr; dialog->document = nullptr; return FALSE; }
        if (message == ContinueOrdinarySearch) {
            if (dialog->ordinaryTask && dialog->ordinaryTask->token == w) {
                // Posted work must let queued input and paint run before continuing.
                if (HIWORD(GetQueueStatus(QS_INPUT | QS_PAINT))) dialog->ordinaryTask->yielding = true;
                else dialog->ContinueSearch();
            }
            return TRUE;
        }
        if (message == WM_TIMER && w == 3) {
            if (dialog->ordinaryTask && (dialog->ordinaryTask->mapped || dialog->ordinaryTask->yielding)) {
                dialog->ordinaryTask->yielding = false; dialog->ContinueSearch();
            }
            return TRUE;
        }
        if (message == WM_TIMER && w == 2) {
            auto found = std::find_if(dialog->owner->documents.begin(), dialog->owner->documents.end(), [&](const auto& d) { return d->searchIdentity == dialog->pendingIdentity; });
            if (found == dialog->owner->documents.end() || !(*found)->FastView()) { KillTimer(window, 2); return TRUE; }
            if (!(*found)->fastView->Busy()) {
                KillTimer(window, 2);
                if (dialog->document == found->get()) dialog->Status(files::FromUtf8((*found)->fastView->Status()));
            }
            return TRUE;
        }
        if (message == WM_TIMER && w == 1) { dialog->FileStep(); return TRUE; }
        if (message == WM_ACTIVATE) {
            dialog->active = LOWORD(w) != WA_INACTIVE;
            dialog->UpdateTransparency();
        }
        if (message == WM_HSCROLL && reinterpret_cast<HWND>(l) == GetDlgItem(window, IDC_SEARCH_OPACITY)) {
            dialog->opacity = static_cast<int>(SendDlgItemMessageW(window, IDC_SEARCH_OPACITY, TBM_GETPOS, 0, 0));
            dialog->UpdateTransparency();
            return TRUE;
        }
        if (message == WM_GETMINMAXINFO && dialog->layoutWidth) {
            RECT r{}, c{}, units{0, 0, 0, 147};
            GetWindowRect(window, &r); GetClientRect(window, &c); MapDialogRect(window, &units);
            auto* bounds = reinterpret_cast<MINMAXINFO*>(l);
            bounds->ptMinTrackSize = {dialog->layoutWidth + r.right - r.left - c.right,
                (dialog->expanded ? dialog->layoutHeight : units.bottom) + r.bottom - r.top - c.bottom};
            bounds->ptMaxTrackSize.y = bounds->ptMinTrackSize.y;
            return TRUE;
        }
        if (message == WM_SIZE && dialog->layoutWidth) { dialog->Layout(); return TRUE; }
        if (message == WM_NOTIFY) {
            const auto* n = reinterpret_cast<NMHDR*>(l);
            if (n->idFrom == IDC_SEARCH_SWAP && n->code == BCN_DROPDOWN) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, IDC_SEARCH_SWAP, dialog->Chinese() ? L"交换查找和替换内容" : L"Swap Find and Replace");
                AppendMenuW(menu, MF_STRING, IDC_SEARCH_COPY_FIND, dialog->Chinese() ? L"复制查找到替换" : L"Copy Find to Replace");
                AppendMenuW(menu, MF_STRING, IDC_SEARCH_COPY_REPLACE, dialog->Chinese() ? L"复制替换到查找" : L"Copy Replace to Find");
                RECT r{}; GetWindowRect(n->hwndFrom, &r);
                int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, r.left, r.bottom, 0, window, nullptr);
                DestroyMenu(menu);
                if (selected) SendMessageW(window, WM_COMMAND, selected, 0);
                return TRUE;
            }
            if (n->idFrom == IDC_SEARCH_TABS && n->code == TCN_SELCHANGE) { dialog->ReadFields(); dialog->SetPage(TabCtrl_GetCurSel(n->hwndFrom)); return TRUE; }
        }
        if (message != WM_COMMAND) return FALSE;
        int id = LOWORD(w);
        if (id == IDC_SEARCH_TRANSPARENT || id == IDC_SEARCH_TRANSPARENT_LOST || id == IDC_SEARCH_TRANSPARENT_ALWAYS) {
            dialog->transparentAlways = IsDlgButtonChecked(window, IDC_SEARCH_TRANSPARENT_ALWAYS) == BST_CHECKED;
            dialog->UpdateTransparency();
            return TRUE;
        }
        if (id == IDC_SEARCH_EXPAND) {
            dialog->ReadFields();
            dialog->expanded = !dialog->expanded;
            dialog->SetPage(dialog->page);
            return TRUE;
        }
        if (id == IDC_SEARCH_SWAP || id == IDC_SEARCH_COPY_FIND || id == IDC_SEARCH_COPY_REPLACE) {
            const auto find = Field(window, IDC_FINDTEXT), replacement = Field(window, IDC_REPLACETEXT);
            if (id != IDC_SEARCH_COPY_FIND) SetDlgItemTextW(window, IDC_FINDTEXT, replacement.c_str());
            if (id != IDC_SEARCH_COPY_REPLACE) SetDlgItemTextW(window, IDC_REPLACETEXT, find.c_str());
            return TRUE;
        }
        if (id == IDC_SEARCH_CURRENT_DIRECTORY) {
            auto* target = dialog->owner->Active();
            const auto path = target && !target->path.empty() ? target->path.parent_path() : std::filesystem::current_path();
            SetDlgItemTextW(window, IDC_SEARCH_DIRECTORY, path.c_str());
            return TRUE;
        }
        if (id == IDC_SEARCH_FILTER_HELP) {
            MessageBoxW(window, dialog->Chinese() ? L"使用 * 和 ? 匹配文件名。多个类型可用分号或空格分隔，例如 *.cpp;*.h。" :
                L"Use * and ? to match file names. Separate multiple patterns with semicolons or spaces, for example *.cpp;*.h.",
                dialog->Chinese() ? L"文件类型" : L"File filters", MB_OK | MB_ICONINFORMATION);
            return TRUE;
        }
        if (id == IDC_SEARCH_BROWSE) {
            IFileDialog* picker = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker)))) {
                picker->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
                if (SUCCEEDED(picker->Show(window))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(picker->GetResult(&item))) { PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { SetDlgItemTextW(window, IDC_SEARCH_DIRECTORY, path); CoTaskMemFree(path); } item->Release(); }
                } picker->Release();
            } return TRUE;
        }
        if (id == IDACC_FIND || id == IDACC_REPLACE) {
            dialog->ReadFields();
            dialog->SetPage(id == IDACC_REPLACE ? 1 : 0);
            SetFocus(GetDlgItem(window, IDC_FINDTEXT));
            return TRUE;
        }
        if (id == IDACC_FINDNEXT) id = IDOK;
        if (id == IDACC_FINDPREV) id = IDC_FINDPREV;
        if (id == IDACC_REPLACENEXT) id = IDC_REPLACE;
        switch (id) {
        case IDOK: case IDC_FINDPREV: case IDC_REPLACE: case IDC_REPLACEALL: case IDC_FINDALL:
        case IDC_SEARCH_COUNT: case IDC_SEARCH_ALL_OPEN: case IDC_SEARCH_REPLACE_OPEN:
        case IDC_SEARCH_MARK: case IDC_SEARCH_CLEAR_MARK: case IDC_SEARCH_FILES: case IDC_SEARCH_REPLACE_FILES:
            dialog->Execute(id); return TRUE;
        }
    } catch (const std::exception& error) {
        if (dialog) { if (dialog->ordinaryTask) dialog->FinishSearch(true); if (dialog->fileRunning) dialog->FinishFiles(true); dialog->Status(files::FromUtf8(error.what())); }
        return TRUE;
    }
    return FALSE;
}

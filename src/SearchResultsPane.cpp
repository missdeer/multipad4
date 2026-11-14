#include "SearchResultsPane.h"
#include "MainFrame.h"
void SearchResultsPane::CreatePane(MainFrame& frame) {
    owner = &frame; RECT rectangle{100, 100, 800, 340};
    Create(frame.m_hWnd, rectangle, L"Search results");
    dockwins::DFDOCKPOS position{};
    position.hdr.hWnd = m_hWnd; position.hdr.hBar = frame.m_hWnd;
    position.dwDockSide = dockwins::CDockingSide::sBottom;
    position.nWidth = MulDiv(210, GetDpiForWindow(frame.m_hWnd), 96); position.nHeight = 700;
    SetDockingPosition(&position); Hide();
}
LRESULT SearchResultsPane::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
    editor = CreateWindowExW(0, L"Scintilla", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
        0, 0, 1, 1, m_hWnd, nullptr, _Module.GetModuleInstance(), nullptr);
    if (!editor) return -1;
    SearchResultLexer::Configure(editor); handled = FALSE; return 0;
}
LRESULT SearchResultsPane::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) {
    RECT rectangle{}; GetClientRect(&rectangle); if (editor) ::MoveWindow(editor, 0, 0, rectangle.right, rectangle.bottom, TRUE);
    handled = FALSE; return 0;
}
LRESULT SearchResultsPane::OnClose(UINT, WPARAM, LPARAM, BOOL&) { Hide(); visible = false; return 0; }
void SearchResultsPane::Reveal() { Show(); visible = true; }
void SearchResultsPane::Clear() {
    ++generation;
    ::SendMessageW(editor, SCI_SETREADONLY, FALSE, 0); ::SendMessageW(editor, SCI_CLEARALL, 0, 0);
    ::SendMessageW(editor, SCI_SETREADONLY, TRUE, 0); targets.clear(); lines.clear(); archives.clear();
}
void SearchResultsPane::Append(const std::string& text, SearchResultLexer::Line style, Target target) {
    ::SendMessageW(editor, SCI_SETREADONLY, FALSE, 0);
    auto value = text + "\n";
    ::SendMessageW(editor, SCI_APPENDTEXT, value.size(), reinterpret_cast<LPARAM>(value.data()));
    ::SendMessageW(editor, SCI_SETREADONLY, TRUE, 0);
    lines.push_back(std::move(style)); targets.push_back(std::move(target));
    SearchResultLexer::Colourise(editor, lines, lines.size() - 1);
}
void SearchResultsPane::Begin(const std::wstring& query) {
    ++generation;
    header = lines.size();
    auto text = files::ToUtf8(query); std::replace(text.begin(), text.end(), '\r', ' '); std::replace(text.begin(), text.end(), '\n', ' ');
    Append("Search \"" + text + "\"", {SearchResultLexer::SearchHeader}, {}); Reveal();
}
void SearchResultsPane::Add(EditorDocument& document, const std::vector<SearchMatch>& matches) {
    if (matches.empty()) return;
    Add(document.path, document.searchIdentity, matches, document.Title());
}
void SearchResultsPane::Add(const std::filesystem::path& path, unsigned long long identity, const std::vector<SearchMatch>& matches, const std::wstring& title) {
    if (matches.empty()) return;
    Target target{identity, path, {}};
    Append("  " + files::ToUtf8(path.empty() ? title : path.wstring()) + " (" + std::to_string(matches.size()) + " hits)", {SearchResultLexer::FileHeader}, target);
    for (size_t i = 0; i < matches.size();) {
        const auto& match = matches[i]; target.match = match; target.matches.clear();
        auto lineText = (match.mappedSource || match.diskSource) ? match.preview : match.source->substr(match.lineStart,
            match.source->find_first_of("\r\n", match.lineStart) == std::string::npos ? std::string::npos :
            match.source->find_first_of("\r\n", match.lineStart) - match.lineStart);
        auto number = std::to_string(match.line + 1);
        std::string prefix = "\tLine " + number + ": ";
        SearchResultLexer::Line style; style.numberStart = 6; style.numberEnd = 6 + number.size();
        do {
            const auto& m = matches[i++]; target.matches.push_back(m);
            style.segments.emplace_back(prefix.size() + m.start - m.lineStart,
                prefix.size() + min<LRESULT>(m.end - m.lineStart, lineText.size()));
        } while (!match.mappedSource && i < matches.size() && matches[i].line == match.line && matches[i].lineStart == match.lineStart);
        Append(prefix + lineText, std::move(style), target);
    }
}
void SearchResultsPane::AddMapped(EditorDocument& document, large::Document::Snapshot snapshot, std::shared_ptr<large::HitArchive> hits) {
    archives.push_back({document.searchIdentity, document.path, std::move(snapshot), std::move(hits), 0});
    LoadMore(archives.size() - 1);
}
void SearchResultsPane::AddFile(files::FileSearchResult result, unsigned long long identity, unsigned long long revision) {
    if (!result.archive || !result.archive->Count()) return;
    MappedPage page{identity, result.path, *result.snapshot, result.archive};
    page.disk = std::move(result); page.revision = revision; page.file = true;
    archives.push_back(std::move(page)); LoadMore(archives.size() - 1);
}
bool SearchResultsPane::LoadMore(size_t archive) {
    if (archive >= archives.size()) return false;
    auto& page = archives[archive]; if (page.next >= page.hits->Count()) return false;
    if (page.file) {
        const auto dirty = lines.size();
        auto hits = page.hits->Page(page.next, 64);
        std::vector<SearchMatch> matches;
        for (const auto& hit : hits) {
            auto begin = (std::max)(hit.lineStart, hit.start > 256 ? hit.start - 256 : 0);
            auto preview = large::Document::Read(page.snapshot, begin, 2048);
            size_t skip = 0;
            while (skip < preview.size() && (static_cast<unsigned char>(preview[skip]) & 0xC0) == 0x80) ++skip;
            begin += skip; preview.erase(0, skip);
            if (begin + preview.size() < large::Document::Size(page.snapshot) && preview.size() > 4) {
                size_t end = preview.size() - 4;
                while (end && (static_cast<unsigned char>(preview[end]) & 0xC0) == 0x80) --end;
                preview.resize(end);
            }
            auto eol = preview.find_first_of("\r\n"); if (eol != std::string::npos) preview.resize(eol);
            SearchMatch match;
            match.start = static_cast<LRESULT>(hit.start); match.end = static_cast<LRESULT>(hit.end);
            match.line = static_cast<LRESULT>(hit.line); match.lineStart = static_cast<LRESULT>(begin);
            match.nativeLineStart = hit.lineStart; match.nativeStart = hit.start; match.nativeEnd = hit.end;
            match.diskSource = page.disk.snapshot; match.sourceRevision = page.revision;
            match.sourceSize = page.disk.originalSize; match.sourceWriteTime = page.disk.originalWriteTime;
            match.preview = std::move(preview); matches.push_back(std::move(match));
        }
        Add(page.path, page.identity, matches); page.next += hits.size();
        if (page.next < page.hits->Count()) {
            Target target; target.identity = page.identity; target.path = page.path; target.archive = archive;
            Append("  Load next results page (" + std::to_string(page.next) + " / " + std::to_string(page.hits->Count()) + " displayed)", {SearchResultLexer::Default}, std::move(target));
        }
        SearchResultLexer::Colourise(editor, lines, dirty); return true;
    }
    EditorDocument* document = nullptr;
    for (const auto& candidate : owner->documents) if (candidate->searchIdentity == page.identity) document = candidate.get();
    if (!document || !document->FastView() || document->fastView->Model().Capture().root != page.snapshot.root) return false;
    const auto dirty = lines.size();
    auto hits = page.hits->Page(page.next, (std::min)(size_t(64), page.hits->Count() - page.next));
    Add(*document, document->MappedMatches(page.snapshot, hits)); page.next += hits.size();
    if (page.next < page.hits->Count()) {
        Target target; target.identity = page.identity; target.path = page.path; target.archive = archive;
        Append("  Load next results page (" + std::to_string(page.next) + " / " + std::to_string(page.hits->Count()) + " displayed)", {SearchResultLexer::Default}, std::move(target));
    }
    SearchResultLexer::Colourise(editor, lines, dirty); return true;
}
void SearchResultsPane::Finish(size_t hits, size_t files) {
    auto start = ::SendMessageW(editor, SCI_POSITIONFROMLINE, header, 0);
    auto end = ::SendMessageW(editor, SCI_GETLINEENDPOSITION, header, 0);
    std::string suffix = " (" + std::to_string(hits) + " hits in " + std::to_string(files) + " files)";
    ::SendMessageW(editor, SCI_SETREADONLY, FALSE, 0);
    ::SendMessageW(editor, SCI_INSERTTEXT, end, reinterpret_cast<LPARAM>(suffix.c_str()));
    ::SendMessageW(editor, SCI_SETREADONLY, TRUE, 0);
    SearchResultLexer::Colourise(editor, lines, header, header + 1);
    ::SendMessageW(editor, SCI_GOTOPOS, start, 0); ::SendMessageW(editor, SCI_EMPTYUNDOBUFFER, 0, 0);
}
bool SearchResultsPane::Navigate(size_t line, size_t matchIndex) {
    if (line >= targets.size() || lines[line].style != SearchResultLexer::Default) return false;
    auto target = targets[line];
    if (target.archive != static_cast<size_t>(-1)) return LoadMore(target.archive);
    if (matchIndex < target.matches.size()) target.match = target.matches[matchIndex];
    EditorDocument* document = nullptr;
    for (const auto& d : owner->documents) if (d->searchIdentity == target.identity) document = d.get();
    if (!document && !target.path.empty()) {
        if (!owner->Open(target.path)) return false;
        document = owner->Active();
    }
    if (!document) return false;
    if (target.match.diskSource) {
        const auto& match = target.match;
        bool valid = target.identity ? document->style.revision == match.sourceRevision : !document->Modified();
        if (!target.identity) {
            std::error_code error;
            valid &= std::filesystem::file_size(target.path, error) == match.sourceSize && !error;
            try { valid &= files::GetTimes(target.path).write == match.sourceWriteTime; } catch (...) { valid = false; }
        }
        if (!valid) { ::MessageBoxW(m_hWnd, L"This result has changed. Run the search again.", L"Search results", MB_OK | MB_ICONINFORMATION); return false; }
        for (int i = 0; i < owner->tabs.GetPageCount(); ++i) if (owner->tabs.GetPageData(i) == document) owner->tabs.SetActivePage(i);
        if (document->FastView()) document->fastView->StartExternalMatch(*match.diskSource, match.line, match.nativeLineStart, match.nativeStart, match.nativeEnd);
        else {
            // Verify the displayed context before using UTF-8 coordinates from a disk snapshot.
            if (document->Range(match.lineStart, match.preview.size()) != match.preview) return false;
            document->Send(SCI_ENSUREVISIBLE, match.line); document->Send(SCI_SETSEL, match.nativeStart, match.nativeEnd); document->Send(SCI_SCROLLCARET);
        }
        ::SetFocus(document->hwnd); owner->Refresh(); return true;
    }
    if (target.match.mappedSource) {
        if (!document->FastView() || target.match.mappedSource->root != document->fastView->Model().Capture().root) {
            ::MessageBoxW(m_hWnd, L"This result has changed. Run the search again.", L"Search results", MB_OK | MB_ICONINFORMATION); return false;
        }
        for (int i = 0; i < owner->tabs.GetPageCount(); ++i) if (owner->tabs.GetPageData(i) == document) owner->tabs.SetActivePage(i);
        document->fastView->ShowSelection(target.match.nativeStart, target.match.nativeEnd - target.match.nativeStart);
        ::SetFocus(document->hwnd); owner->Refresh(); return true;
    }
    // Snapshots must not silently select unrelated text after editing or reload.
    auto lineStart = document->Send(SCI_POSITIONFROMLINE, target.match.line);
    auto lineEnd = document->Send(SCI_GETLINEENDPOSITION, target.match.line);
    if (lineStart < 0 || lineEnd < lineStart || lineEnd > document->Send(SCI_GETLENGTH) ||
        document->Range(lineStart, lineEnd - lineStart) != target.match.source->substr(target.match.lineStart,
            target.match.source->find_first_of("\r\n", target.match.lineStart) == std::string::npos ? std::string::npos :
            target.match.source->find_first_of("\r\n", target.match.lineStart) - target.match.lineStart)) {
        ::MessageBoxW(m_hWnd, L"This result has changed. Run the search again.", L"Search results", MB_OK | MB_ICONINFORMATION); return false;
    }
    auto start = lineStart + target.match.start - target.match.lineStart;
    auto end = min<LRESULT>(document->Send(SCI_GETLENGTH), start + target.match.end - target.match.start);
    if (document->Range(start, end - start) != target.match.source->substr(target.match.start, target.match.end - target.match.start)) return false;
    for (int i = 0; i < owner->tabs.GetPageCount(); ++i) if (owner->tabs.GetPageData(i) == document) owner->tabs.SetActivePage(i);
    document->Send(SCI_ENSUREVISIBLE, target.match.line); document->Send(SCI_SETSEL, start, end); document->Send(SCI_SCROLLCARET);
    ::SetFocus(document->hwnd); owner->Refresh(); return true;
}
BOOL SearchResultsPane::PreTranslateMessage(MSG* message) {
    if (message->hwnd == editor && message->message == WM_KEYDOWN && message->wParam == VK_RETURN) {
        Navigate(::SendMessageW(editor, SCI_LINEFROMPOSITION, ::SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0), 0)); return TRUE;
    } return FALSE;
}
LRESULT SearchResultsPane::OnNotify(UINT, WPARAM, LPARAM l, BOOL& handled) {
    auto* n = reinterpret_cast<SCNotification*>(l);
    if (n->nmhdr.hwndFrom != editor) { handled = FALSE; return 0; }
    if (n->nmhdr.code == SCN_DOUBLECLICK) {
        auto line = ::SendMessageW(editor, SCI_LINEFROMPOSITION, n->position, 0);
        size_t index = 0;
        if (line >= 0 && static_cast<size_t>(line) < lines.size()) {
            auto column = n->position - ::SendMessageW(editor, SCI_POSITIONFROMLINE, line, 0);
            const auto& segments = lines[line].segments;
            for (size_t i = 0; i < segments.size(); ++i) if (column >= segments[i].first && column <= segments[i].second) { index = i; break; }
        }
        if (!Navigate(line, index)) ::SendMessageW(editor, SCI_TOGGLEFOLD, line, 0);
    } else if (n->nmhdr.code == SCN_MARGINCLICK) ::SendMessageW(editor, SCI_TOGGLEFOLD, ::SendMessageW(editor, SCI_LINEFROMPOSITION, n->position, 0), 0);
    else if (n->nmhdr.code == SCN_STYLENEEDED) {
        auto first = ::SendMessageW(editor, SCI_LINEFROMPOSITION, ::SendMessageW(editor, SCI_GETENDSTYLED, 0, 0), 0);
        auto last = ::SendMessageW(editor, SCI_LINEFROMPOSITION, n->position, 0);
        SearchResultLexer::Colourise(editor, lines, first, last + 1);
    }
    return 0;
}
LRESULT SearchResultsPane::OnContext(UINT, WPARAM, LPARAM l, BOOL&) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Copy"); AppendMenuW(menu, MF_STRING, 2, L"Select all");
    AppendMenuW(menu, MF_STRING, 3, L"Collapse all"); AppendMenuW(menu, MF_STRING, 4, L"Expand all");
    if (!archives.empty()) AppendMenuW(menu, MF_STRING, 7, L"Load next results page");
    AppendMenuW(menu, MF_STRING, 5, L"Clear all"); AppendMenuW(menu, MF_STRING, 6, L"Hide");
    POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; if (point.x == -1) { point = {20,20}; ClientToScreen(&point); }
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, m_hWnd, nullptr); DestroyMenu(menu);
    if (command == 1) ::SendMessageW(editor, SCI_COPY, 0, 0);
    if (command == 2) ::SendMessageW(editor, SCI_SELECTALL, 0, 0);
    if (command == 3 || command == 4) ::SendMessageW(editor, SCI_FOLDALL, command == 3 ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND, 0);
    if (command == 7 && !archives.empty()) LoadMore(archives.size() - 1);
    if (command == 5) Clear(); if (command == 6) { Hide(); visible = false; } return 0;
}

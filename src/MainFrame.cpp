#include "MainFrame.h"
#include <shellapi.h>
#include "UpstreamUI.h"
#include "EditorDialogs.h"
#include "FileCommands.h"
#include "FileDialogs.h"
#include "StyleManager.h"
#include "FrameCommands.h"
#include "CommandLine.h"
#include "InfoDialog.h"
#include "Session.h"
BOOL MainFrame::PreTranslateMessage(MSG* msg) {
    if (pluginAdmin.hwnd && ::IsWindowVisible(pluginAdmin.hwnd) && ::IsDialogMessageW(pluginAdmin.hwnd, msg)) return TRUE;
    if (plugins.PreTranslate(msg)) return TRUE;
    if (searchResults.PreTranslateMessage(msg)) return TRUE;
    if (searchDialog.PreTranslateMessage(msg)) return TRUE;
    if (mdiAccelerators && ::TranslateAcceleratorW(m_hWnd, mdiAccelerators, msg)) return TRUE;
    return MainFrameBase::PreTranslateMessage(msg);
}
EditorDocument* MainFrame::Active() {
    int i = tabs.GetActivePage();
    return i < 0 ? nullptr : static_cast<EditorDocument*>(tabs.GetPageData(i));
}
void MainFrame::Error(const std::exception& error) {
    auto message = files::FromUtf8(error.what());
    ShowInfoDialog(m_hWnd, settings, message, L"MultiPad4", MB_OK | MB_ICONERROR);
}
LRESULT MainFrame::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    CreateUpstreamUI();
    HMENU searchMenu = GetSubMenu(GetMenu(), 2);
    AppendMenuW(searchMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(searchMenu, MF_STRING, ID_SEARCH_FILES, L"Find in Files...\tCtrl+Shift+F");
    AppendMenuW(searchMenu, MF_STRING, ID_SEARCH_MARK, L"Mark...\tCtrl+M");
    AppendMenuW(searchMenu, MF_STRING, ID_SEARCH_RESULTS, L"Search results\tF7");
    InitializeDockingFrame();
    SetTabOwnerParent(m_hWnd);
    CreateMDIClient(windowMenu);
    m_tabbedClient.HideMDITabsWhenMDIChildNotMaximized(FALSE);
    tabs.owner = m_hWnd; tabs.m_hWnd = m_hWndMDIClient; tabs.control = &GetMDITabCtrl();
    dock.Create(m_hWnd);
    searchResults.CreatePane(*this);
    InitializeFramePreferences(*this);
    UpdateLayout();
    DragAcceptFiles(TRUE);
    _Module.GetMessageLoop()->AddMessageFilter(this);
    SetTimer(1, 1000);
    AddClipboardFormatListener(m_hWnd);
    plugins.Initialize(*this);
    return 0;
}
EditorDocument* MainFrame::NewDocument() {
    try {
    auto document = std::make_unique<EditorDocument>();
    document->untitled = L"未命名 " + std::to_wstring(++untitledCount);
    if (!document->Create(tabs)) { MessageBox(L"无法创建 Scintilla 编辑器。", L"MultiPad4", MB_ICONERROR); return nullptr; }
    ApplyDocumentPreferences(*this, *document);
    document->encoding = static_cast<files::Encoding>(settings.Get(IDM_ENCODING_SETDEFAULT, static_cast<int>(files::Encoding::Ansi)));
    auto* raw = document.get();
    if (!tabs.AddPage(raw->ViewWindow(), raw->Title().c_str(), -1, raw)) { ::DestroyWindow(raw->hwnd); return nullptr; }
    documents.push_back(std::move(document)); Refresh();
    return raw;
    } catch (const std::exception& error) { Error(error); return nullptr; }
}
bool MainFrame::Open(const std::filesystem::path& input, std::optional<files::Encoding> forcedEncoding) {
    try {
        auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(input));
        for (int i = 0; i < tabs.GetPageCount(); ++i) {
            auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
            std::error_code ec;
            if (!d->path.empty() && (CompareStringOrdinal(d->path.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL
                || std::filesystem::equivalent(d->path, path, ec))) { tabs.SetActivePage(i); return true; }
        }

        auto document = std::make_unique<EditorDocument>(); document->path = path;
        if (!document->Create(tabs)) throw std::runtime_error("Cannot create Scintilla editor.");
        ApplyDocumentPreferences(*this, *document);
        if (!LoadLargeFileForFrame(*this, *document, path, forcedEncoding))
            document->Load(forcedEncoding ? files::ReadWithEncoding(path, *forcedEncoding) : ReadFileForFrame(*this, path));
        InitializeFileDocument(*this, *document);
        SnapshotDocument(*document);
        auto* raw = document.get();
        if (!tabs.AddPage(raw->ViewWindow(), raw->Title().c_str(), -1, raw)) {
            ::DestroyWindow(raw->hwnd); throw std::runtime_error("Cannot add document tab.");
        }
        documents.push_back(std::move(document)); RecordOpenedFile(*this, path); if (!raw->FastView()) plugins.Notify(NPPN_FILEOPENED, plugins.Buffer(raw)); Refresh(); return true;
    } catch (const std::exception& error) { Error(error); return false; }
}
bool MainFrame::Save(EditorDocument& document, SaveMode mode, bool keepBackup) {
    auto path = document.path;
    if (mode != SaveMode::Normal || path.empty()) {
        CFileDialog dialog(FALSE, L"txt", path.empty() ? nullptr : path.c_str(),
            OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR, L"所有文件 (*.*)\0*.*\0\0", m_hWnd);
        if (dialog.DoModal() != IDOK) return false;
        path = dialog.m_szFileName;
    }
    return SaveTo(document, path, mode == SaveMode::Copy, keepBackup);
}
bool MainFrame::SaveTo(EditorDocument& document, const std::filesystem::path& input, bool copy, bool keepBackup) {
    try {
        auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(input));
        for (auto& other : documents) {
            if ((!copy && other.get() == &document) || other->path.empty()) continue;
            std::error_code ec;
            if (CompareStringOrdinal(other->path.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL
                || std::filesystem::equivalent(other->path, path, ec))
                throw std::runtime_error("This file is already open in another tab.");
        }
        if (!copy) plugins.Notify(NPPN_FILEBEFORESAVE, plugins.Buffer(&document));
        if (document.FastView()) {
            document.fastView->Save(path, copy);
            if (!copy) {
                document.path = path; document.encodingDirty = false;
                SnapshotDocument(document); RemoveDocumentRecovery(*this, document, keepBackup); RecordOpenedFile(*this, path); Refresh();
            }
            return true;
        }
        PrepareFileSave(*this, document);
        if (document.LargeFile()) document.SaveStreaming(path);
        else {
            files::Text output;
            if (!PrepareEncodingSave(*this, document, output, copy)) return false;
            files::WriteAtomic(path, output);
        }
        if (copy) return true; // A copy must retain path, lexer, encoding and dirty state.
        document.path = path; document.ConfigureLexer(); document.Send(SCI_SETSAVEPOINT); document.encodingDirty = false;
        SnapshotDocument(document); RemoveDocumentRecovery(*this, document, keepBackup); RecordOpenedFile(*this, path); plugins.Notify(NPPN_FILESAVED, plugins.Buffer(&document)); Refresh(); return true;
    } catch (const std::exception& error) { Error(error); return false; }
}
bool MainFrame::Reload(EditorDocument& document) {
    if (document.path.empty()) return false;
    if (document.Modified() && MessageBox(L"Discard changes and reload this document?", document.Title().c_str(),
        MB_OKCANCEL | MB_ICONWARNING) != IDOK) return false;
    try {

        auto position = document.FastView() ? static_cast<LRESULT>(document.fastView->Caret()) : document.Send(SCI_GETCURRENTPOS);
        if (!LoadLargeFileForFrame(*this, document, document.path))
            document.Load(ReadFileForReload(*this, document));
        RestoreFileVariables(document);
        document.ConfigureLexer(); InitializeFileDocument(*this, document);
        RemoveDocumentRecovery(*this, document);
        if (document.FastView()) document.fastView->Seek((std::min)(static_cast<large::Position>(position), document.fastView->Size()));
        else document.Send(SCI_GOTOPOS, min(position, document.Send(SCI_GETLENGTH)));
        SnapshotDocument(document);
        Refresh(); return true;
    } catch (const std::exception& error) { Error(error); return false; }
}
bool MainFrame::Close(int i) {
    FrameOperation operation(runtime, m_hWnd);
    if (i < 0 || i >= tabs.GetPageCount()) return true;
    auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
    if (!ConfirmClose(*d)) return false;
    // A modal confirmation pumps messages; even a tab reorder invalidates i.
    for (i = 0; i < tabs.GetPageCount(); ++i) if (tabs.GetPageData(i) == d) break;
    if (i == tabs.GetPageCount()) return true;
    if (searchDialog.document == d) searchDialog.Close();
    if (settings.Get(IDM_VIEW_NOSAVEFINDREPL, 1)) {
        const auto& search = d->search;
        settings.WriteString(L"Search", L"Find", search.findText);
        settings.WriteString(L"Search", L"Replace", search.replaceText);
        settings.WriteString(L"Search", L"Flags", std::to_wstring(search.flags));
        int options = static_cast<int>(search.wrap) | (static_cast<int>(search.regex) << 1) | (static_cast<int>(search.wildcard) << 2)
            | (static_cast<int>(search.transformBackslashes) << 3) | (static_cast<int>(search.dotMatchesNewline) << 4) | (static_cast<int>(search.useBoostRegex) << 5)
            | (static_cast<int>(search.closeAfterAction) << 6) | (static_cast<int>(search.bookmarkMatches) << 7)
            | (static_cast<int>(search.useMonospaced) << 8) | (static_cast<int>(search.transparent) << 9)
            | (static_cast<int>(search.closeAfterFind) << 10) | (static_cast<int>(search.closeAfterReplace) << 11);
        settings.WriteString(L"Search", L"Options", std::to_wstring(options));
        if (searchDialog.hasSavedPosition) {
            const RECT& r = searchDialog.savedPosition;
            settings.WriteString(L"Search", L"Position", std::to_wstring(r.left) + L"," + std::to_wstring(r.top));
        }
        for (size_t j = 0; j < 32; ++j) {
            auto key = std::to_wstring(j);
            settings.WriteString(L"Find History", key.c_str(), j < search.findHistory.size() ? search.findHistory[j] : L"");
            settings.WriteString(L"Replace History", key.c_str(), j < search.replaceHistory.size() ? search.replaceHistory[j] : L"");
        }
    }
    if (runtime.pasteBoard == d->hwnd) runtime.pasteBoard = nullptr;
    RemoveDocumentRecovery(*this, *d, true); ForgetDocumentStyle(*d);
    const auto buffer = plugins.Buffer(d);
    const bool notifyPlugins = !d->FastView();
    if (notifyPlugins) plugins.Notify(NPPN_FILEBEFORECLOSE, buffer);
    tabs.RemovePage(i);
    std::erase_if(documents, [d](const auto& p) { return p.get() == d; });
    if (notifyPlugins) plugins.Notify(NPPN_FILECLOSED, buffer);
    Refresh(); return true;
}
bool MainFrame::ConfirmClose(EditorDocument& document, bool keepBackup) {
    if (!document.Modified()) return true;
    for (int i = 0; i < tabs.GetPageCount(); ++i) if (tabs.GetPageData(i) == &document) { tabs.SetActivePage(i); break; }
    auto question = L"Save changes to “" + (document.path.empty() ? document.untitled : document.path.filename().wstring()) + L"”?";
    int answer = MessageBox(question.c_str(), L"MultiPad4", MB_YESNOCANCEL | MB_ICONQUESTION);
    const bool confirmed = answer != IDCANCEL && (answer != IDYES || Save(document, SaveMode::Normal, keepBackup));
    // Cancel stops this close sequence, including child requests received by
    // its modal loop. External open requests retain their independent queue.
    if (!confirmed) runtime.pendingTabCloses.clear();
    return confirmed;
}
bool MainFrame::Relaunch(bool elevated, bool empty, bool closeCurrent) {
    auto* target = Active();
    if (!empty && (closeCurrent || settings.Get(IDM_VIEW_SAVEBEFORERUNNINGTOOLS))) {
        for (int i = 0; i < tabs.GetPageCount(); ++i) {
            auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
            if (!closeCurrent && d != target) continue;
            if (!d->Modified()) continue;
            tabs.SetActivePage(i);
            auto question = L"Save changes before restarting?\n" + d->Title();
            int answer = MessageBox(question.c_str(), L"MultiPad4", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL || (answer == IDYES && !Save(*d, SaveMode::Normal, closeCurrent))) return false;
        }
    }
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, _countof(executable));
    std::wstring parameters = L"/n +";
    std::filesystem::path session;
    if (!empty) {
        session = std::filesystem::temp_directory_path() / (L"MultiPad4-relaunch-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".ini");
        SaveRelaunchSession(*this, session, closeCurrent, target);
        parameters = L"/n /session \"" + session.wstring() + L"\"";
        if (settings.Get(IDM_VIEW_SAVESETTINGS, 1)) { SaveFramePreferences(*this); settings.Save(); }
    }
    auto result = ShellExecuteW(m_hWnd, elevated ? L"runas" : L"open", executable, parameters.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) { std::error_code error; if (!session.empty()) std::filesystem::remove(session, error); return false; }
    if (!empty && closeCurrent) {
        // Prompts were resolved before launching; no second prompt may cancel
        // after the replacement instance has already started.
        for (auto& d : documents) { d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; }
        PostMessage(WM_CLOSE);
    }
    return true;
}
void MainFrame::Refresh() {
    auto* active = Active();
    plugins.Activated();
    searchDialog.SyncDocument();
    std::vector<std::wstring> labels;
    for (int i = 0; i < tabs.GetPageCount(); ++i) {
        auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i)); auto title = d->Title();
        if (title != tabs.GetPageTitle(i)) tabs.SetPageTitle(i, title.c_str());
        labels.push_back(std::move(title));
    }
    bool changed = dock.pane.list.GetCount() != static_cast<int>(labels.size());
    for (int i = 0; !changed && i < static_cast<int>(labels.size()); ++i) {
        int n = dock.pane.list.GetTextLen(i); std::wstring old(static_cast<size_t>(n) + 1, L'\0');
        dock.pane.list.GetText(i, old.data()); old.resize(n); changed = old != labels[i];
    }
    if (changed) {
        dock.pane.list.SetRedraw(FALSE); dock.pane.list.ResetContent();
        for (const auto& label : labels) dock.pane.list.AddString(label.c_str());
        dock.pane.list.SetRedraw(TRUE); dock.pane.list.Invalidate();
    }
    dock.pane.list.SetCurSel(tabs.GetActivePage());
    std::wstring title = L"MultiPad4";
    if (!runtime.titleOverride.empty()) title = runtime.titleOverride + L" — MultiPad4";
    else if (active) {
        std::wstring name = active->Title();
        int format = settings.Get(IDM_VIEW_SHOWFILENAMEONLY);
        if (!active->path.empty() && format == 1) name += L" (" + active->path.parent_path().wstring() + L")";
        else if (!active->path.empty() && format == 2) name = (active->Modified() ? L"* " : L"") + active->path.wstring();
        if (settings.Get(IDM_VIEW_SHOWEXCERPT)) {
            auto length = min<LRESULT>(124, active->Send(SCI_GETLINEENDPOSITION, 0));
            std::string text(static_cast<size_t>(length) + 1, '\0');
            active->Send(SCI_GETTEXT, length, reinterpret_cast<LPARAM>(text.data())); text.resize(length);
            auto boundary = min<size_t>(120, text.size());
            while (boundary && boundary < text.size() && (static_cast<unsigned char>(text[boundary]) & 0xC0) == 0x80) --boundary;
            try { name += L" — " + files::FromUtf8(text.substr(0, boundary)); } catch (...) {}
        }
        title = name + L" — MultiPad4";
    }
    SetWindowText(title.c_str());
    UpdateStatus(); UpdateCommandUI();
}
void MainFrame::ShowSearch(bool replace) {
    if (searchDialog.hwnd) searchDialog.ReadFields();
    auto* d = Active();
    if (!d) d = NewDocument();
    if (d) {
        if (settings.Get(IDM_SET_SELECTIONASFINDTEXT, 1) && !d->Send(SCI_GETSELECTIONEMPTY)) {
            auto length = d->Send(SCI_GETSELTEXT);
            if (length > 0 && length < 32768) {
                std::string text(static_cast<size_t>(length) + 1, '\0'); d->Send(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(text.data())); text.resize(length);
                d->search.findText = files::FromUtf8(text);
            }
        } else if (settings.Get(IDM_SET_PASTEBUFFERASFINDTEXT) && ::OpenClipboard(m_hWnd)) {
            if (auto memory = GetClipboardData(CF_UNICODETEXT)) if (auto text = static_cast<const wchar_t*>(GlobalLock(memory))) {
                d->search.findText = text; GlobalUnlock(memory);
            }
            ::CloseClipboard();
        }
        searchDialog.Open(*this, *d, replace);
    }
}
void MainFrame::FindNext(bool backwards) {
    if (searchDialog.hwnd) {
        searchDialog.Execute(backwards ? IDC_FINDPREV : IDOK);
        return;
    }
    auto* d = Active(); if (!d) return;
    if (d->search.findText.empty()) { ShowSearch(false); return; }
    if (!d->FindNext(backwards)) {
        if (searchDialog.hwnd) searchDialog.Status(L"No further match found.");
        else ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"No further match found."));
    }
}
bool MainFrame::ConfirmSearchWrap(bool) {
    // Notepad++ wraps directly when Wrap around is enabled.
    return true;
}
LRESULT MainFrame::OnCommand(WORD, WORD input, HWND, BOOL&) {
    FrameOperation operation(runtime, m_hWnd);
    try {
    const UINT id = NormalizeCommand(input);
    if (!State(id).enabled) return 0;
    auto* d = Active();
    if (ExecuteFileCommand(*this, id) || ExecuteFrameCommand(*this, id)) { Refresh(); return 0; }
    if (d && (ExecuteStyleCommand(m_hWnd, *d, id) || ExecuteEditorCommand(*d, id, [&](bool previous) { return ConfirmSearchWrap(previous); }))) {
        RememberDocumentPreferences(*this, *d, id); Refresh(); return 0;
    }
    switch (id) {
    case ID_NEW: NewDocument(); break;
    case ID_OPEN: {
        wchar_t buffer[32768]{}; CFileDialog dialog(TRUE, nullptr, nullptr,
            OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR,
            L"源代码与文本 (*.*)\0*.*\0\0", m_hWnd);
        dialog.m_ofn.lpstrFile = buffer; dialog.m_ofn.nMaxFile = _countof(buffer);
        if (dialog.DoModal() == IDOK) {
            std::filesystem::path first = buffer; const wchar_t* p = buffer + wcslen(buffer) + 1;
            if (!*p) Open(first);
            else for (; *p; p += wcslen(p) + 1) Open(first / p);
        }
        break;
    }
    case ID_LOAD_FOR_EDITING: if (d && d->FastView()) { d->fastView->LoadForEditing(); plugins.Notify(NPPN_FILEOPENED, plugins.Buffer(d)); Refresh(); } break;
    case ID_SAVE: if (d) Save(*d); break;
    case ID_SAVEAS: if (d) Save(*d, SaveMode::As); break;
    case IDM_FILE_SAVECOPY: if (d) Save(*d, SaveMode::Copy); break;
    case IDM_FILE_REVERT: if (d) Reload(*d); break;
    case ID_SAVEALL: for (auto& doc : documents) if (doc->Modified() && !Save(*doc)) break; break;
    case ID_CLOSE: Close(tabs.GetActivePage()); break;
    case ID_CLOSEALL: while (tabs.GetPageCount()) if (!Close(tabs.GetPageCount() - 1)) break; break;
    case ID_EXIT: PostMessage(WM_CLOSE); break;
    case ID_DOCUMENTS: dock.Toggle(); break;
    case ID_DOCKLEFT: dock.SetSide(DockSide::Left); break;
    case ID_DOCKRIGHT: dock.SetSide(DockSide::Right); break;
    case ID_DOCKBOTTOM: dock.SetSide(DockSide::Bottom); break;
    case ID_FLOAT: dock.SetSide(DockSide::Floating); break;
    case ID_NEXTTAB: case ID_PREVTAB: {
        int n = tabs.GetPageCount();
        if (n) tabs.SetActivePage((tabs.GetActivePage() + n + (id == ID_NEXTTAB ? 1 : -1)) % n);
        break;
    }
    case ID_SEARCH_RESULTS: searchResults.Reveal(); break;
    case ID_SEARCH_FILES: ShowSearch(false); searchDialog.SetPage(2); break;
    case ID_SEARCH_MARK: ShowSearch(false); searchDialog.SetPage(3); break;
    case ID_FIND: ShowSearch(false); break;
    case IDM_EDIT_REPLACE: ShowSearch(true); break;
    case ID_FINDNEXT: FindNext(); break;
    case IDM_EDIT_FINDPREV: FindNext(true); break;
    case IDM_EDIT_GOTOLINE: if (d) ShowEditorDialog(m_hWnd, *d, IDD_LINENUM); break;
    case IDM_VIEW_ZOOM_LEVEL: if (d) ShowEditorDialog(m_hWnd, *d, IDD_ZOOMLEVEL); break;
    case IDM_VIEW_LONGLINESETTINGS:
        if (d && ShowEditorDialog(m_hWnd, *d, IDD_LONGLINES) == IDOK)
            RememberDocumentPreferences(*this, *d, id);
        break;
    case IDM_VIEW_TOOLBAR:
        toolbarVisible = !toolbarVisible; ::ShowWindow(m_hWndToolBar, toolbarVisible ? SW_SHOW : SW_HIDE); UpdateLayout(); break;
    case IDM_VIEW_STATUSBAR:
        statusVisible = !statusVisible; ::ShowWindow(m_hWndStatusBar, statusVisible ? SW_SHOW : SW_HIDE); UpdateLayout(); break;
    case IDM_VIEW_CUSTOMIZE_TOOLBAR: ::SendMessageW(m_hWndToolBar, TB_CUSTOMIZE, 0, 0); break;
    case IDM_VIEW_ALWAYSONTOP:
        SetWindowPos((GetExStyle() & WS_EX_TOPMOST) ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); break;
    case IDM_FILE_NEWWINDOW: Relaunch(false, true); break;
    case ID_ABOUT: ShowAboutDialog(m_hWnd); break;
    }
    Refresh(); return 0;
    } catch (const std::exception& error) { Error(error); return 0; }
}
LRESULT MainFrame::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
    FrameOperation operation(runtime, m_hWnd);
    plugins.Notify(NPPN_BEFORESHUTDOWN);
    if (settings.Get(IDM_VIEW_SAVESETTINGS, 1)) SaveFramePreferences(*this);
    while (tabs.GetPageCount()) if (!Close(tabs.GetPageCount() - 1)) { plugins.Notify(NPPN_CANCELSHUTDOWN); return 0; }
    if (settings.Get(IDM_VIEW_SAVESETTINGS, 1)) {
        settings.Save();
    }
    else settings.SaveOption(IDM_VIEW_SAVESETTINGS);
    pluginAdmin.Close();
    plugins.Shutdown();
    searchDialog.Close();
    if (dock.pane.IsWindow()) { if (dock.pane.IsDocking()) dock.pane.Undock(); dock.pane.DestroyWindow(); }
    DestroyWindow(); return 0;
}
LRESULT MainFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
    pluginAdmin.Close();
    plugins.Shutdown();
    RemoveClipboardFormatListener(m_hWnd);
    KillTimer(1); SetTrayVisible(*this, false);
    searchDialog.Close();
    if (toolbarDisabledImages) { ImageList_Destroy(toolbarDisabledImages); toolbarDisabledImages = nullptr; }
    if (toolbarImages) { ImageList_Destroy(toolbarImages); toolbarImages = nullptr; }
    _Module.GetMessageLoop()->RemoveMessageFilter(this); PostQuitMessage(0); handled = FALSE; return 0;
}
LRESULT MainFrame::OnDrop(UINT, WPARAM w, LPARAM, BOOL&) {
    HDROP drop = reinterpret_cast<HDROP>(w); UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        UINT size = DragQueryFileW(drop, i, nullptr, 0); std::wstring path(size + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), size + 1); path.resize(size); Open(path);
    }
    DragFinish(drop); return 0;
}
LRESULT MainFrame::OnChanged(UINT, WPARAM, LPARAM, BOOL&) { Refresh(); return 0; }
LRESULT MainFrame::OnCloseTab(UINT, WPARAM w, LPARAM, BOOL&) {
    const HWND child = reinterpret_cast<HWND>(w);
    if (runtime.operationDepth || runtime.inBackgroundWork || !IsWindowEnabled()) {
        if (std::find(runtime.pendingTabCloses.begin(), runtime.pendingTabCloses.end(), child) == runtime.pendingTabCloses.end())
            runtime.pendingTabCloses.push_back(child);
        return 0;
    }
    for (int i = 0; i < tabs.GetPageCount(); ++i)
        if (tabs.GetPageHWND(i) == child) { Close(i); break; }
    return 0;
}
LRESULT MainFrame::OnPane(UINT, WPARAM action, LPARAM index, BOOL&) {
    switch(action) {
    case 0: dock.Toggle(); break;
    case 1: dock.SetSide(DockSide::Left); break;
    case 2: dock.SetSide(DockSide::Right); break;
    case 3: dock.SetSide(DockSide::Bottom); break;
    case 4: dock.SetSide(dock.side == DockSide::Floating ? DockSide::Left : DockSide::Floating); break;
    case 10: if (index >= 0 && index < tabs.GetPageCount()) tabs.SetActivePage(static_cast<int>(index)); break;
    }
    return 0;
}
LRESULT MainFrame::OnMenu(UINT, WPARAM, LPARAM, BOOL&) {
    UpdateCommandUI(); return 0;
}
LRESULT MainFrame::OnDpi(UINT, WPARAM, LPARAM l, BOOL&) {
    auto* r = reinterpret_cast<RECT*>(l);
    SetWindowPos(nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
    for (auto& d : documents) if (d->Send(SCI_GETMARGINWIDTHN, 0)) d->Send(SCI_SETMARGINWIDTHN, 0, MulDiv(54, GetDpiForWindow(m_hWnd), 96));
    UpdateToolbarImages(*this);
    dock.Layout(); return 0;
}

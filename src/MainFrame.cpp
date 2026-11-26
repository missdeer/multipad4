#include "MainFrame.h"
#include "../plugins/PluginApi.h"
#include <shellapi.h>
#include "UpstreamUI.h"
#include "EditorDialogs.h"
#include "FileCommands.h"
#include "FileDialogs.h"
#include "StyleManager.h"
#include "ThemeDialog.h"
#include "LanguageConfiguration.h"
#include "FrameCommands.h"
#include "CommandLine.h"
#include "InfoDialog.h"
#include "Session.h"
#include "AutoSave.h"

HWND MainFrame::Create(const RECT& position, HINSTANCE instance) {
    constexpr auto name = L"MultiPad4.MainFrame";
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = WindowProcedure;
    type.hInstance = instance;
    type.lpszClassName = name;
    type.lpszClassName = name;
    type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDR_MAINFRAME));
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_APPWORKSPACE);
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    const HMENU menu = LoadMenuW(instance, MAKEINTRESOURCEW(IDR_MAINFRAME));
    if (!menu) return nullptr;
    const HWND window = CreateWindowExW(0, name, L"MultiPad4",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, position.left, position.top,
        position.right - position.left, position.bottom - position.top, nullptr, menu, instance, this);
    if (!window && ::IsMenu(menu)) DestroyMenu(menu);
    return window;
}

LRESULT CALLBACK MainFrame::WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* frame = reinterpret_cast<MainFrame*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        frame = static_cast<MainFrame*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        if (frame->settings.portable && !SetPropW(window, PortableWindowProperty, window)) return FALSE;
        frame->m_hWnd = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(frame));
    }
    if (!frame) return DefWindowProcW(window, message, w, l);
    BOOL handled = TRUE;
    const auto result = frame->Dispatch(message, w, l, handled);
    if (handled) return result;
    if (message == WM_SIZE) {
        if (w != SIZE_MINIMIZED) frame->UpdateLayout();
        return 0;
    }
    const auto defaultResult = DefFrameProcW(window, frame->m_hWndMDIClient, message, w, l);
    if (message == WM_NCDESTROY) {
        if (frame->settings.portable) RemovePropW(window, PortableWindowProperty);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        frame->m_hWnd = nullptr;
    }
    return defaultResult;
}

HWND MainFrame::MDIGetActive(BOOL* maximized) const {
    return reinterpret_cast<HWND>(::SendMessageW(m_hWndMDIClient, WM_MDIGETACTIVE, 0, reinterpret_cast<LPARAM>(maximized)));
}
void MainFrame::MDIMaximize(HWND child) {
    ::SendMessageW(m_hWndMDIClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
}
void MainFrame::MDIRestore(HWND child) {
    ::SendMessageW(m_hWndMDIClient, WM_MDIRESTORE, reinterpret_cast<WPARAM>(child), 0);
}
LRESULT MainFrame::Dispatch(UINT message, WPARAM w, LPARAM l, BOOL& handled) {
    if (message >= NPPMSG && message <= NPPMSG + 999) return OnPluginMessage(message, w, l, handled);
    if (message >= RUNCOMMAND_USER && message <= RUNCOMMAND_USER + 100) return OnPluginMessage(message, w, l, handled);
    switch (message) {
    case WM_APP + 0x120: return OnPluginNotify(message, w, l, handled);
    case WM_CREATE: return OnCreate(message, w, l, handled);
    case WM_CLOSE: return OnClose(message, w, l, handled);
    case WM_DESTROY: return OnDestroy(message, w, l, handled);
    case WM_DROPFILES: return OnDrop(message, w, l, handled);
    case WM_DOCUMENT_CHANGED: return OnChanged(message, w, l, handled);
    case WM_CLOSE_TAB: return OnCloseTab(message, w, l, handled);
    case WM_TAB_CONTEXT: return OnTabContextMenu(message, w, l, handled);
    case WM_TAB_ACTIVATED: return OnTabActivated(message, w, l, handled);
    case WM_DOCK_LAYOUT: return OnDockLayout(message, w, l, handled);
    case WM_INITMENUPOPUP: return OnMenu(message, w, l, handled);
    case WM_DPICHANGED: return OnDpi(message, w, l, handled);
    case WM_SIZE: return OnSize(message, w, l, handled);
    case WM_NOTIFY: return OnNotify(message, w, l, handled);
    case WM_TIMER: return OnTimer(message, w, l, handled);
    case WM_FILE_CHANGE: return OnFileChange(message, w, l, handled);
    case WM_ENABLE: return OnWatchEnable(message, w, l, handled);
    case WM_ACTIVATEAPP: return OnActivateApp(message, w, l, handled);
    case WM_POWERBROADCAST: return OnPower(message, w, l, handled);
    case WM_QUERYENDSESSION: return OnQueryEndSession(message, w, l, handled);
    case WM_ENDSESSION: return OnEndSession(message, w, l, handled);
    case WM_APP + 0x103: return OnTray(message, w, l, handled);
    case WM_APP + 0x104: return OnDeferredCommand(message, w, l, handled);
    case WM_CLIPBOARDUPDATE: return OnClipboardUpdate(message, w, l, handled);
    case WM_COPYDATA: return OnCopyData(message, w, l, handled);
    case WM_CONTEXTMENU: return OnContextMenu(message, w, l, handled);
    case WM_MENUSELECT: return OnMenuSelect(message, w, l, handled);
    case WM_COMMAND: {
        const WORD id = LOWORD(w), code = HIWORD(w);
        const auto control = reinterpret_cast<HWND>(l);
        if (id == IDM_SETTING_PLUGINADM) return OnPluginAdmin(code, id, control, handled);
        if (id == IDM_SETTING_OPENPLUGINSDIR) return OnPluginAdmin(code, id, control, handled);
        if (id >= 46000 && id <= 55999) return OnPluginCommand(code, id, control, handled);
        if (id == ID_WINDOW_CASCADE) return OnArrangeMDI(code, id, control, handled);
        if (id == ID_WINDOW_TILE_HORZ) return OnArrangeMDI(code, id, control, handled);
        if (id == ID_WINDOW_TILE_VERT) return OnArrangeMDI(code, id, control, handled);
        if (id == ID_WINDOW_ARRANGE) return OnArrangeMDI(code, id, control, handled);
        if (id >= 40000 && id <= 45999) return OnCommand(code, id, control, handled);
        break;
    }
    }
    handled = FALSE;
    return 0;
}

BOOL MainFrame::PreTranslateMessage(MSG* msg) {
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE) {
        if (HWND pane = GetCapture(); pane && GetPropW(pane, dockPlacementProperty)) {
            // Undocking during a drag can move keyboard focus away from the pane.
            msg->hwnd = pane;
            return FALSE;
        }
    }
    if (pluginAdmin.hwnd && ::IsWindowVisible(pluginAdmin.hwnd) && ::IsDialogMessageW(pluginAdmin.hwnd, msg)) return TRUE;
    if (plugins.PreTranslate(msg)) return TRUE;
    if (searchResults.PreTranslateMessage(msg)) return TRUE;
    if (searchDialog.PreTranslateMessage(msg)) return TRUE;
    if (mdiAccelerators && ::TranslateAcceleratorW(m_hWnd, mdiAccelerators, msg)) return TRUE;
    if (editorAccelerators && ::TranslateAcceleratorW(m_hWnd, editorAccelerators, msg)) return TRUE;
    return TranslateMDISysAccel(m_hWndMDIClient, msg);
}
void MainFrame::UpdateLayout(BOOL resizeBars) {
    RECT area{};
    GetClientRect(&area);
    auto barHeight = [resizeBars](HWND bar) -> LONG {
        if (!bar || !(GetWindowLongPtrW(bar, GWL_STYLE) & WS_VISIBLE)) return 0;
        if (resizeBars) ::SendMessageW(bar, WM_SIZE, 0, 0);
        RECT rect{};
        ::GetWindowRect(bar, &rect);
        return rect.bottom - rect.top;
    };
    area.top = std::min(area.bottom, barHeight(m_hWndToolBar));
    area.bottom = std::max(area.top, area.bottom - barHeight(m_hWndStatusBar));
    LayoutDockPanes(m_hWnd, area);
    if (tabStrip.LayoutWindow())
        ::SetWindowPos(tabStrip.LayoutWindow(), nullptr, area.left, area.top, area.right - area.left, area.bottom - area.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
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
    editorAccelerators = LoadAcceleratorsW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_MAINWND));
    HMENU searchMenu = GetSubMenu(GetMenu(), 2);
    AppendMenuW(searchMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(searchMenu, MF_STRING, ID_SEARCH_FILES, L"Find in Files...\tCtrl+Shift+F");
    AppendMenuW(searchMenu, MF_STRING, ID_SEARCH_MARK, L"Mark...\tCtrl+M");
    AppendMenuW(searchMenu, MF_STRING, ID_SEARCH_RESULTS, L"Search results\tF7");
    auto schemeMenu = GetSubMenu(GetMenu(), 3);
    AppendMenuW(schemeMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(schemeMenu, MF_STRING, ID_RELOAD_CONFIGURATION, L"Reload language configuration");
    // The localized main menu places Window immediately before Help.
    windowMenu = GetSubMenu(GetMenu(), GetMenuItemCount(GetMenu()) - 2);
    CLIENTCREATESTRUCT client{windowMenu, 0xFF00};
    m_hWndMDIClient = CreateWindowExW(0, L"MDICLIENT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | MDIS_ALLCHILDSTYLES,
        0, 0, 0, 0, m_hWnd, nullptr, GetModuleHandleW(nullptr), &client);
    if (!m_hWndMDIClient || !tabStrip.Create(m_hWnd, m_hWndMDIClient)) return -1;
    tabs.owner = m_hWnd; tabs.m_hWnd = m_hWndMDIClient; tabs.control = &tabStrip;
    UpdateLayout();
    searchResults.CreatePane(*this);
    InitializeFramePreferences(*this);
    ApplyTabTitleFont();
    ApplyTabWidth();
    DragAcceptFiles(TRUE);
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
    ApplyDefaultDocumentTheme(*document);document->ConfigureLexer();
    document->encoding = static_cast<files::Encoding>(settings.Get(IDM_ENCODING_SETDEFAULT, static_cast<int>(files::Encoding::Ansi)));
        auto* raw = AddDocument(std::move(document));
        Refresh();
    return raw;
    } catch (const std::exception& error) { Error(error); return nullptr; }
}
EditorDocument* MainFrame::AddDocument(std::unique_ptr<EditorDocument> document) {
    auto* raw = document.get();
    if (documents.size() == documents.capacity()) documents.reserve(documents.size() * 2 + 1);
    if (!tabs.AddPage(raw->ViewWindow(), raw->Title().c_str(), -1, raw))
        throw std::runtime_error("Cannot add document tab.");
    documents.push_back(std::move(document));
    return raw;
}
bool MainFrame::Open(const std::filesystem::path& input, std::optional<files::Encoding> forcedEncoding) {
    if (searchDialog.DeferOpen(input, forcedEncoding)) return false;
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
        auto* raw = AddDocument(std::move(document));
        RecordOpenedFile(*this, path); if (!raw->FastView()) plugins.Notify(NPPN_FILEOPENED, plugins.Buffer(raw)); Refresh(); return true;
    } catch (const std::exception& error) { Error(error); return false; }
}
bool MainFrame::Save(EditorDocument& document, SaveMode mode, bool keepBackup) {
    auto path = document.path;
    if (mode != SaveMode::Normal || path.empty()) {
        wchar_t file[32768]{};
        if (path.native().size() >= _countof(file)) return false;
        wcscpy_s(file, path.c_str());
        auto dialog = FileDialogOptions(m_hWnd, file, _countof(file),
            OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR, L"所有文件 (*.*)\0*.*\0\0", L"txt");
        if (!GetSaveFileNameW(&dialog)) return false;
        path = file;
    }
    return SaveTo(document, path, mode == SaveMode::Copy, keepBackup);
}
bool MainFrame::SaveTo(EditorDocument& document, const std::filesystem::path& input, bool copy, bool keepBackup) {
    if (searchDialog.CommittingReplacement()) {
        ::MessageBoxW(m_hWnd, L"A file replacement is committing. Please retry saving shortly.", L"Replace in Files", MB_OK | MB_ICONINFORMATION); return false;
    }
    CancelDocumentBackup(*this, document);
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
        if (document.LargeFile() && (document.encoding == files::Encoding::Utf8 || document.encoding == files::Encoding::Utf8Bom
            || document.encoding == files::Encoding::Utf16LE || document.encoding == files::Encoding::Utf16BE
            || document.encoding == files::Encoding::Utf16LEPlain || document.encoding == files::Encoding::Utf16BEPlain)) document.SaveStreaming(path);
        else {
            std::string output;
            if (!PrepareEncodingSave(*this, document, output, copy)) return false;
            files::WriteAtomicChunks(path, [&](const files::ChunkConsumer& consume) { consume(output); });
        }
        if (copy) return true; // A copy must retain path, lexer, encoding and dirty state.
        document.path = path; document.ConfigureLexer(); document.Send(SCI_SETSAVEPOINT); document.encodingDirty = false;
        SnapshotDocument(document); RemoveDocumentRecovery(*this, document, keepBackup); RecordOpenedFile(*this, path); plugins.Notify(NPPN_FILESAVED, plugins.Buffer(&document)); Refresh(); return true;
    } catch (const std::exception& error) { Error(error); return false; }
}
bool MainFrame::Reload(EditorDocument& document) {
    CancelDocumentBackup(*this, document);
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
bool MainFrame::Close(int i, bool confirm) {
    FrameOperation operation(runtime, m_hWnd);
    if (i < 0 || i >= tabs.GetPageCount()) return true;
    auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
    if (confirm && !ConfirmClose(*d)) return false;
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
void MainFrame::CloseTabs(int anchor, UINT command) {
    const int count = tabs.GetPageCount();
    if (anchor < 0 || anchor >= count) return;
    int first = 0, last = count;
    switch (command) {
    case ID_CLOSE: Close(anchor); return;
    case ID_CLOSE_LEFT_TABS: last = anchor; break;
    case ID_CLOSE_RIGHT_TABS: first = anchor + 1; break;
    case ID_CLOSE_OTHER_TABS: case ID_CLOSEALL: break;
    default: return;
    }

    // A save prompt can pump messages that reorder the remaining tabs.
    std::vector<unsigned long long> targets;
    for (int i = last - 1; i >= first; --i) {
        if (command == ID_CLOSE_OTHER_TABS && i == anchor) continue;
        targets.push_back(tabs.Child(i)->document->searchIdentity);
    }
    for (auto identity : targets) {
        for (int i = tabs.GetPageCount() - 1; i >= 0; --i) {
            if (tabs.Child(i)->document->searchIdentity != identity) continue;
            if (!Close(i)) return;
            break;
        }
    }
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
        exitSessionEligible = false;
        PostMessage(WM_CLOSE);
    }
    return true;
}
void MainFrame::Refresh() {
    auto* active = Active();
    plugins.Activated();
    searchDialog.SyncDocument();
    const bool structural = publishedStructure != tabStrip.structureRevision;
    bool titlesChanged = structural;
    if (structural) {
        publishedDocuments.clear(); publishedDocuments.reserve(tabs.GetPageCount());
        for (int i = 0; i < tabs.GetPageCount(); ++i) {
            auto* d = static_cast<EditorDocument*>(tabs.GetPageData(i));
            d->publishedTabIndex = static_cast<size_t>(i);
            publishedDocuments.emplace_back(d->searchIdentity, d->publishedTitle);
        }
        publishedStructure = tabStrip.structureRevision;
    }
    for (const auto& document : documents) {
        auto& d = *document;
        if (d.publishedTabIndex >= publishedDocuments.size() || publishedDocuments[d.publishedTabIndex].first != d.searchIdentity) continue;
        if (!d.titleDirty && &d != active && d.encodingDirty == d.publishedEncodingDirty &&
            (!d.FastView() || d.Modified() == d.publishedModified) && d.path == d.publishedPath && d.untitled == d.publishedUntitled) continue;
        const auto title = d.Title();
        if (title != d.publishedTitle) {
            d.publishedTitle = title;
            publishedDocuments[d.publishedTabIndex].second = title;
            tabs.SetPageTitle(static_cast<int>(d.publishedTabIndex), title.c_str());
            titlesChanged = true;
        }
        d.publishedPath = d.path; d.publishedUntitled = d.untitled;
        d.publishedModified = d.Modified(); d.publishedEncodingDirty = d.encodingDirty; d.titleDirty = false;
    }
    const auto currentActive = active ? active->searchIdentity : 0;
    const bool activeChanged = currentActive != publishedActive;
    publishedActive = currentActive;
    if (titlesChanged) {
        if (plugins.ready) plugins.Notify(MPN_DOCUMENTSCHANGED);
    } else if (activeChanged && plugins.ready) plugins.Notify(MPN_DOCUMENTSCHANGED, currentActive);
    UpdateCaption();
    UpdateStatus(); UpdateMenuCommands(*this, runtime.mainMenu ? runtime.mainMenu : GetMenu()); UpdateCommandUI();
}
void MainFrame::UpdateCaption() {
    auto* active = Active();
    std::wstring title = L"MultiPad4";
    if (!runtime.titleOverride.empty()) title = runtime.titleOverride + L" — MultiPad4";
    else if (active) {
        std::wstring name = active->Title();
        int format = settings.Get(IDM_VIEW_SHOWFILENAMEONLY);
        if (!active->path.empty() && format == 1) name += L" (" + active->path.parent_path().wstring() + L")";
        else if (!active->path.empty() && format == 2) name = (active->Modified() ? L"* " : L"") + active->path.wstring();
        if (settings.Get(IDM_VIEW_SHOWEXCERPT)) {
            if (captionIdentity != active->searchIdentity || captionExcerptRevision != active->excerptRevision) {
                auto length = min<LRESULT>(124, active->Send(SCI_GETLINEENDPOSITION, 0));
                std::string text(static_cast<size_t>(length) + 1, '\0');
                active->Send(SCI_GETTEXT, length, reinterpret_cast<LPARAM>(text.data())); text.resize(length);
                auto boundary = min<size_t>(120, text.size());
                while (boundary && boundary < text.size() && (static_cast<unsigned char>(text[boundary]) & 0xC0) == 0x80) --boundary;
                captionExcerpt.clear();
                try { captionExcerpt = L" — " + files::FromUtf8(text.substr(0, boundary)); } catch (...) {}
                captionIdentity = active->searchIdentity; captionExcerptRevision = active->excerptRevision;
            }
            name += captionExcerpt;
        }
        title = name + L" — MultiPad4";
    }
    if (title != publishedCaption) { publishedCaption = title; SetWindowTextW(m_hWnd, title.c_str()); }
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
    if (id == IDM_VIEW_SCHEME_CONFIG && d) {
        if (ShowThemeDialog(m_hWnd, *d) && ThemeScopeAll()) {
            for (auto& document : documents) {
                if (document.get() == d) continue;
                ApplyDefaultDocumentTheme(*document);
                document->ConfigureLexer();
            }
        }
        Refresh();
        return 0;
    }
    if(id==ID_RELOAD_CONFIGURATION){if(!ReloadLanguageConfiguration())throw std::runtime_error("Language reload rejected; previous snapshot retained. See configuration diagnostics.");for(auto& document:documents)document->ConfigureLexer();Refresh();return 0;}
    if (d && (ExecuteStyleCommand(m_hWnd, *d, id) || ExecuteEditorCommand(*d, id, [&](bool previous) { return ConfirmSearchWrap(previous); }))) {
        RememberDocumentPreferences(*this, *d, id); Refresh(); return 0;
    }
    switch (id) {
    case ID_NEW:
        if (auto* created = NewDocument()) ::SetFocus(created->hwnd);
        break;
    case ID_OPEN: {
        wchar_t buffer[32768]{};
        auto dialog = FileDialogOptions(m_hWnd, buffer, _countof(buffer),
            OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR, L"源代码与文本 (*.*)\0*.*\0\0");
        if (GetOpenFileNameW(&dialog)) {
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
    case ID_CHECK_UPDATES: appUpdater.Check(true); break;
    case ID_AUTO_CHECK_UPDATES:
        settings.Set(id, !settings.Get(id, 1));
        settings.SaveOption(id);
        break;
    }
    Refresh(); return 0;
    } catch (const std::exception& error) { Error(error); return 0; }
}
LRESULT MainFrame::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
    if (runtime.operationDepth || runtime.inBackgroundWork) return 0;
    FrameOperation operation(runtime, m_hWnd);
    plugins.Notify(NPPN_BEFORESHUTDOWN);
    bool savedSession = false;
    try {
        if (appUpdater.ready) {
            const auto snapshot = UpdateSessionPath(*this, appUpdater.ready->directory);
            std::filesystem::create_directories(snapshot.parent_path());
            SaveSessionSnapshot(*this, snapshot);
            savedSession = true;
        } else {
            savedSession = SaveExitSession(*this, pendingPluginMaintenance ?
                ExitSessionContents::SavedOnly : ExitSessionContents::IncludeUnsaved);
        }
    } catch (const std::exception& error) {
        pendingPluginMaintenance.reset();
        appUpdater.ready.reset();
        Error(std::runtime_error(std::string("Cannot save the session. ") + error.what()));
        runtime.pendingTabCloses.clear();
        plugins.Notify(NPPN_CANCELSHUTDOWN);
        return 0;
    }
    if (settings.Get(IDM_VIEW_SAVESETTINGS, 1)) SaveFramePreferences(*this);
    while (tabs.GetPageCount()) if (!Close(tabs.GetPageCount() - 1, !savedSession && !pendingPluginMaintenance)) {
        appUpdater.ready.reset();
        plugins.Notify(NPPN_CANCELSHUTDOWN);
        return 0;
    }
    if (settings.Get(IDM_VIEW_SAVESETTINGS, 1)) {
        settings.Save();
    }
    else settings.SaveOption(IDM_VIEW_SAVESETTINGS);
    pluginAdmin.Close();
    plugins.Shutdown();
    searchDialog.Close();
    DestroyWindow(); return 0;
}
LRESULT MainFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
    for (auto& document : documents) document->runtime.directoryWatch.reset();
    pluginAdmin.Close();
    plugins.Shutdown();
    RemoveClipboardFormatListener(m_hWnd);
    KillTimer(1); SetTrayVisible(*this, false);
    searchDialog.Close();
    if (toolbarDisabledImages) { ImageList_Destroy(toolbarDisabledImages); toolbarDisabledImages = nullptr; }
    if (toolbarImages) { ImageList_Destroy(toolbarImages); toolbarImages = nullptr; }
    PostQuitMessage(0); handled = FALSE; return 0;
}
LRESULT MainFrame::OnDrop(UINT, WPARAM w, LPARAM, BOOL&) {
    HDROP drop = reinterpret_cast<HDROP>(w); UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        UINT size = DragQueryFileW(drop, i, nullptr, 0); std::wstring path(size + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), size + 1); path.resize(size); Open(path);
    }
    DragFinish(drop); return 0;
}
LRESULT MainFrame::OnChanged(UINT, WPARAM lightweight, LPARAM, BOOL&) {
    MSG message{};
    // One queued update represents all notifications already waiting for this frame.
    while (::PeekMessageW(&message, m_hWnd, WM_DOCUMENT_CHANGED, WM_DOCUMENT_CHANGED, PM_REMOVE))
        lightweight = lightweight && message.wParam;
    if (!lightweight) Refresh();
    else {
        if (settings.Get(IDM_VIEW_SHOWEXCERPT)) {
            UpdateCaption(); UpdateMenuCommands(*this, runtime.mainMenu ? runtime.mainMenu : GetMenu());
        }
        UpdateStatus(); UpdateCommandUI();
    }
    if (!runtime.workTimerArmed && std::any_of(documents.begin(), documents.end(), [](const auto& d) { return d->style.occurrencePending; })) {
        SetTimer(3, 30); runtime.workTimerArmed = true;
    }
    return 0;
}
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
LRESULT MainFrame::OnMenu(UINT, WPARAM menu, LPARAM, BOOL&) {
    const auto popup = reinterpret_cast<HMENU>(menu);
    UpdateMenuCommands(*this, popup);
    plugins.UpdateMenu(popup);
    UpdateCommandUI(); return 0;
}
LRESULT MainFrame::OnPluginCommand(WORD, WORD id, HWND, BOOL&) {
    try { plugins.Execute(id); }
    catch (const std::exception& error) { Error(error); }
    return 0;
}
void MainFrame::ApplyTabTitleFont() {
    const auto size = settings.ReadString(L"Tab Bar", L"FontSize");
    wchar_t* end = nullptr;
    const auto points = wcstol(size.c_str(), &end, 10);
    GetMDITabCtrl().SetTitleFont(settings.ReadString(L"Tab Bar", L"Font"), end == size.data() + size.size() ? points : 0);
    UpdateLayout();
}
int MainFrame::TabWidthPreference() const {
    const auto text = settings.ReadString(L"Tab Bar", L"Width");
    wchar_t* end = nullptr;
    const auto width = wcstol(text.c_str(), &end, 10);
    return end == text.data() + text.size() && width >= DocumentTabStrip::minTabWidth && width <= DocumentTabStrip::maxTabWidth
        ? static_cast<int>(width) : DocumentTabStrip::defaultTabWidth;
}
void MainFrame::ApplyTabWidth() {
    GetMDITabCtrl().SetFixedTabWidth(settings.ReadString(L"Tab Bar", L"FixedWidth") == L"1" ? TabWidthPreference() : 0);
}
LRESULT MainFrame::OnDpi(UINT, WPARAM, LPARAM l, BOOL&) {
    auto* r = reinterpret_cast<RECT*>(l);
    SetWindowPos(nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyTabTitleFont();
    ApplyTabWidth();
    for (auto& d : documents) if (d->Send(SCI_GETMARGINWIDTHN, 0)) d->Send(SCI_SETMARGINWIDTHN, 0, MulDiv(54, GetDpiForWindow(m_hWnd), 96));
    for (auto& child : tabs.children) child->UpdateFileIcon();
    tabStrip.UpdateImages();
    UpdateToolbarImages(*this);
    return 0;
}

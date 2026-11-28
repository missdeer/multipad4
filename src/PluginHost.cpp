#include "MainFrame.h"
#include "../plugins/PluginApi.h"
#include "EditCaseMapping.h"
#include "FileCommands.h"
#include "FileDialogs.h"
#include <fstream>
#include "StyleManager.h"

LRESULT PluginPane::OnSize(UINT, WPARAM, LPARAM, BOOL& handled) {
    RECT rect{}; GetClientRect(&rect);
    if (::IsWindow(client)) ::MoveWindow(client, 0, 0, rect.right, rect.bottom, TRUE);
    handled = FALSE; return 0;
}
LRESULT PluginPane::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
    NMHDR notification{owner, static_cast<UINT_PTR>(::GetDlgCtrlID(owner)), 1051};
    ::SendMessageW(client, WM_NOTIFY, notification.idFrom, reinterpret_cast<LPARAM>(&notification));
    ::ShowWindow(client, SW_HIDE);
    if (IsWindowVisible()) Hide();
    return 0;
}
UINT_PTR PluginHost::Buffer(const EditorDocument* document) const { return document ? document->searchIdentity : 0; }
EditorDocument* PluginHost::Document(UINT_PTR id) {
    auto found = frame->tabs.documentsById.find(id);
    return found == frame->tabs.documentsById.end() ? nullptr : found->second;
}
LRESULT CALLBACK PluginHost::ProxyProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    auto* host = reinterpret_cast<PluginHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        host = static_cast<PluginHost*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (host && msg >= 2000 && msg < 6000)
        return host->SciMessage(msg, w, l);
    if (host && msg == WM_SETFOCUS) if (auto* d = host->frame->Active()) ::SetFocus(d->hwnd);
    return DefWindowProcW(window, msg, w, l);
}
sptr_t PluginHost::Direct(sptr_t host, unsigned int msg, uptr_t w, sptr_t l) {
    return reinterpret_cast<PluginHost*>(host)->SciMessage(msg, w, l);
}
sptr_t PluginHost::DirectStatus(sptr_t host, unsigned int msg, uptr_t w, sptr_t l, int* status) {
    auto result = Direct(host, msg, w, l); if (status) *status = static_cast<int>(Direct(host, SCI_GETSTATUS, 0, 0)); return result;
}
LRESULT PluginHost::SciMessage(UINT msg, WPARAM w, LPARAM l) {
    if (msg == SCI_GETDIRECTFUNCTION) return reinterpret_cast<LRESULT>(&Direct);
    if (msg == SCI_GETDIRECTSTATUSFUNCTION) return reinterpret_cast<LRESULT>(&DirectStatus);
    if (msg == SCI_GETDIRECTPOINTER) return reinterpret_cast<LRESULT>(this);
    auto* d = frame->Active(); if (!d || d->FastView()) return 0;
    // N++'s bundled fork returns text lengths excluding the terminator.
    if (msg == SCI_GETSELTEXT) return d->Send(msg, 0, l);
    if (msg == 2041 || msg == 2042 || msg == 2292) return d->Send(msg == 2041 ? SCI_MARKERSETFORETRANSLUCENT : msg == 2042 ? SCI_MARKERSETBACKTRANSLUCENT : SCI_MARKERSETBACKSELECTEDTRANSLUCENT, w, l | 0xFF000000u);
    // N++ uses pointer-sized positions even in its legacy range APIs.
    if (msg == 2162) return d->Send(SCI_GETTEXTRANGEFULL, w, l);
    if (msg == 2015 || msg == SCI_GETSTYLEDTEXTFULL) {
        auto* range = reinterpret_cast<Sci_TextRangeFull*>(l); if (!range || !range->lpstrText) return 0;
        auto end = range->chrg.cpMax == -1 ? d->Send(SCI_GETLENGTH) : range->chrg.cpMax;
        auto length = end - range->chrg.cpMin;
        if (range->chrg.cpMin < 0 || length < 0 || end > d->Send(SCI_GETLENGTH)) return 0;
        // N++ interleaves text and style bytes; notepad4 returns separate arrays.
        std::vector<char> packed(static_cast<size_t>(length) * 2 + 2);
        Sci_TextRangeFull full{range->chrg, packed.data()}; d->Send(SCI_GETSTYLEDTEXTFULL, 0, reinterpret_cast<LPARAM>(&full));
        for (Sci_Position i = 0; i < length; ++i) { range->lpstrText[2*i] = packed[length + 1 + i]; range->lpstrText[2*i+1] = packed[i]; }
        range->lpstrText[2*length] = 0; range->lpstrText[2*length+1] = 0; return 2*length;
    }
    if (msg == 2010) return static_cast<signed char>(d->Send(SCI_GETSTYLEINDEXAT, w));
    if (msg == 2151) return d->Send(SCI_FORMATRANGEFULL, w, l);
    if (msg == 2150 || msg == SCI_FINDTEXTFULL) {
        struct NppFind { Sci_CharacterRangeFull range; const char* text; Sci_CharacterRangeFull found; };
        auto* find = reinterpret_cast<NppFind*>(l); if (!find || !find->text) return -1;
        Sci_TextToFindFull full{find->range, find->text, strlen(find->text), {0, 0}};
        auto result = d->Send(SCI_FINDTEXTFULL, w, reinterpret_cast<LPARAM>(&full));
        find->found = full.chrgText; return result;
    }
    return d->Send(msg, w, l);
}
void PluginHost::IndexPlugin(size_t index) {
    const auto& plugin=plugins[index];modules.try_emplace(plugin.module,index);
    if(plugin.preTranslate)pretranslators.push_back(index);
    for(int i=0;i<plugin.count;++i){
        const auto& item=plugin.items[i];auto& entry=commands[static_cast<UINT>(item._cmdID)];
        if(item._pFunc){
            if(entry.normal.plugin==SIZE_MAX)entry.normal={index,i};
            if(plugin.supportsFastView && entry.fastView.plugin==SIZE_MAX)entry.fastView={index,i};
        }
        if(item._pShKey && entry.shortcut.plugin==SIZE_MAX)entry.shortcut={index,i};
    }
}
void PluginHost::Initialize(MainFrame& owner) {
    frame = &owner;
    if (!enabled) return;
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, _countof(exe));
    home = std::filesystem::path(exe).parent_path() / L"plugins"; config = home / L"Config";

    std::error_code directoryError;
    std::filesystem::create_directories(config, directoryError);
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = ProxyProc; cls.lpszClassName = L"MultiPad4.PluginScintilla";
    RegisterClassW(&cls);
    proxy = CreateWindowW(cls.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0, owner, nullptr, cls.hInstance, this);
    secondProxy = CreateWindowW(cls.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0, owner, nullptr, cls.hInstance, this);
    menu = CreatePopupMenu();
    const HMENU mainMenu = owner.runtime.mainMenu;
    int pluginMenuPosition = GetMenuItemCount(mainMenu) - 1;
    wchar_t label[128]{};
    const auto instance = GetModuleHandleW(nullptr);
    LoadStringW(instance, IDS_PLUGIN_MENU, label, _countof(label));
    InsertMenuW(mainMenu, pluginMenuPosition, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(menu), label);
    LoadStringW(instance, IDS_PLUGIN_ADMIN, label, _countof(label));
    AppendMenuW(menu, MF_STRING, IDM_SETTING_PLUGINADM, label);
    LoadStringW(instance, IDS_PLUGIN_OPEN_FOLDER, label, _countof(label));
    AppendMenuW(menu, MF_STRING, IDM_SETTING_OPENPLUGINSDIR, label);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    std::vector<std::filesystem::path> candidates;
    for (auto& entry : std::filesystem::directory_iterator(home, directoryError)) {
        if (!entry.is_directory()) continue;
        auto name = entry.path().filename().wstring();
        if (_wcsicmp(name.c_str(), L"disabled") == 0 || _wcsicmp(name.c_str(), L"Config") == 0) continue;
        if (onlyModule == L"__bundled__" && std::find(bundledModules.begin(), bundledModules.end(), name) == bundledModules.end()) continue;
        if (onlyModule != L"__bundled__" && !onlyModule.empty() && _wcsicmp((name + L".dll").c_str(), onlyModule.c_str()) != 0) continue;
        auto dll = entry.path() / (name + L".dll"); if (std::filesystem::is_regular_file(dll)) candidates.push_back(dll);
    }
    std::sort(candidates.begin(), candidates.end()); std::vector<ACCEL> keys;
    for (const auto& path : candidates) {
        Plugin plugin; plugin.module = path.filename().wstring();
        plugin.library = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!plugin.library) { errors.push_back(plugin.module + L": LoadLibrary error " + std::to_wstring(GetLastError())); continue; }
        auto unicode = reinterpret_cast<BOOL(__cdecl*)()>(GetProcAddress(plugin.library, "isUnicode"));
        auto info = reinterpret_cast<PFUNCSETINFO>(GetProcAddress(plugin.library, "setInfo"));
        auto name = reinterpret_cast<PFUNCGETNAME>(GetProcAddress(plugin.library, "getName"));
        auto funcs = reinterpret_cast<PFUNCGETFUNCSARRAY>(GetProcAddress(plugin.library, "getFuncsArray"));
        plugin.notify = reinterpret_cast<PBENOTIFIED>(GetProcAddress(plugin.library, "beNotified"));
        plugin.message = reinterpret_cast<PMESSAGEPROC>(GetProcAddress(plugin.library, "messageProc"));
        if (!unicode || !unicode() || !info || !name || !funcs || !plugin.notify || !plugin.message) {
            errors.push_back(plugin.module + L": invalid Unicode plugin exports"); FreeLibrary(plugin.library); continue;
        }
        plugin.editorNotification = reinterpret_cast<BOOL(__cdecl*)(const SCNotification*)>(GetProcAddress(plugin.library, "multipadHandleEditorNotification"));
        plugin.preTranslate = reinterpret_cast<BOOL(__cdecl*)(MSG*)>(GetProcAddress(plugin.library, "multipadPreTranslateMessage"));
        plugin.commandEnabled = reinterpret_cast<BOOL(__cdecl*)(UINT)>(GetProcAddress(plugin.library, "multipadCommandEnabled"));
        plugin.populateEditorContextMenu = reinterpret_cast<void(__cdecl*)(HMENU)>(
            GetProcAddress(plugin.library, "multipadPopulateEditorContextMenu"));
        auto supportsFastView = reinterpret_cast<BOOL(__cdecl*)()>(GetProcAddress(plugin.library, "multipadSupportsFastView"));
        plugin.supportsFastView = supportsFastView && supportsFastView();
        plugin.name = name(); info({owner, proxy, secondProxy}); plugin.items = funcs(&plugin.count);
        if (!plugin.items || plugin.count <= 0 || plugin.count > 4096 || nextCommand + plugin.count + 2 >= 56000) {
            errors.push_back(plugin.module + L": invalid command table"); FreeLibrary(plugin.library); continue;
        }
        HMENU submenu = plugin.submenu = CreatePopupMenu();
        for (int i = 0; i < plugin.count; ++i) {
            if (nextCommand == IDM_SETTING_OPENPLUGINSDIR) nextCommand += 2;
            auto& item = plugin.items[i]; item._cmdID = nextCommand++;
            if (!item._pFunc) { AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr); continue; }
            AppendMenuW(submenu, MF_STRING | (item._init2Check ? MF_CHECKED : 0), item._cmdID, item._itemName);
            if (auto* key = item._pShKey; key && key->_key) keys.push_back({static_cast<BYTE>(FVIRTKEY | (key->_isCtrl ? FCONTROL : 0) | (key->_isAlt ? FALT : 0) | (key->_isShift ? FSHIFT : 0)), key->_key, static_cast<WORD>(item._cmdID)});
        }
        if (auto customize = reinterpret_cast<void(__cdecl*)(HMENU)>(GetProcAddress(plugin.library, "multipadCustomizeMenu")))
            customize(submenu);
        const auto getMainMenuLabel = reinterpret_cast<const wchar_t* (__cdecl*)()>(
            GetProcAddress(plugin.library, "multipadGetMainMenuLabel"));
        const wchar_t* mainMenuLabel = getMainMenuLabel ? getMainMenuLabel() : nullptr;
        if (mainMenuLabel && *mainMenuLabel && InsertMenuW(mainMenu, pluginMenuPosition, MF_BYPOSITION | MF_POPUP,
            reinterpret_cast<UINT_PTR>(submenu), mainMenuLabel)) {
            ++pluginMenuPosition;
        } else {
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), plugin.name.c_str());
        }
        plugins.push_back(std::move(plugin));IndexPlugin(plugins.size()-1);
    }
    if (!keys.empty()) accelerators = CreateAcceleratorTableW(keys.data(), static_cast<int>(keys.size()));
    if (plugins.empty()) {
        LoadStringW(instance, IDS_PLUGIN_NONE_LOADED, label, _countof(label));
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, label);
    } else if (GetMenuItemCount(menu) == 3) DeleteMenu(menu, 2, MF_BYPOSITION);
    std::ofstream log(config / L"MultiPad4-plugin-load.log");
    for (auto& p : plugins) log << "Loaded " << files::ToUtf8(p.module) << " (" << p.count << " commands)\n";
    for (auto& e : errors) log << files::ToUtf8(e) << '\n';
    DrawMenuBar(owner);
}
void PluginHost::Notify(UINT code, UINT_PTR buffer) {
    if (stopped) return;
    SCNotification notification{}; notification.nmhdr.hwndFrom = frame->m_hWnd; notification.nmhdr.idFrom = buffer; notification.nmhdr.code = code;
    for (auto& p : plugins) p.notify(&notification);
}
bool PluginHost::ScintillaNotification(const SCNotification& source) {
    if (!ready || stopped) return false;
    auto* d = frame->Active();
    bool active = d && !d->FastView() && source.nmhdr.hwndFrom == d->hwnd;
    bool consumed = false;
    for (auto& p : plugins)
        if (p.editorNotification) consumed |= p.editorNotification(&source) != FALSE;
    auto notification = source; notification.nmhdr.hwndFrom = proxy; notification.nmhdr.idFrom = 0;
    if (active) for (auto& p : plugins) p.notify(&notification);
    return consumed && source.nmhdr.code != SCN_MODIFIED;
}
void PluginHost::Ready() { if (ready) return; ready = true; Notify(NPPN_TBMODIFICATION); Notify(NPPN_READY); Activated(); }
void PluginHost::Activated() {
    if (!ready || stopped) return;
    auto* document = frame->Active(); auto id = document && !document->FastView() ? Buffer(document) : 0; if (id == activeBuffer) return; activeBuffer = id;
    if (id) Notify(NPPN_BUFFERACTIVATED, id);
}
void PluginHost::Shutdown() {
    if (stopped) return;
    Notify(NPPN_SHUTDOWN); stopped = true;
    for (auto& pane : panes) if (pane->IsWindow()) {
        if (::IsWindow(pane->client)) ::DestroyWindow(pane->client);
        pane->DestroyWindow();
    }
    panes.clear();
    for (HWND dialog : modeless) if (::IsWindow(dialog)) ::DestroyWindow(dialog);
    modeless.clear();
    commands.clear();modules.clear();pretranslators.clear();
    for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) FreeLibrary(it->library);
    plugins.clear();
    if (accelerators) { DestroyAcceleratorTable(accelerators); accelerators = nullptr; }
    if (::IsWindow(proxy)) ::DestroyWindow(proxy); if (::IsWindow(secondProxy)) ::DestroyWindow(secondProxy);
}
void PluginHost::UpdateMenu(HMENU submenu) {
    const auto* document = frame->Active();
    for (const auto& plugin : plugins) {
        if (plugin.submenu != submenu || !plugin.commandEnabled) continue;
        const bool available = !document || !document->FastView() || plugin.supportsFastView;
        for (int i = 0; i < plugin.count; ++i) {
            const auto& item = plugin.items[i];
            if (item._pFunc)
                EnableMenuItem(submenu, item._cmdID, MF_BYCOMMAND |
                    (available && plugin.commandEnabled(item._cmdID) ? MF_ENABLED : MF_GRAYED));
        }
        break;
    }
}
void PluginHost::PopulateEditorContextMenu(HMENU submenu) {
    if (!submenu) return;
    const auto* document = frame->Active();
    for (const auto& plugin : plugins)
        if (plugin.populateEditorContextMenu && (!document || !document->FastView() || plugin.supportsFastView))
            plugin.populateEditorContextMenu(submenu);
}
bool PluginHost::Execute(UINT id) {
    const bool fastView = frame->Active() && frame->Active()->FastView();
    auto found=commands.find(id);if(found==commands.end())return false;
    const auto target=fastView?found->second.fastView:found->second.normal;
    if(target.plugin==SIZE_MAX)return false;
    auto& p=plugins[target.plugin];
    if(p.commandEnabled && !p.commandEnabled(id))return false;
    p.items[target.item]._pFunc();frame->Refresh();return true;
}
bool PluginHost::PreTranslate(MSG* msg) {
    for(auto index:pretranslators)if(plugins[index].preTranslate(msg))return true;
    if (accelerators && TranslateAcceleratorW(*frame, accelerators, msg)) return true;
    for (HWND dialog : modeless) if (::IsWindow(dialog) && IsDialogMessageW(dialog, msg)) return true;
    return false;
}

LRESULT PluginHost::Message(UINT msg, WPARAM w, LPARAM l) {
    auto* d = frame->Active();
    auto copy = [&](const std::wstring& value, bool boolean = false) -> LRESULT {
        if (!l) return value.size();
        if (w <= value.size()) return FALSE;
        wcscpy_s(reinterpret_cast<wchar_t*>(l), w, value.c_str());
        return boolean ? TRUE : static_cast<LRESULT>(value.size());
    };
    auto path = [](EditorDocument* doc) { return doc->path.empty() ? doc->untitled : doc->path.wstring(); };
    auto language = [](EditorDocument* doc) { return !doc ? L_TEXT : doc->language == L"JSON" ? L_JSON : doc->language == L"XML" ? L_XML : doc->language == L"HTML" ? L_HTML : doc->language == L"C/C++" ? L_CPP : L_TEXT; };
    switch (msg) {
    case MPM_GETPROFILESTRING: {
        const auto* request = reinterpret_cast<const MultiPadProfileString*>(l);
        if (!request || !request->section || !request->key || !request->fallback) return -1;
        const auto value = frame->settings.ReadString(request->section, request->key, request->fallback);
        if (request->text) {
            if (request->capacity <= value.size()) return -1;
            wcscpy_s(request->text, request->capacity, value.c_str());
        }
        return static_cast<LRESULT>(value.size());
    }
    case MPM_OPENFOLDER:
    case MPM_RUNCOMMAND:
        if (!l || frame->runtime.operationDepth || frame->runtime.inBackgroundWork || !frame->IsWindowEnabled()) return FALSE;
        try {
            const auto* text = reinterpret_cast<const wchar_t*>(l);
            if (msg == MPM_OPENFOLDER)
                return OpenFolderRequest(*frame, text, w ? FolderOpenRequest::ContainingFolder : FolderOpenRequest::OpenFolder);
            ExecuteExternalCommand(*frame, text);
            return TRUE;
        } catch (const std::exception& error) { frame->Error(error); return FALSE; }
    case MPM_GETCOMPLETIONSTATE: {
        auto* doc = Document(w); if (!doc || doc->FastView()) return 0;
        const auto accepts = [&](int id) {
            const auto found = doc->style.completionOptions.find(id);
            return found == doc->style.completionOptions.end() || found->second != 0;
        };
        return (accepts(IDC_AUTOC_FILLUP_ENTER) ? MPC_ACCEPTENTER : 0) |
            (accepts(IDC_AUTOC_FILLUP_TAB) ? MPC_ACCEPTTAB : 0) |
            (doc->completionComposing ? MPC_COMPOSING : 0);
    }
    case MPM_SETEXTERNALCOMPLETION: {
        auto* doc = Document(w); if (!doc || doc->FastView()) return FALSE;
        doc->externalCompletion = l != 0;
        doc->Send(SCI_AUTOCCANCEL);
        doc->style.completionPending = false; ++doc->completionEpoch;
        return TRUE;
    }
    case MPM_GETBUFFEREDITORWINDOW: {
        auto* doc = Document(w);
        return doc && !doc->FastView() ? reinterpret_cast<LRESULT>(doc->hwnd) : 0;
    }
    case MPM_GETEDITORWINDOW:
        return d && !d->FastView() ? reinterpret_cast<LRESULT>(d->hwnd) : 0;
    case MPM_GETLEXERMETADATA: {
        if (!d || d->FastView() || !l) return FALSE;
        const auto* lexer = GetDocumentLexer(*d);
        *reinterpret_cast<MultiPadLexerMetadata*>(l) = {
            lexer->iLexer, lexer->lexerAttr, lexer->commentStyleMarker, lexer->escapeCharacterStyle,
            lexer->operatorStyle, lexer->operatorStyle2, lexer->stringStyleFirst, lexer->stringStyleLast};
        return TRUE;
    }
    case MPM_MAPSELECTION:
        if (!d || d->FastView() || d->Send(SCI_GETREADONLY) || !l) return FALSE;
        ApplyDocumentCaseMapping(*d, reinterpret_cast<MultiPadSelectionMapper>(l));
        return TRUE;
    case MPM_GETBUFFERFILEENCODING: {
        auto* doc = Document(w);
        return doc ? static_cast<LRESULT>(doc->encoding) : -1;
    }
    case MPM_GETBUFFERTITLE: {
        auto* doc = Document(w); auto* request = reinterpret_cast<MultiPadBufferTitle*>(l);
        if (!doc || !request) return -1;
        auto title = doc->Title();
        if (request->text) {
            if (request->capacity <= title.size()) return -1;
            wcscpy_s(request->text, request->capacity, title.c_str());
        }
        return static_cast<LRESULT>(title.size());
    }
    case NPPM_GETCURRENTSCINTILLA: if (!l) return FALSE; *reinterpret_cast<int*>(l) = 0; return TRUE;
    case NPPM_GETCURRENTVIEW: return 0;
    case NPPM_GETNPPVERSION: return MAKELONG(w ? 880 : 88, 8);
    case NPPM_GETWINDOWSVERSION: return WV_WIN11;
    case NPPM_GETCURRENTNATIVELANGENCODING: return 65001;
    case NPPM_GETPLUGINSCONFIGDIR: return copy(config.wstring(), true);
    case NPPM_GETPLUGINHOMEPATH: return copy(home.wstring());
    case NPPM_GETNPPSETTINGSDIRPATH: return copy(home.parent_path().wstring(), true);
    case NPPM_GETNPPDIRECTORY: return copy(home.parent_path().wstring(), true);
    case NPPM_GETNPPFULLFILEPATH: { wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, _countof(exe)); return copy(exe, true); }
    case NPPM_GETFULLCURRENTPATH: return d ? copy(path(d), true) : FALSE;
    case NPPM_GETCURRENTDIRECTORY: return d ? copy(d->path.parent_path().wstring(), true) : FALSE;
    case NPPM_GETFILENAME: return d ? copy(d->path.empty() ? d->untitled : d->path.filename().wstring(), true) : FALSE;
    case NPPM_GETNAMEPART: return d ? copy(d->path.stem().wstring(), true) : FALSE;
    case NPPM_GETEXTPART: return d ? copy(d->path.extension().wstring(), true) : FALSE;
    case NPPM_GETCURRENTLINE: return d ? d->Send(SCI_LINEFROMPOSITION, d->Send(SCI_GETCURRENTPOS)) : -1;
    case NPPM_GETCURRENTCOLUMN: return d ? d->Send(SCI_GETCOLUMN, d->Send(SCI_GETCURRENTPOS)) : -1;
    case NPPM_GETCURRENTBUFFERID: return Buffer(d);
    case NPPM_GETNBOPENFILES: return l == 2 ? 0 : frame->tabs.GetPageCount();
    case NPPM_GETCURRENTDOCINDEX: return l == 1 ? -1 : frame->tabs.GetActivePage();
    case NPPM_GETBUFFERIDFROMPOS: return l == 0 && w < static_cast<WPARAM>(frame->tabs.GetPageCount()) ? Buffer(static_cast<EditorDocument*>(frame->tabs.GetPageData(static_cast<int>(w)))) : 0;
    case NPPM_GETPOSFROMBUFFERID: return frame->tabs.Position(Document(w));
    case MPM_GETBUFFERFILEPATH:
    case NPPM_GETFULLPATHFROMBUFFERID: {
        auto* doc = Document(w); if (!doc) return -1;
        auto name = msg == MPM_GETBUFFERFILEPATH ? doc->path.wstring() : path(doc);
        if (l) wcscpy_s(reinterpret_cast<wchar_t*>(l), name.size() + 1, name.c_str());
        return name.size();
    }
    case MPM_CLOSEBUFFER: {
        if (frame->runtime.operationDepth || frame->runtime.inBackgroundWork || !frame->IsWindowEnabled()) return FALSE;
        const auto position = Message(NPPM_GETPOSFROMBUFFERID, w, 0);
        return position < 0 || frame->Close(static_cast<int>(position));
    }
    case NPPM_GETOPENFILENAMES_DEPRECATED: case NPPM_GETOPENFILENAMESPRIMARY_DEPRECATED: {
        if (!w || l <= 0) return 0; int count = min(static_cast<int>(l), frame->tabs.GetPageCount());
        for (int i = 0; i < count; ++i) { auto name = path(static_cast<EditorDocument*>(frame->tabs.GetPageData(i))); wcscpy_s(reinterpret_cast<wchar_t**>(w)[i], MAX_PATH, name.c_str()); } return count;
    }
    case NPPM_GETOPENFILENAMESSECOND_DEPRECATED: return 0;
    case NPPM_ACTIVATEDOC: if (w || l < 0 || l >= frame->tabs.GetPageCount()) return FALSE; frame->tabs.SetActivePage(static_cast<int>(l)); frame->Refresh(); return TRUE;
    case NPPM_DOOPEN: case NPPM_SWITCHTOFILE: return l && !frame->runtime.operationDepth && !frame->runtime.inBackgroundWork && frame->IsWindowEnabled() && frame->Open(reinterpret_cast<const wchar_t*>(l));
    case NPPM_SAVECURRENTFILE: return d && frame->Save(*d);
    case NPPM_SAVECURRENTFILEAS: return d && l && frame->SaveTo(*d, reinterpret_cast<const wchar_t*>(l), w != 0);
    case NPPM_SAVEALLFILES: for (auto& doc : frame->documents) if (doc->Modified() && !frame->Save(*doc)) return FALSE; return TRUE;
    case NPPM_RELOADBUFFERID: { auto* doc = Document(w); return doc && frame->Reload(*doc); }
    case NPPM_GETCURRENTLANGTYPE: if (!l) return FALSE; *reinterpret_cast<int*>(l) = language(d); return TRUE;
    case NPPM_GETBUFFERLANGTYPE: return Document(w) ? language(Document(w)) : -1;
    case NPPM_SETCURRENTLANGTYPE: case NPPM_SETBUFFERLANGTYPE: {
        auto* doc = msg == NPPM_SETCURRENTLANGTYPE ? d : Document(w); if (!doc) return FALSE;
        const wchar_t* name = l == L_JSON ? L"JSON" : l == L_XML ? L"XML" : l == L_HTML ? L"HTML" : l == L_CPP ? L"C/C++" : L"Text";
        SelectDocumentScheme(*doc, name); frame->Refresh(); Notify(NPPN_LANGCHANGED, Buffer(doc)); return TRUE;
    }
    case NPPM_GETBUFFERENCODING: {
        auto* doc = Document(w); if (!doc) return -1;
        switch (doc->encoding) {
        case files::Encoding::Utf8Bom: return 1; case files::Encoding::Utf16BE: return 2; case files::Encoding::Utf16LE: return 3;
        case files::Encoding::Utf8: return 4; case files::Encoding::Utf16BEPlain: return 6; case files::Encoding::Utf16LEPlain: return 7; default: return 0;
        }
    }
    case NPPM_GETBUFFERFORMAT: { auto* doc = Document(w); if (!doc) return -1; auto eol = doc->Send(SCI_GETEOLMODE); return eol == SC_EOL_CRLF ? 0 : eol == SC_EOL_CR ? 1 : 2; }
    case NPPM_SETSTATUSBAR:
        if (l && w < 6) ::SendMessageW(frame->m_hWndStatusBar, SB_SETTEXTW, w, l); return TRUE;
    case NPPM_GETLANGUAGENAME: case NPPM_GETLANGUAGEDESC: {
        std::wstring name = w == L_JSON ? L"JSON" : w == L_XML ? L"XML" : w == L_HTML ? L"HTML" : w == L_CPP ? L"C++" : L"Normal text";
        if (l) wcscpy_s(reinterpret_cast<wchar_t*>(l), name.size() + 1, name.c_str()); return name.size();
    }
    case NPPM_GETBOOKMARKID: return 0;
    case NPPM_GETSHORTCUTBYCMDID:
        if(l){auto found=commands.find(static_cast<UINT>(w));if(found!=commands.end()){
            const auto target=found->second.shortcut;if(target.plugin!=SIZE_MAX){*reinterpret_cast<ShortcutKey*>(l)=*plugins[target.plugin].items[target.item]._pShKey;return TRUE;}
        }}
        return FALSE;
    case NPPM_GETMENUHANDLE: return reinterpret_cast<LRESULT>(w == NPPPLUGINMENU ? menu : frame->GetMenu());
    case NPPM_SETMENUITEMCHECK: CheckMenuItem(frame->GetMenu(), static_cast<UINT>(w), MF_BYCOMMAND | (l ? MF_CHECKED : MF_UNCHECKED)); return TRUE;
    case NPPM_MENUCOMMAND: {
        // N++ command IDs overlap notepad4 IDs, so translate before dispatch.
        UINT command = l == 41001 ? ID_NEW : l == 41002 ? ID_OPEN : l == 41003 ? ID_CLOSE : l == 41006 ? ID_SAVE : l == 41007 ? ID_SAVEAS : l == 41008 ? ID_SAVEALL : 0;
        if (!command) return FALSE; frame->SendMessage(WM_COMMAND, command); return TRUE;
    }
    case NPPM_MODELESSDIALOG: {
        auto window = reinterpret_cast<HWND>(l); if (!::IsWindow(window)) return 0;
        std::erase(modeless, window); if (w == MODELESSDIALOGADD) modeless.push_back(window); return l;
    }
    case NPPM_DMMREGASDCKDLG: {
        auto* data = reinterpret_cast<tTbData*>(l); if (!data || !::IsWindow(data->hClient)) return FALSE;
        for (auto& pane : panes) if (pane->client == data->hClient) return TRUE;
        auto pane = std::make_unique<PluginPane>(); pane->owner = *frame; pane->client = data->hClient; pane->dialogId = data->dlgID; pane->module = data->pszModuleName ? data->pszModuleName : L"";
        DockPlacement placement{};
        if (!pane->Create(*frame, placement.floating, data->pszName)) return FALSE;
        ::SetParent(data->hClient, *pane); SetWindowLongPtrW(data->hClient, GWL_STYLE, (GetWindowLongPtrW(data->hClient, GWL_STYLE) & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_CHILD);
        ::SetWindowPos(data->hClient, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        constexpr PaneSide sides[]{PaneSide::Left, PaneSide::Right, PaneSide::Top, PaneSide::Bottom};
        if (!(data->uMask & DWS_DF_FLOATING)) placement.side = sides[(data->uMask >> 28) & 3];
        placement.width = MulDiv(440, GetDpiForWindow(*frame), 96);
        placement.height = MulDiv(210, GetDpiForWindow(*frame), 96);
        pane->Place(placement);
        ::ShowWindow(data->hClient, SW_HIDE); pane->SendMessage(WM_SIZE); panes.push_back(std::move(pane)); return TRUE;
    }
    case NPPM_DMMSHOW: case NPPM_DMMHIDE: case NPPM_DMMUPDATEDISPINFO:
        for (auto& pane : panes) if (pane->client == reinterpret_cast<HWND>(l)) {
            if (msg == NPPM_DMMSHOW) { ::ShowWindow(pane->client, SW_SHOW); pane->Show(); pane->SendMessage(WM_SIZE); } else if (msg == NPPM_DMMHIDE) { ::ShowWindow(pane->client, SW_HIDE); pane->Hide(); } return TRUE;
        } return FALSE;
    case NPPM_DMMGETPLUGINHWNDBYNAME:
        if (l) for (auto& pane : panes) if (pane->dialogId == static_cast<int>(w) && _wcsicmp(pane->module.c_str(), reinterpret_cast<const wchar_t*>(l)) == 0) return reinterpret_cast<LRESULT>(pane->client);
        return 0;
    case NPPM_ALLOCATESUPPORTED_DEPRECATED: return TRUE;
    case NPPM_ALLOCATECMDID: case NPPM_ALLOCATEMARKER: case NPPM_ALLOCATEINDICATOR: {
        if (!l) return FALSE; *reinterpret_cast<int*>(l) = 0;
        int start = msg == NPPM_ALLOCATECMDID ? nextCommand : msg == NPPM_ALLOCATEMARKER ? nextMarker : nextIndicator;
        int limit = msg == NPPM_ALLOCATECMDID ? 56000 : msg == NPPM_ALLOCATEMARKER ? 24 : 32;
        if (!w || w > static_cast<WPARAM>(limit - start)) return FALSE;
        if (msg == NPPM_ALLOCATECMDID && start <= static_cast<int>(IDM_SETTING_PLUGINADM) &&
            start + static_cast<int>(w) > static_cast<int>(IDM_SETTING_OPENPLUGINSDIR)) {
            start = IDM_SETTING_PLUGINADM + 1;
            if (w > static_cast<WPARAM>(limit - start)) return FALSE;
        }
        *reinterpret_cast<int*>(l) = start;
        if (msg == NPPM_ALLOCATECMDID) nextCommand = start + static_cast<UINT>(w); else if (msg == NPPM_ALLOCATEMARKER) nextMarker += static_cast<int>(w); else nextIndicator += static_cast<int>(w);
        return TRUE;
    }
    case NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR: return d ? d->Send(SCI_STYLEGETFORE, STYLE_DEFAULT) : RGB(0,0,0);
    case NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR: return d ? d->Send(SCI_STYLEGETBACK, STYLE_DEFAULT) : RGB(255,255,255);
    case NPPM_ISDARKMODEENABLED: return FALSE;
    case NPPM_GETNATIVELANGFILENAME: if (l && w) *reinterpret_cast<char*>(l) = 0; return 0;
    case NPPM_GETCURRENTCMDLINE: return copy(GetCommandLineW(), true);
    case NPPM_SETUNTITLEDNAME: { auto* doc = w == static_cast<WPARAM>(-1) ? d : Document(w); if (!doc || !doc->path.empty() || !l) return FALSE; doc->untitled = reinterpret_cast<const wchar_t*>(l); frame->Refresh(); return TRUE; }
    case NPPM_ADDSCNMODIFIEDFLAGS: for (auto& doc : frame->documents) doc->Send(SCI_SETMODEVENTMASK, doc->Send(SCI_GETMODEVENTMASK) | w); return TRUE;
    case NPPM_CREATESCINTILLAHANDLE: return reinterpret_cast<LRESULT>(CreateWindowW(L"Scintilla", L"", WS_CHILD, 0, 0, 0, 0, w ? reinterpret_cast<HWND>(w) : *frame, nullptr, GetModuleHandleW(nullptr), nullptr));
    case NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED: return ::DestroyWindow(reinterpret_cast<HWND>(l));
    case NPPM_MSGTOPLUGIN:
        if(w && l){auto found=modules.find(reinterpret_cast<const wchar_t*>(w));if(found!=modules.end()){plugins[found->second].message(msg,w,l);return TRUE;}}return FALSE;
    default: return FALSE;
    }
}

#include "MainFrame.h"
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
    for (auto& d : frame->documents) if (Buffer(d.get()) == id) return d.get();
    return nullptr;
}
LRESULT CALLBACK PluginHost::ProxyProc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    auto* host = reinterpret_cast<PluginHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        host = static_cast<PluginHost*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (host && msg >= 2000 && msg < 6000) return host->SciMessage(msg, w, l);
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
    auto* d = frame->Active(); if (!d) return 0;
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
void PluginHost::Initialize(MainFrame& owner) {
    frame = &owner;
    if (!enabled) return;
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, _countof(exe));
    home = std::filesystem::path(exe).parent_path() / L"plugins"; config = home / L"Config";
    if (!std::filesystem::is_directory(home)) return;
    std::filesystem::create_directories(config);
    WNDCLASSW cls{}; cls.hInstance = _Module.GetModuleInstance(); cls.lpfnWndProc = ProxyProc; cls.lpszClassName = L"MultiPad4.PluginScintilla";
    RegisterClassW(&cls);
    proxy = CreateWindowW(cls.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0, owner, nullptr, cls.hInstance, this);
    secondProxy = CreateWindowW(cls.lpszClassName, L"", WS_CHILD, 0, 0, 0, 0, owner, nullptr, cls.hInstance, this);
    menu = CreatePopupMenu();
    InsertMenuW(owner.GetMenu(), GetMenuItemCount(owner.GetMenu()) - 1, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(menu), L"&Plugins");
    std::vector<std::filesystem::path> candidates;
    for (auto& entry : std::filesystem::directory_iterator(home)) {
        if (!entry.is_directory()) continue;
        auto name = entry.path().filename().wstring();
        if (_wcsicmp(name.c_str(), L"disabled") == 0 || _wcsicmp(name.c_str(), L"Config") == 0) continue;
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
        plugin.name = name(); info({owner, proxy, secondProxy}); plugin.items = funcs(&plugin.count);
        if (!plugin.items || plugin.count <= 0 || plugin.count > 4096 || nextCommand + plugin.count >= 56000) {
            errors.push_back(plugin.module + L": invalid command table"); FreeLibrary(plugin.library); continue;
        }
        HMENU submenu = CreatePopupMenu();
        for (int i = 0; i < plugin.count; ++i) {
            auto& item = plugin.items[i]; item._cmdID = nextCommand++;
            if (!item._pFunc) { AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr); continue; }
            AppendMenuW(submenu, MF_STRING | (item._init2Check ? MF_CHECKED : 0), item._cmdID, item._itemName);
            if (auto* key = item._pShKey; key && key->_key) keys.push_back({static_cast<BYTE>(FVIRTKEY | (key->_isCtrl ? FCONTROL : 0) | (key->_isAlt ? FALT : 0) | (key->_isShift ? FSHIFT : 0)), key->_key, static_cast<WORD>(item._cmdID)});
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), plugin.name.c_str()); plugins.push_back(std::move(plugin));
    }
    if (!keys.empty()) accelerators = CreateAcceleratorTableW(keys.data(), static_cast<int>(keys.size()));
    if (plugins.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No compatible plugins loaded");
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
void PluginHost::ScintillaNotification(const SCNotification& source) {
    if (!ready || stopped) return;
    auto* d = frame->Active(); if (!d || source.nmhdr.hwndFrom != d->hwnd) return;
    auto notification = source; notification.nmhdr.hwndFrom = proxy; notification.nmhdr.idFrom = 0;
    for (auto& p : plugins) p.notify(&notification);
}
void PluginHost::Ready() { if (ready) return; ready = true; Notify(NPPN_TBMODIFICATION); Notify(NPPN_READY); Activated(); }
void PluginHost::Activated() {
    if (!ready || stopped) return;
    auto id = Buffer(frame->Active()); if (id == activeBuffer) return; activeBuffer = id;
    if (id) Notify(NPPN_BUFFERACTIVATED, id);
}
void PluginHost::Shutdown() {
    if (stopped) return;
    Notify(NPPN_SHUTDOWN); stopped = true;
    for (auto& pane : panes) if (pane->IsWindow()) {
        if (::IsWindow(pane->client)) ::DestroyWindow(pane->client);
        if (pane->IsDocking()) pane->Undock(); pane->DestroyWindow();
    }
    panes.clear();
    for (HWND dialog : modeless) if (::IsWindow(dialog)) ::DestroyWindow(dialog);
    modeless.clear();
    for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) FreeLibrary(it->library);
    plugins.clear();
    if (accelerators) { DestroyAcceleratorTable(accelerators); accelerators = nullptr; }
    if (::IsWindow(proxy)) ::DestroyWindow(proxy); if (::IsWindow(secondProxy)) ::DestroyWindow(secondProxy);
}
bool PluginHost::Execute(UINT id) {
    for (auto& p : plugins) for (int i = 0; i < p.count; ++i) if (p.items[i]._cmdID == static_cast<int>(id) && p.items[i]._pFunc) { p.items[i]._pFunc(); frame->Refresh(); return true; }
    return false;
}
bool PluginHost::PreTranslate(MSG* msg) {
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
    case NPPM_GETPOSFROMBUFFERID:
        for (int i = 0; i < frame->tabs.GetPageCount(); ++i) if (Buffer(static_cast<EditorDocument*>(frame->tabs.GetPageData(i))) == w) return i;
        return -1;
    case NPPM_GETFULLPATHFROMBUFFERID: {
        auto* doc = Document(w); if (!doc) return -1;
        auto name = path(doc); if (l) wcscpy_s(reinterpret_cast<wchar_t*>(l), name.size() + 1, name.c_str()); return name.size();
    }
    case NPPM_GETOPENFILENAMES_DEPRECATED: case NPPM_GETOPENFILENAMESPRIMARY_DEPRECATED: {
        if (!w || l <= 0) return 0; int count = min(static_cast<int>(l), frame->tabs.GetPageCount());
        for (int i = 0; i < count; ++i) { auto name = path(static_cast<EditorDocument*>(frame->tabs.GetPageData(i))); wcscpy_s(reinterpret_cast<wchar_t**>(w)[i], MAX_PATH, name.c_str()); } return count;
    }
    case NPPM_GETOPENFILENAMESSECOND_DEPRECATED: return 0;
    case NPPM_ACTIVATEDOC: if (w || l < 0 || l >= frame->tabs.GetPageCount()) return FALSE; frame->tabs.SetActivePage(static_cast<int>(l)); frame->Refresh(); return TRUE;
    case NPPM_DOOPEN: case NPPM_SWITCHTOFILE: return l && frame->Open(reinterpret_cast<const wchar_t*>(l));
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
        if (l) for (auto& p : plugins) for (int i = 0; i < p.count; ++i) if (p.items[i]._cmdID == static_cast<int>(w) && p.items[i]._pShKey) { *reinterpret_cast<ShortcutKey*>(l) = *p.items[i]._pShKey; return TRUE; }
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
        RECT rect{100, 100, 500, 600}; if (!pane->Create(*frame, rect, data->pszName)) return FALSE;
        ::SetParent(data->hClient, *pane); SetWindowLongPtrW(data->hClient, GWL_STYLE, (GetWindowLongPtrW(data->hClient, GWL_STYLE) & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_CHILD);
        ::SetWindowPos(data->hClient, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        dockwins::DFDOCKPOS pos{}; pos.hdr.hWnd = *pane; pos.hdr.hBar = *frame;
        auto side = (data->uMask >> 28) & 3;
        pos.dwDockSide = side == 0 ? dockwins::CDockingSide::sLeft : side == 1 ? dockwins::CDockingSide::sRight : side == 2 ? dockwins::CDockingSide::sTop : dockwins::CDockingSide::sBottom;
        pos.nWidth = MulDiv(440, GetDpiForWindow(*frame), 96); pos.nHeight = MulDiv(520, GetDpiForWindow(*frame), 96); pos.fPctPos = 0;
        if (!(data->uMask & DWS_DF_FLOATING)) pane->SetDockingPosition(&pos);
        ::ShowWindow(data->hClient, SW_HIDE); pane->SendMessage(WM_SIZE); pane->Hide(); panes.push_back(std::move(pane)); return TRUE;
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
        *reinterpret_cast<int*>(l) = start;
        if (msg == NPPM_ALLOCATECMDID) nextCommand += static_cast<UINT>(w); else if (msg == NPPM_ALLOCATEMARKER) nextMarker += static_cast<int>(w); else nextIndicator += static_cast<int>(w);
        return TRUE;
    }
    case NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR: return d ? d->Send(SCI_STYLEGETFORE, STYLE_DEFAULT) : RGB(0,0,0);
    case NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR: return d ? d->Send(SCI_STYLEGETBACK, STYLE_DEFAULT) : RGB(255,255,255);
    case NPPM_ISDARKMODEENABLED: return FALSE;
    case NPPM_GETNATIVELANGFILENAME: if (l && w) *reinterpret_cast<char*>(l) = 0; return 0;
    case NPPM_GETCURRENTCMDLINE: return copy(GetCommandLineW(), true);
    case NPPM_SETUNTITLEDNAME: { auto* doc = w == static_cast<WPARAM>(-1) ? d : Document(w); if (!doc || !doc->path.empty() || !l) return FALSE; doc->untitled = reinterpret_cast<const wchar_t*>(l); frame->Refresh(); return TRUE; }
    case NPPM_ADDSCNMODIFIEDFLAGS: for (auto& doc : frame->documents) doc->Send(SCI_SETMODEVENTMASK, doc->Send(SCI_GETMODEVENTMASK) | w); return TRUE;
    case NPPM_CREATESCINTILLAHANDLE: return reinterpret_cast<LRESULT>(CreateWindowW(L"Scintilla", L"", WS_CHILD, 0, 0, 0, 0, w ? reinterpret_cast<HWND>(w) : *frame, nullptr, _Module.GetModuleInstance(), nullptr));
    case NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED: return ::DestroyWindow(reinterpret_cast<HWND>(l));
    case NPPM_MSGTOPLUGIN: if (w && l) for (auto& p : plugins) if (_wcsicmp(p.module.c_str(), reinterpret_cast<const wchar_t*>(w)) == 0) { p.message(msg, w, l); return TRUE; } return FALSE;
    default: return FALSE;
    }
}

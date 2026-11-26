// Adapted from Notepad++ PluginsAdmin (Copyright (C)2021 Don HO).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainFrame.h"
#include "PluginAdmin.h"
#include "PluginMaintenance.h"
#include "Session.h"
#include "../3rdparty/nlohmann-json/json.hpp"
#include <fstream>
#include <array>
#include <set>
#include <cwctype>
using nlohmann::json;
namespace {
std::wstring AdminText(UINT id) {
    wchar_t text[256]{};
    LoadStringW(GetModuleHandleW(nullptr), id, text, _countof(text));
    return text;
}
std::string Resource(UINT id) {
    auto instance = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    auto data = LoadResource(instance, resource);
    if (!resource || !data) throw std::runtime_error(files::ToUtf8(AdminText(IDS_PLUGINADM_RESOURCE_MISSING)));
    return {static_cast<const char*>(LockResource(data)), SizeofResource(instance, resource)};
}
std::array<unsigned, 4> Version(const std::wstring& text) {
    std::array<unsigned, 4> result{}; const wchar_t* p = text.c_str();
    for (auto& part : result) { wchar_t* end = nullptr; part = wcstoul(p, &end, 10); if (end == p || *end != L'.') break; p = end + 1; }
    return result;
}
bool Compatible(const std::wstring& interval) {
    if (interval.empty()) return true;
    const auto host = Version(L"8.8.8"); // Same version reported by PluginHost.
    if (interval.front() != L'[') return Version(interval) == host;
    const auto comma = interval.find(L',');
    if (comma == std::wstring::npos || interval.back() != L']') return false;
    auto first = interval.substr(1, comma - 1), last = interval.substr(comma + 1, interval.size() - comma - 2);
    return (first.empty() || Version(first) <= host) && (last.empty() || host <= Version(last));
}
struct PluginFileInfo {
    std::wstring version, name, description, author, homepage, copyright, license;
};
PluginFileInfo ReadPluginFileInfo(const std::filesystem::path& path) {
    PluginFileInfo result;
    DWORD unused = 0; const auto size = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!size) return result;
    std::vector<BYTE> bytes(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data())) return result;
    VS_FIXEDFILEINFO* info = nullptr; UINT length = 0;
    if (VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &length)
        && info && length >= sizeof(*info) && info->dwSignature == VS_FFI_SIGNATURE) {
        result.version = std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS))
            + L"." + std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionLS));
    }
    struct Translation { WORD language, codePage; };
    Translation* translations = nullptr;
    std::vector<Translation> candidates;
    if (VerQueryValueW(bytes.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length) && translations && length >= sizeof(Translation))
        candidates.assign(translations, translations + length / sizeof(Translation));
    // Some third-party DLLs omit the translation table.
    candidates.push_back({0x0409, 1200}); candidates.push_back({0x0409, 1252});
    auto read = [&](const wchar_t* key) -> std::wstring {
        for (const auto& translation : candidates) {
            wchar_t query[128]{};
            swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%ls", translation.language, translation.codePage, key);
            wchar_t* value = nullptr; UINT characters = 0;
            if (VerQueryValueW(bytes.data(), query, reinterpret_cast<void**>(&value), &characters) && value && characters) {
                std::wstring text(value, std::find(value, value + characters, L'\0'));
                if (!text.empty()) return text;
            }
        }
        return {};
    };
    result.name = read(L"PluginName");
    if (result.name.empty()) result.name = read(L"ProductName");
    result.description = read(L"Comments");
    if (result.description.empty()) result.description = read(L"FileDescription");
    result.author = read(L"Author");
    if (result.author.empty()) result.author = read(L"CompanyName");
    result.homepage = read(L"Homepage");
    result.copyright = read(L"LegalCopyright");
    result.license = read(L"License");
    return result;
}
std::wstring Lower(std::wstring value) { for (auto& ch : value) ch = static_cast<wchar_t>(towlower(ch)); return value; }
}
void PluginAdmin::Load() {
    entries.clear();
    auto catalog = json::parse(Resource(IDR_PLUGIN_CATALOG));
    // A locally supplied x64 catalogue can replace the bundled snapshot.
    auto local = frame->plugins.config / L"nppPluginList.json";
    if (std::filesystem::exists(local)) { std::ifstream input(local); input >> catalog; }
    if (catalog.value("arch", "") != "64") throw std::runtime_error(files::ToUtf8(AdminText(IDS_PLUGINADM_CATALOG_ARCH)));
    catalogVersion = files::FromUtf8(catalog.at("version").get<std::string>());
    std::set<std::wstring> seen;
    for (const auto& item : catalog.at("npp-plugins")) {
        Entry entry;
        auto get = [&](const char* key) { return files::FromUtf8(item.value(key, std::string{})); };
        entry.folder = get("folder-name");
        if (!ValidPluginFolder(entry.folder) || !seen.insert(Lower(entry.folder)).second) continue;
        entry.name = get("display-name"); entry.version = get("version"); entry.description = get("description");
        entry.author = get("author"); entry.homepage = get("homepage"); entry.url = get("repository"); entry.hash = get("id");
        entry.compatible = Compatible(get("npp-compatible-versions")); entries.push_back(std::move(entry));
    }
    std::map<std::wstring,std::array<size_t,2>,PluginHost::ModuleLess> rows;
    for(size_t i=0;i<entries.size();++i)rows.emplace(entries[i].folder,std::array<size_t,2>{i,i});
    std::map<std::wstring,size_t,PluginHost::ModuleLess> loadedModules;
    for(size_t i=0;i<frame->plugins.plugins.size();++i)loadedModules[frame->plugins.plugins[i].module]=i;
    auto scan = [&](bool disabled) {
        auto root = frame->plugins.home; if (disabled) root /= L"disabled";
        if (!std::filesystem::exists(root)) return;
        for (const auto& directory : std::filesystem::directory_iterator(root)) {
            if (!directory.is_directory()) continue;
            auto folder = directory.path().filename().wstring();
            auto dll = directory.path() / (folder + L".dll");
            if (!ValidPluginFolder(folder) || !std::filesystem::is_regular_file(dll)) continue;
            auto [row,inserted]=rows.try_emplace(folder,std::array<size_t,2>{SIZE_MAX,SIZE_MAX});
            auto index=row->second[disabled];
            if(index!=SIZE_MAX && entries[index].present && entries[index].disabled!=disabled)index=SIZE_MAX;
            const auto metadata = ReadPluginFileInfo(dll);
            if(index==SIZE_MAX){
                Entry e; e.folder = folder; e.name = metadata.name.empty() ? folder : metadata.name;
                index=entries.size();entries.push_back(std::move(e));
            }
            row->second[disabled]=index;auto* found=&entries[index];
            found->present = true; found->disabled = disabled; found->installed = metadata.version;
            if (found->description.empty()) found->description = metadata.description;
            if (found->author.empty()) found->author = metadata.author;
            if (found->homepage.empty()) found->homepage = metadata.homepage;
            found->copyright = metadata.copyright; found->license = metadata.license;
            if(!disabled){auto loaded=loadedModules.find(folder+L".dll");if(loaded!=loadedModules.end()){found->loaded=true;found->name=frame->plugins.plugins[loaded->second].name;}}
        }
    };
    scan(false); scan(true);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
}
void PluginAdmin::Show(MainFrame& owner) {
    frame = &owner;
    if (!IsWindow(hwnd)) {
        try { Load(); } catch (const std::exception& error) { owner.Error(error); return; }
        hwnd = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PLUGINSADMIN_DLG), owner, DialogProc, reinterpret_cast<LPARAM>(this));
    }
    if (hwnd) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
}
void PluginAdmin::Close() { if (IsWindow(hwnd)) DestroyWindow(hwnd); hwnd = nullptr; }
void PluginAdmin::Populate() {
    ListView_DeleteAllItems(list); visible.clear();
    page = TabCtrl_GetCurSel(tab);
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& e = entries[i]; bool show = false;
        switch (page) {
        case 0: show = !e.present && e.compatible; break;
        case 1: show = e.present && !e.disabled && e.loaded && e.compatible && !e.version.empty() && !e.installed.empty() && Version(e.installed) < Version(e.version); break;
        case 2: show = e.present && !e.disabled && e.loaded; break;
        case 3: show = (e.present && !e.disabled && !e.loaded) || (!e.present && !e.compatible); break;
        case 4: show = e.disabled; break;
        }
        if (!show) continue;
        auto row = static_cast<int>(visible.size()); visible.push_back(i);
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row; item.pszText = e.name.data(); ListView_InsertItem(list, &item);
        auto version = (page == 0 || page == 1 || !e.present) ? e.version : e.installed;
        ListView_SetItemText(list, row, 1, version.data());
    }
    ShowWindow(GetDlgItem(hwnd, IDC_PLUGINADM_INSTALL), page == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_PLUGINADM_UPDATE), page == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_PLUGINADM_REMOVE), page >= 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_PLUGINADM_DEACTIVATE), page == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, IDC_PLUGINADM_ACTIVATE), page == 4 ? SW_SHOW : SW_HIDE);
    SetDlgItemTextW(hwnd, IDC_PLUGINADM_EDIT, AdminText(IDS_PLUGINADM_SELECT_HINT).c_str());
    if (!visible.empty()) ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}
void PluginAdmin::Describe() {
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0 || static_cast<size_t>(row) >= visible.size()) return;
    const auto& e = entries[visible[row]];
    auto text = e.description;
    auto append = [&](UINT label, const std::wstring& value) {
        if (value.empty()) return;
        if (!text.empty()) text += L"\r\n";
        text += AdminText(label); text += value;
    };
    append(IDS_PLUGINADM_NAME_FIELD, e.name);
    append(IDS_PLUGINADM_VERSION_FIELD, e.present ? e.installed : e.version);
    append(IDS_PLUGINADM_AUTHOR_FIELD, e.author);
    append(IDS_PLUGINADM_HOMEPAGE_FIELD, e.homepage);
    append(IDS_PLUGINADM_COPYRIGHT_FIELD, e.copyright);
    append(IDS_PLUGINADM_LICENSE_FIELD, e.license);
    if (e.present && !e.loaded && !e.disabled) {
        text += L"\r\n"; text += AdminText(IDS_PLUGINADM_NOT_LOADED);
        for (auto& error : frame->plugins.errors) if (error.starts_with(e.folder + L".dll:")) text += error;
    }
    SetDlgItemTextW(hwnd, IDC_PLUGINADM_EDIT, text.c_str());
}
void PluginAdmin::Search() {
    wchar_t query[512]{}; GetDlgItemTextW(hwnd, IDC_PLUGINADM_SEARCH_EDIT, query, _countof(query));
    auto needle = Lower(query); if (needle.empty() || visible.empty()) return;
    int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    for (size_t offset = 1; offset <= visible.size(); ++offset) {
        auto row = (static_cast<size_t>(selected + 1) + offset - 1) % visible.size();
        const auto& e = entries[visible[row]];
        if (Lower(e.name + L" " + e.description).find(needle) == std::wstring::npos) continue;
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(list, static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, static_cast<int>(row), FALSE); return;
    }
    MessageBeep(MB_ICONINFORMATION);
}
void PluginAdmin::Apply(int command) {
    if (frame->runtime.operationDepth || frame->pendingPluginMaintenance || frame->appUpdater.ready) return;
    FrameOperation operationGuard(frame->runtime, frame->m_hWnd);
    const auto operation = command == IDC_PLUGINADM_INSTALL ? PluginOperation::Install :
        command == IDC_PLUGINADM_UPDATE ? PluginOperation::Update :
        command == IDC_PLUGINADM_REMOVE ? PluginOperation::Remove :
        command == IDC_PLUGINADM_DEACTIVATE ? PluginOperation::Disable : PluginOperation::Enable;
    std::vector<PluginPackage> selected;
    for (size_t row = 0; row < visible.size(); ++row) if (ListView_GetCheckState(list, static_cast<int>(row))) {
        const auto& e = entries[visible[row]];
        if ((operation == PluginOperation::Remove || operation == PluginOperation::Disable ||
            operation == PluginOperation::Enable) && !e.present) continue;
        selected.push_back({e.folder, e.url, e.hash, e.disabled});
    }
    const auto title = AdminText(IDS_PLUGINADM_TITLE);
    const auto language = GetThreadUILanguage();
    if (selected.empty()) {
        MessageBoxExW(hwnd, AdminText(IDS_PLUGINADM_CHECK_FIRST).c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION, language);
        return;
    }
    if (MessageBoxExW(hwnd, AdminText(IDS_PLUGINADM_RESTART).c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION, language) != IDYES) return;
    // Resolve save/cancel decisions before scheduling plugin maintenance.
    for (int i = 0; i < frame->tabs.GetPageCount(); ++i) {
        auto* doc = static_cast<EditorDocument*>(frame->tabs.GetPageData(i));
        if (!frame->ConfirmClose(*doc)) return;
    }
    auto task = CreatePluginTaskDirectory();
    SaveRelaunchSession(*frame, task / L"session.ini", true);
    frame->pendingPluginMaintenance.emplace(PluginMaintenance{operation, frame->plugins.home, task, std::move(selected)});
    if (!frame->PostMessage(WM_CLOSE)) {
        frame->pendingPluginMaintenance.reset();
        throw std::runtime_error("Could not close MultiPad4 for plugin maintenance.");
    }
}
INT_PTR CALLBACK PluginAdmin::DialogProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<PluginAdmin*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) { self = reinterpret_cast<PluginAdmin*>(l); self->hwnd = window; SetWindowLongPtrW(window, DWLP_USER, l); }
    if (!self) return FALSE;
    try {
        if (message == WM_INITDIALOG) {
            SetWindowTextW(window, AdminText(IDS_PLUGINADM_TITLE).c_str());
            constexpr std::pair<int, UINT> labels[] = {
                {IDC_PLUGINADM_SEARCH_STATIC, IDS_PLUGINADM_SEARCH}, {IDC_PLUGINADM_RESEARCH_NEXT, IDS_PLUGINADM_NEXT},
                {IDC_PLUGINADM_INSTALL, IDS_PLUGINADM_INSTALL}, {IDC_PLUGINADM_UPDATE, IDS_PLUGINADM_UPDATE},
                {IDC_PLUGINADM_REMOVE, IDS_PLUGINADM_REMOVE}, {IDC_PLUGINADM_DEACTIVATE, IDS_PLUGINADM_DISABLE},
                {IDC_PLUGINADM_ACTIVATE, IDS_PLUGINADM_ENABLE}, {IDCANCEL, IDS_PLUGINADM_CLOSE},
                {IDC_PLUGINLIST_VERSION_STATIC, IDS_PLUGINADM_CATALOG_VERSION}, {IDC_PLUGINLIST_ADDR, IDS_PLUGINADM_CATALOG_LINK}
            };
            for (const auto& [control, label] : labels) SetDlgItemTextW(window, control, AdminText(label).c_str());
            auto link = GetDlgItem(window, IDC_PLUGINLIST_ADDR);
            SetWindowLongPtrW(link, GWL_STYLE, GetWindowLongPtrW(link, GWL_STYLE) | SS_NOTIFY);
            RECT rect{8, 40, 492, 160}; MapDialogRect(window, &rect);
            self->tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, window, reinterpret_cast<HMENU>(5514), GetModuleHandleW(nullptr), nullptr);
            constexpr UINT tabs[] = {IDS_PLUGINADM_AVAILABLE, IDS_PLUGINADM_UPDATES, IDS_PLUGINADM_INSTALLED, IDS_PLUGINADM_INCOMPATIBLE, IDS_PLUGINADM_DISABLED};
            for (int index = 0; index < static_cast<int>(_countof(tabs)); ++index) {
                auto label = AdminText(tabs[index]);
                TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = label.data();
                TabCtrl_InsertItem(self->tab, index, &item);
            }
            RECT body{}; GetClientRect(self->tab, &body); TabCtrl_AdjustRect(self->tab, FALSE, &body); MapWindowPoints(self->tab, window, reinterpret_cast<POINT*>(&body), 2);
            self->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, body.left, body.top, body.right - body.left, body.bottom - body.top, window, reinterpret_cast<HMENU>(IDC_PLUGINADM_LISTVIEW), GetModuleHandleW(nullptr), nullptr);
            auto font = SendMessageW(window, WM_GETFONT, 0, 0); SendMessageW(self->tab, WM_SETFONT, font, TRUE); SendMessageW(self->list, WM_SETFONT, font, TRUE);
            ListView_SetExtendedListViewStyle(self->list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            auto pluginLabel = AdminText(IDS_PLUGINADM_PLUGIN), versionLabel = AdminText(IDS_PLUGINADM_VERSION);
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.cx = (body.right - body.left) * 3 / 4; column.pszText = pluginLabel.data(); ListView_InsertColumn(self->list, 0, &column);
            column.cx = (body.right - body.left) / 4 - GetSystemMetrics(SM_CXVSCROLL); column.pszText = versionLabel.data(); ListView_InsertColumn(self->list, 1, &column);
            SetDlgItemTextW(window, IDC_PLUGINLIST_VERSIONNUMBER_STATIC, self->catalogVersion.c_str());
            self->Populate(); return TRUE;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) { ShowWindow(window, SW_HIDE); return TRUE; }
        if (message == WM_NCDESTROY) { self->hwnd = nullptr; self->list = nullptr; self->tab = nullptr; }
        if (message == WM_NOTIFY) {
            auto* info = reinterpret_cast<NMHDR*>(l);
            if (info->hwndFrom == self->tab && info->code == TCN_SELCHANGE) self->Populate();
            if (info->hwndFrom == self->list && info->code == LVN_ITEMCHANGED) self->Describe();
        }
        if (message == WM_COMMAND) {
            int id = LOWORD(w);
            if (id == IDC_PLUGINLIST_ADDR && HIWORD(w) == STN_CLICKED)
                ShellExecuteW(window, L"open", L"https://github.com/notepad-plus-plus/nppPluginList", nullptr, nullptr, SW_SHOWNORMAL);
            if (id == IDC_PLUGINADM_RESEARCH_NEXT || id == IDOK) self->Search();
            if (id == IDC_PLUGINADM_SEARCH_EDIT && HIWORD(w) == EN_CHANGE) self->Search();
            if (id == IDC_PLUGINADM_INSTALL || id == IDC_PLUGINADM_UPDATE || id == IDC_PLUGINADM_REMOVE || id == IDC_PLUGINADM_ACTIVATE || id == IDC_PLUGINADM_DEACTIVATE) self->Apply(id);
        }
    } catch (const std::exception& error) { self->frame->Error(error); }
    return FALSE;
}

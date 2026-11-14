// Adapted from Notepad++ PluginsAdmin (Copyright (C)2021 Don HO).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainFrame.h"
#include "PluginAdmin.h"
#include "Session.h"
#include "../3rdparty/nlohmann-json/json.hpp"
#include <fstream>
#include <array>
#include <set>
#include <cwctype>
using nlohmann::json;
namespace {
std::string Resource(UINT id) {
    auto instance = _Module.GetModuleInstance();
    auto resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    auto data = LoadResource(instance, resource);
    if (!resource || !data) throw std::runtime_error("Plugin administration resource missing.");
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
bool ValidPluginFolder(const std::wstring& name) {
    if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ') return false;
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return false;
    for (auto ch : name) if (ch < 32) return false;
    return _wcsicmp(name.c_str(), L"Config") && _wcsicmp(name.c_str(), L"disabled") && _wcsicmp(name.c_str(), L"APIs");
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
    if (catalog.value("arch", "") != "64") throw std::runtime_error("Plugin catalogue must be x64.");
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
    auto scan = [&](bool disabled) {
        auto root = frame->plugins.home; if (disabled) root /= L"disabled";
        if (!std::filesystem::exists(root)) return;
        for (const auto& directory : std::filesystem::directory_iterator(root)) {
            if (!directory.is_directory()) continue;
            auto folder = directory.path().filename().wstring();
            auto dll = directory.path() / (folder + L".dll");
            if (!ValidPluginFolder(folder) || !std::filesystem::is_regular_file(dll)) continue;
            auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return _wcsicmp(e.folder.c_str(), folder.c_str()) == 0 && (!e.present || e.disabled == disabled); });
            const auto metadata = ReadPluginFileInfo(dll);
            if (found == entries.end()) {
                Entry e; e.folder = folder; e.name = metadata.name.empty() ? folder : metadata.name;
                entries.push_back(std::move(e)); found = entries.end() - 1;
            }
            found->present = true; found->disabled = disabled; found->installed = metadata.version;
            if (found->description.empty()) found->description = metadata.description;
            if (found->author.empty()) found->author = metadata.author;
            if (found->homepage.empty()) found->homepage = metadata.homepage;
            found->copyright = metadata.copyright; found->license = metadata.license;
            for (const auto& plugin : frame->plugins.plugins) if (_wcsicmp(plugin.module.c_str(), (folder + L".dll").c_str()) == 0 && !disabled) { found->loaded = true; found->name = plugin.name; }
        }
    };
    scan(false); scan(true);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
}
void PluginAdmin::Show(MainFrame& owner) {
    frame = &owner;
    if (!IsWindow(hwnd)) {
        try { Load(); } catch (const std::exception& error) { owner.Error(error); return; }
        hwnd = CreateDialogParamW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDD_PLUGINSADMIN_DLG), owner, DialogProc, reinterpret_cast<LPARAM>(this));
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
    SetDlgItemTextW(hwnd, IDC_PLUGINADM_EDIT, L"请选择插件查看说明；勾选插件后可执行操作。");
    if (!visible.empty()) ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}
void PluginAdmin::Describe() {
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0 || static_cast<size_t>(row) >= visible.size()) return;
    const auto& e = entries[visible[row]];
    auto text = e.description;
    auto append = [&](const wchar_t* label, const std::wstring& value) {
        if (value.empty()) return;
        if (!text.empty()) text += L"\r\n";
        text += label; text += value;
    };
    append(L"名称：", e.name);
    append(L"版本：", e.present ? e.installed : e.version);
    append(L"作者：", e.author);
    append(L"主页：", e.homepage);
    append(L"版权：", e.copyright);
    append(L"许可：", e.license);
    if (e.present && !e.loaded && !e.disabled) {
        text += L"\r\n未加载：";
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
    if (frame->runtime.operationDepth) return;
    FrameOperation operationGuard(frame->runtime, frame->m_hWnd);
    std::string operation = command == IDC_PLUGINADM_INSTALL ? "install" : command == IDC_PLUGINADM_UPDATE ? "update" : command == IDC_PLUGINADM_REMOVE ? "remove" : command == IDC_PLUGINADM_DEACTIVATE ? "disable" : "enable";
    json selected = json::array();
    for (size_t row = 0; row < visible.size(); ++row) if (ListView_GetCheckState(list, static_cast<int>(row))) {
        const auto& e = entries[visible[row]];
        if ((operation == "remove" || operation == "disable" || operation == "enable") && !e.present) continue;
        selected.push_back({{"folder", files::ToUtf8(e.folder)}, {"disabled", e.disabled}, {"url", files::ToUtf8(e.url)}, {"hash", files::ToUtf8(e.hash)}});
    }
    if (selected.empty()) { MessageBoxW(hwnd, L"请先勾选要操作的插件。", L"插件管理", MB_OK | MB_ICONINFORMATION); return; }
    if (MessageBoxW(hwnd, L"此操作需要退出并重新启动 MultiPad4。是否继续？", L"插件管理", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    // Resolve all save/cancel decisions before handing work to another process.
    for (int i = 0; i < frame->tabs.GetPageCount(); ++i) {
        auto* doc = static_cast<EditorDocument*>(frame->tabs.GetPageData(i));
        if (!frame->ConfirmClose(*doc)) return;
    }
    auto task = std::filesystem::temp_directory_path() / (L"MultiPad4-plugins-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(task);
    auto session = task / L"session.ini";
    SaveRelaunchSession(*frame, session, true);
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, _countof(exe));
    json plan{{"pid", GetCurrentProcessId()}, {"exe", files::ToUtf8(exe)}, {"root", files::ToUtf8(frame->plugins.home.wstring())}, {"session", files::ToUtf8(session.wstring())}, {"operation", operation}, {"plugins", selected}};
    { std::ofstream output(task / L"plan.json", std::ios::binary); output << plan.dump(); if (!output) throw std::runtime_error("Cannot write plugin operation plan."); }
    { std::ofstream output(task / L"worker.ps1", std::ios::binary); output << Resource(IDR_PLUGIN_WORKER); if (!output) throw std::runtime_error("Cannot write plugin worker."); }
    wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, _countof(system));
    auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    auto args = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + (task / L"worker.ps1").wstring() + L"\" -PlanPath \"" + (task / L"plan.json").wstring() + L"\"";
    auto result = ShellExecuteW(hwnd, L"open", powershell.c_str(), args.c_str(), task.c_str(), SW_HIDE);
    if (reinterpret_cast<INT_PTR>(result) <= 32) throw std::runtime_error("Cannot start plugin worker.");
    for (auto& doc : frame->documents) { doc->Send(SCI_SETSAVEPOINT); doc->encodingDirty = false; }
    ShowWindow(hwnd, SW_HIDE); frame->PostMessage(WM_CLOSE);
}
INT_PTR CALLBACK PluginAdmin::DialogProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<PluginAdmin*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) { self = reinterpret_cast<PluginAdmin*>(l); self->hwnd = window; SetWindowLongPtrW(window, DWLP_USER, l); }
    if (!self) return FALSE;
    try {
        if (message == WM_INITDIALOG) {
            SetWindowTextW(window, L"插件管理");
            const std::pair<int, const wchar_t*> labels[] = {
                {IDC_PLUGINADM_SEARCH_STATIC, L"搜索(&S):"}, {IDC_PLUGINADM_RESEARCH_NEXT, L"下一个(&N)"},
                {IDC_PLUGINADM_INSTALL, L"安装(&I)"}, {IDC_PLUGINADM_UPDATE, L"更新(&U)"},
                {IDC_PLUGINADM_REMOVE, L"移除(&R)"}, {IDC_PLUGINADM_DEACTIVATE, L"禁用(&D)"},
                {IDC_PLUGINADM_ACTIVATE, L"启用(&A)"}, {IDCANCEL, L"关闭"},
                {IDC_PLUGINLIST_VERSION_STATIC, L"插件目录版本:"}, {IDC_PLUGINLIST_ADDR, L"插件目录仓库"}
            };
            for (const auto& label : labels) SetDlgItemTextW(window, label.first, label.second);
            auto link = GetDlgItem(window, IDC_PLUGINLIST_ADDR);
            SetWindowLongPtrW(link, GWL_STYLE, GetWindowLongPtrW(link, GWL_STYLE) | SS_NOTIFY);
            RECT rect{8, 40, 492, 160}; MapDialogRect(window, &rect);
            self->tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, window, reinterpret_cast<HMENU>(5514), _Module.GetModuleInstance(), nullptr);
            const wchar_t* names[] = {L"可用", L"更新", L"已安装", L"不兼容 / 加载失败", L"已禁用"};
            for (int i = 0; i < 5; ++i) { TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(names[i]); TabCtrl_InsertItem(self->tab, i, &item); }
            RECT body{}; GetClientRect(self->tab, &body); TabCtrl_AdjustRect(self->tab, FALSE, &body); MapWindowPoints(self->tab, window, reinterpret_cast<POINT*>(&body), 2);
            self->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, body.left, body.top, body.right - body.left, body.bottom - body.top, window, reinterpret_cast<HMENU>(IDC_PLUGINADM_LISTVIEW), _Module.GetModuleInstance(), nullptr);
            auto font = SendMessageW(window, WM_GETFONT, 0, 0); SendMessageW(self->tab, WM_SETFONT, font, TRUE); SendMessageW(self->list, WM_SETFONT, font, TRUE);
            ListView_SetExtendedListViewStyle(self->list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.cx = (body.right - body.left) * 3 / 4; column.pszText = const_cast<wchar_t*>(L"插件"); ListView_InsertColumn(self->list, 0, &column);
            column.cx = (body.right - body.left) / 4 - GetSystemMetrics(SM_CXVSCROLL); column.pszText = const_cast<wchar_t*>(L"版本"); ListView_InsertColumn(self->list, 1, &column);
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

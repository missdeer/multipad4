#pragma once
#include "MainFrame.h"
#include "QAArtifacts.h"
#include <fstream>
inline void RunPluginFeatureTests(MainFrame& frame) {
    auto& host = frame.plugins;
    std::ofstream out(host.config / L"MultiPad4-plugin-test.log");
    auto require = [&](bool ok, const char* description) {
        out << (ok ? "PASS " : "FAIL ") << description << std::endl;
        if (!ok) throw std::runtime_error(description);
    };
    auto openClipboard = [&]() {
        for (int attempt = 0; attempt < 50; ++attempt) { if (::OpenClipboard(frame)) return true; Sleep(10); }
        return false;
    };
    auto command = [&](const wchar_t* module, const wchar_t* name) {
        for (auto& plugin : host.plugins) if (_wcsicmp(plugin.module.c_str(), module) == 0)
            for (int i = 0; i < plugin.count; ++i) if (wcscmp(plugin.items[i]._itemName, name) == 0) {
                out << "RUN " << files::ToUtf8(module) << " / " << files::ToUtf8(name) << std::endl;
                require(host.Execute(plugin.items[i]._cmdID), "dispatch plugin command"); return;
            }
        throw std::runtime_error("Required plugin command missing");
    };
    require(host.plugins.size() >= 5 && host.errors.empty(), "load all five supplied plugins");
    const auto savedCommand = host.nextCommand;
    host.nextCommand = IDM_SETTING_OPENPLUGINSDIR - 1;
    int allocatedCommand = 0;
    require(host.Message(NPPM_ALLOCATECMDID, 3, reinterpret_cast<LPARAM>(&allocatedCommand)) &&
        allocatedCommand == static_cast<int>(IDM_SETTING_PLUGINADM + 1) && host.nextCommand == IDM_SETTING_PLUGINADM + 4,
        "dynamic plugin command blocks skip admin menu IDs");
    host.nextCommand = savedCommand;
    require(GetMenuState(host.menu, IDM_SETTING_PLUGINADM, MF_BYCOMMAND) != static_cast<UINT>(-1), "plugin admin menu exists");
    frame.SendMessage(WM_COMMAND, IDM_SETTING_PLUGINADM);
    auto& admin = frame.pluginAdmin;
    require(IsWindow(admin.hwnd) && IsWindowVisible(admin.hwnd), "plugin admin command opens modeless dialog");
    require(TabCtrl_GetItemCount(admin.tab) == 5 && !admin.entries.empty(), "plugin admin loads catalogue and five tabs");
    require(!admin.visible.empty(), "available plugins shown");
    TabCtrl_SetCurSel(admin.tab, 2); admin.Populate();
    require(admin.visible.size() == host.plugins.size(), "installed plugins reflect loaded host");
    auto matepath = std::find_if(admin.entries.begin(), admin.entries.end(), [](const auto& entry) { return entry.folder == L"Matepath" && !entry.disabled; });
    if (matepath != admin.entries.end()) {
        require(!matepath->installed.empty() && !matepath->description.empty() && !matepath->author.empty()
            && !matepath->homepage.empty() && !matepath->copyright.empty() && !matepath->license.empty(), "local Matepath metadata comes from DLL resources");
        const auto entryIndex = static_cast<size_t>(matepath - admin.entries.begin());
        const auto visibleEntry = std::find(admin.visible.begin(), admin.visible.end(), entryIndex);
        require(visibleEntry != admin.visible.end(), "Matepath appears in installed plugins");
        ListView_SetItemState(admin.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(admin.list, static_cast<int>(visibleEntry - admin.visible.begin()), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        admin.Describe();
        wchar_t details[4096]{}; GetDlgItemTextW(admin.hwnd, IDC_PLUGINADM_EDIT, details, _countof(details));
        require(wcsstr(details, matepath->description.c_str()) && wcsstr(details, matepath->author.c_str())
            && wcsstr(details, matepath->homepage.c_str()) && wcsstr(details, matepath->license.c_str()), "plugin details show local DLL metadata");
        qaArtifactDirectory = host.config / L"MultiPad4-QA";
        CaptureQAWindow(admin.hwnd, L"plugins-matepath-details");

        // Read a disabled, renamed local plugin without loading its executable code.
        const auto fixture = std::filesystem::temp_directory_path() / (L"MultiPad4-plugin-metadata-" + std::to_wstring(GetCurrentProcessId()));
        const auto savedHome = host.home;
        const auto savedConfig = host.config;
        try {
            const auto folder = fixture / L"disabled" / L"LocalBrowser";
            std::filesystem::create_directories(folder);
            std::filesystem::copy_file(savedHome / L"Matepath" / L"Matepath.dll", folder / L"LocalBrowser.dll", std::filesystem::copy_options::overwrite_existing);
            host.home = fixture;
            PluginAdmin offline; offline.frame = &frame; offline.Load();
            host.home = savedHome;
            const auto local = std::find_if(offline.entries.begin(), offline.entries.end(), [](const auto& entry) { return entry.folder == L"LocalBrowser"; });
            require(local != offline.entries.end() && local->disabled && !local->loaded && local->name == L"Matepath"
                && !local->description.empty() && !local->author.empty()
                && !local->homepage.empty() && local->installed == matepath->installed, "disabled plugin metadata is read from disk");
            const auto catalogFolder = fixture / L"CatalogBrowser";
            std::filesystem::create_directories(catalogFolder);
            std::filesystem::copy_file(savedHome / L"Matepath" / L"Matepath.dll", catalogFolder / L"CatalogBrowser.dll", std::filesystem::copy_options::overwrite_existing);
            std::filesystem::create_directories(fixture / L"Config");
            std::ofstream(fixture / L"Config" / L"nppPluginList.json") << R"({"arch":"64","version":"test","npp-plugins":[{"folder-name":"CatalogBrowser","display-name":"Catalogue Browser","description":"Catalogue description","author":"Catalogue author","homepage":"https://example.com/catalogue"}]})";
            host.home = fixture; host.config = fixture / L"Config";
            offline.Load();
            host.home = savedHome; host.config = savedConfig;
            const auto catalogEntry = std::find_if(offline.entries.begin(), offline.entries.end(), [](const auto& entry) { return entry.folder == L"CatalogBrowser"; });
            require(catalogEntry != offline.entries.end() && catalogEntry->name == L"Catalogue Browser"
                && catalogEntry->description == L"Catalogue description" && catalogEntry->author == L"Catalogue author"
                && catalogEntry->homepage == L"https://example.com/catalogue", "catalogue metadata takes precedence over DLL fallback");
            std::filesystem::remove_all(fixture);
        } catch (...) {
            host.home = savedHome; host.config = savedConfig;
            std::error_code cleanupError; std::filesystem::remove_all(fixture, cleanupError);
            throw;
        }
    }
    SetDlgItemTextW(admin.hwnd, IDC_PLUGINADM_SEARCH_EDIT, L"JsonTools"); admin.Search();
    int row = ListView_GetNextItem(admin.list, -1, LVNI_SELECTED);
    require(row >= 0 && admin.entries[admin.visible[row]].folder == L"JsonTools", "plugin admin searches names");
    qaArtifactDirectory = host.config / L"MultiPad4-QA";
    CaptureQAWindow(admin.hwnd, L"plugins-admin-installed");
    auto originalDialog = admin.hwnd;
    ::SendMessageW(admin.hwnd, WM_COMMAND, IDCANCEL, 0);
    require(!IsWindowVisible(admin.hwnd), "plugin admin close hides dialog");
    frame.SendMessage(WM_COMMAND, IDM_SETTING_PLUGINADM);
    require(admin.hwnd == originalDialog && IsWindowVisible(admin.hwnd), "plugin admin reuses existing dialog");
    admin.Close();
    RunMatepathInputRegression(frame);
    auto* first = frame.Active(); auto* second = frame.NewDocument();
    first->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("isolated"));
    auto text = [&](const char* value) { second->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(value)); second->Send(SCI_SELECTALL); };
    text("alpha beta");
    struct NppFind { Sci_CharacterRangeFull range; const char* text; Sci_CharacterRangeFull found; } find{{0, 10}, "beta", {0, 0}};
    require(host.SciMessage(SCI_FINDTEXTFULL, 0, reinterpret_cast<LPARAM>(&find)) == 6 && find.found.cpMin == 6 && find.found.cpMax == 10, "N++ FindTextFull ABI translation");
    std::vector<char> styled(22);
    Sci_TextRangeFull styledRange{{0, 10}, styled.data()};
    require(host.SciMessage(SCI_GETSTYLEDTEXTFULL, 0, reinterpret_cast<LPARAM>(&styledRange)) == 20 && styled[0] == 'a' && styled[12] == 'b', "N++ interleaved styled text ABI");
    text("Hello"); command(L"mimeTools.dll", L"Base64 Encode with padding"); out << "B64=" << second->Text() << std::endl; require(second->Text() == "SGVsbG8=", "mimeTools Base64 encode");
    second->Send(SCI_SELECTALL); command(L"mimeTools.dll", L"Base64 Decode"); require(second->Text() == "Hello", "mimeTools Base64 decode");
    text("ABC"); command(L"NppConverter.dll", L"ASCII -> HEX");
    out << "HEX=" << second->Text() << std::endl;
    require(second->Text().find("41") != std::string::npos, "NppConverter encode");
    second->Send(SCI_SELECTALL); command(L"NppConverter.dll", L"HEX -> ASCII"); require(second->Text() == "ABC", "NppConverter roundtrip");
    text("<root><child>value</child></root>"); command(L"XMLTools.dll", L"Pretty print");
    out << "XML=" << second->Text() << std::endl;
    require(second->Text().find('\n') != std::string::npos && second->Text().find("<child>value</child>") != std::string::npos, "XMLTools pretty print");
    text("{\"a\":1,\"b\":[2,3]}"); command(L"JsonTools.dll", L"&Pretty-print current JSON file");
    out << "JSON=" << second->Text() << std::endl;
    require(second->Text().find('\n') != std::string::npos && second->Text().find("\"a\"") != std::string::npos, "JsonTools pretty print");
    second->Send(SCI_SETSEL, 0, 0); command(L"JsonTools.dll", L"&Compress current JSON file");
    require(second->Text().find('\n') == std::string::npos, "JsonTools compress");
    second->Send(SCI_SELECTALL); command(L"NppExport.dll", L"&Copy HTML to clipboard");
    require(openClipboard(), "open export clipboard");
    const bool html = IsClipboardFormatAvailable(RegisterClipboardFormatW(L"HTML Format")) != FALSE;
    ::CloseClipboard(); require(html, "NppExport HTML clipboard format");
    command(L"NppExport.dll", L"&Copy RTF to clipboard");
    require(openClipboard(), "open RTF clipboard");
    const bool rtf = IsClipboardFormatAvailable(RegisterClipboardFormatW(L"Rich Text Format")) != FALSE;
    ::CloseClipboard(); require(rtf, "NppExport RTF clipboard format");
    require(first->Text() == "isolated", "plugin edits preserve background document");
    frame.ShowWindow(SW_SHOWNORMAL); frame.ShowWindow(SW_SHOWNORMAL); frame.UpdateWindow();
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == second) { frame.tabs.SetActivePage(i); frame.MDIMaximize(frame.tabs.GetPageHWND(i)); }
    frame.Refresh();
    command(L"JsonTools.dll", L"Open &JSON tree viewer");
    require(!host.panes.empty() && ::IsWindow(host.panes.back()->client), "JsonTools tree panel registration");
    require(host.panes.back()->IsWindowVisible(), "JsonTools tree panel visible");
    frame.ShowWindow(SW_SHOWNORMAL); frame.ShowWindow(SW_SHOWNORMAL); frame.UpdateWindow();
    qaArtifactDirectory = host.config / L"MultiPad4-QA";
    CaptureQAWindow(frame, L"plugins-json-tree");
    auto* panel = host.panes.back().get();
    host.Message(NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(panel->client));
    require(!::IsWindowVisible(panel->client), "hide plugin panel");
    host.Message(NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(panel->client));
    require(::IsWindowVisible(panel->client), "restore plugin panel");
    panel->SendMessage(WM_CLOSE);
    require(!::IsWindowVisible(panel->client), "close plugin panel");
    RunRepeatedDockRegistrationTests(frame);
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == first) frame.tabs.SetActivePage(i);
    frame.Refresh(); first->Send(SCI_SELECTALL); command(L"mimeTools.dll", L"Base64 Encode with padding");
    require(first->Text() == "aXNvbGF0ZWQ=", "cached Scintilla handle follows active MDI tab");
    first->Send(SCI_SETSAVEPOINT); second->Send(SCI_SETSAVEPOINT);
    out << "PASS all supplied plugin functional checks" << std::endl;
}

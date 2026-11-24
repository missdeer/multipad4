#pragma once
#include "FileDialogTests.h"
#include "../plugins/Project/Resources.h"

inline void RunProjectPluginTests(MainFrame& frame, EditorDocument& original,
                                  const std::filesystem::path& directory) {
    if (frame.plugins.onlyModule != L"__bundled__") return;
    namespace fs = std::filesystem;
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Project plugin integration failed at line " + std::to_string(where.line()));
    };
    const auto plugin = std::ranges::find_if(frame.plugins.plugins, [](const PluginHost::Plugin& candidate) {
        return candidate.module == L"Project.dll";
    });
    require(plugin != frame.plugins.plugins.end());
    const HMENU mainMenu = frame.GetMenu();
    const HMENU pluginsMenu = frame.plugins.menu;
    require(reinterpret_cast<HMENU>(frame.plugins.Message(NPPM_GETMENUHANDLE, NPPMAINMENU, 0)) == mainMenu);
    require(reinterpret_cast<HMENU>(frame.plugins.Message(NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0)) == pluginsMenu);
    const int menuCount = GetMenuItemCount(mainMenu);
    int position = 0;
    while (position < menuCount && GetSubMenu(mainMenu, position) != plugin->submenu) ++position;
    require(position + 1 < menuCount && GetSubMenu(mainMenu, position + 1) == pluginsMenu);
    wchar_t caption[menuItemSize]{};
    GetMenuStringW(mainMenu, position, caption, _countof(caption), MF_BYPOSITION);
    require(std::wstring_view(caption) == L"P&roject");
    for (int index = 0; index < GetMenuItemCount(pluginsMenu); ++index)
        require(GetSubMenu(pluginsMenu, index) != plugin->submenu);
    require(GetMenuItemCount(plugin->submenu) == plugin->count);
    auto findCommand = [&](std::wstring_view label) {
        for (int index = 0; index < plugin->count; ++index) {
            std::wstring text = plugin->items[index]._itemName;
            std::erase(text, L'&');
            if (label == text) return static_cast<UINT>(plugin->items[index]._cmdID);
        }
        throw std::runtime_error("Project plugin command missing");
    };
    auto execute = [&](std::wstring_view label) { require(frame.plugins.Execute(findCommand(label))); };

    struct Restore {
        PluginHost& host;
        fs::path config;
        HWND dialogOwner;
        ~Restore() { host.config = std::move(config); filedialogtest::owner = dialogOwner; }
    } restore{frame.plugins, frame.plugins.config, filedialogtest::owner};
    filedialogtest::owner = frame;
    const auto folder = directory / L"projects";
    frame.plugins.config = folder;
    fs::create_directories(folder / L"members" / L"nested");
    const auto source = folder / L"项目.txt", direct = folder / L"members" / L"direct.txt";
    const auto nested = folder / L"members" / L"nested" / L"nested.txt";
    const auto library = folder / L"Project.mp4projects";
    for (const auto& path : {source, direct, nested}) files::WriteAtomic(path, {"test"});

    execute(L"Project Pane");
    const auto pane = reinterpret_cast<HWND>(frame.plugins.Message(NPPM_DMMGETPLUGINHWNDBYNAME, PROJECT_PANE,
        reinterpret_cast<LPARAM>(L"Project.dll")));
    require(IsWindow(pane) && (GetWindowLongPtrW(pane, GWL_STYLE) & WS_VISIBLE));
    const auto selector = GetDlgItem(pane, PROJECT_PANE_SELECTOR);
    const auto memberList = GetDlgItem(pane, PROJECT_PANE_FILES);
    require(SendMessageW(selector, CB_GETCOUNT, 0, 0) == 1);
    require(!IsWindowEnabled(GetDlgItem(pane, PROJECT_PANE_OPEN)));

    const UINT addCurrent = findCommand(L"Add Current File");
    auto openProjectMenu = [&] { frame.SendMessage(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(plugin->submenu), position); };
    openProjectMenu();
    require(GetMenuState(plugin->submenu, findCommand(L"Project Pane"), MF_BYCOMMAND) & MF_CHECKED);
    require((GetMenuState(plugin->submenu, addCurrent, MF_BYCOMMAND) & MF_GRAYED) != 0);
    require(!frame.plugins.Execute(addCurrent));
    require(frame.Open(source));
    const auto sourceId = frame.plugins.Buffer(frame.Active());
    openProjectMenu();
    require(GetMenuState(plugin->submenu, addCurrent, MF_BYCOMMAND) == MF_ENABLED);
    CaptureQAWindow(frame, L"Project-main-menu-bar");
    CaptureQAPopup(frame, plugin->submenu, L"Project-main-menu");
    frame.SendMessage(WM_COMMAND, addCurrent);
    require(SendMessageW(memberList, LB_GETCOUNT, 0, 0) == 1);
    const auto saved = files::ReadWithEncoding(library, files::Encoding::Utf8).utf8;
    require(saved.find(files::ToUtf8(L"\"项目.txt\"")) != std::string::npos);
    execute(L"Add Current File");
    require(saved == files::ReadWithEncoding(library, files::Encoding::Utf8).utf8);
    execute(L"Remove Current File");
    require(SendMessageW(memberList, LB_GETCOUNT, 0, 0) == 0);
    require(files::ReadWithEncoding(library, files::Encoding::Utf8).utf8.find(files::ToUtf8(L"项目.txt")) == std::string::npos);

    files::WriteAtomic(library, {R"({"version":1,"active":0,"description":"Smoke test library","projects":[
        {"name":"Default","base":"","autoLoad":false,"members":[]},
        {"name":"示例项目","base":"members","autoLoad":true,"members":[
            {"path":"项目.txt","folder":false},{"path":"members","folder":true}]}]})"});
    require(frame.Open(nested));
    const auto nestedId = frame.plugins.Buffer(frame.Active());
    const int count = frame.tabs.GetPageCount();
    execute(L"Next Project");
    require(frame.tabs.GetPageCount() == count + 1 && frame.Active()->path == direct);
    const auto directId = frame.plugins.Buffer(frame.Active());
    require(SendMessageW(selector, CB_GETCURSEL, 0, 0) == 1 && SendMessageW(memberList, LB_GETCOUNT, 0, 0) == 2);
    SendMessageW(selector, CB_SETCURSEL, 0, 0);
    SendMessageW(pane, WM_COMMAND, MAKEWPARAM(PROJECT_PANE_SELECTOR, CBN_SELENDOK), reinterpret_cast<LPARAM>(selector));
    require(SendMessageW(selector, CB_GETCURSEL, 0, 0) == 0 && SendMessageW(memberList, LB_GETCOUNT, 0, 0) == 0);
    SendMessageW(selector, CB_SETCURSEL, 1, 0);
    SendMessageW(pane, WM_COMMAND, MAKEWPARAM(PROJECT_PANE_SELECTOR, CBN_SELENDOK), reinterpret_cast<LPARAM>(selector));
    require(SendMessageW(selector, CB_GETCURSEL, 0, 0) == 1 && SendMessageW(memberList, LB_GETCOUNT, 0, 0) == 2);
    SendMessageW(memberList, LB_SETCURSEL, 0, 0);
    MSG enter{memberList, WM_KEYDOWN, VK_RETURN};
    require(frame.plugins.PreTranslate(&enter) && frame.Active()->path == source);
    SendMessageW(memberList, LB_SETCURSEL, 1, 0);
    SendMessageW(pane, WM_COMMAND, MAKEWPARAM(PROJECT_PANE_FILES, LBN_SELCHANGE), reinterpret_cast<LPARAM>(memberList));
    require(IsWindowEnabled(GetDlgItem(pane, PROJECT_PANE_OPEN)));
    SendMessageW(pane, WM_COMMAND, MAKEWPARAM(PROJECT_PANE_FILES, LBN_DBLCLK), reinterpret_cast<LPARAM>(memberList));
    require(frame.Active()->path == direct);
    CaptureQAWindow(frame, L"Project-docked-pane");
    ::SendMessageW(GetParent(pane), WM_CLOSE, 0, 0);
    require(!(GetWindowLongPtrW(pane, GWL_STYLE) & WS_VISIBLE));
    execute(L"Project Pane");
    require(GetWindowLongPtrW(pane, GWL_STYLE) & WS_VISIBLE);
    execute(L"Project Pane");
    require(!(GetWindowLongPtrW(pane, GWL_STYLE) & WS_VISIBLE));
    openProjectMenu();
    require(GetMenuState(plugin->submenu, findCommand(L"Project Pane"), MF_BYCOMMAND) == MF_UNCHECKED);

    bool shown = false;
    filedialogtest::Run([&](HWND dialog) {
        shown = true;
        require(SendDlgItemMessageW(dialog, PROJECT_LIST, LB_GETCOUNT, 0, 0) == 2);
        require(SendDlgItemMessageW(dialog, PROJECT_MEMBERS, LB_GETCOUNT, 0, 0) == 2);
        CaptureQAWindow(dialog, L"Project manager");
        SendDlgItemMessageW(dialog, PROJECT_MEMBERS, LB_SETCURSEL, 0, 0);
        SendMessageW(dialog, WM_COMMAND, PROJECT_REMOVE_MEMBER, 0);
        require(SendDlgItemMessageW(dialog, PROJECT_MEMBERS, LB_GETCOUNT, 0, 0) == 1);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }, [&] { execute(L"Edit Projects..."); });
    require(shown);

    frame.Active()->Send(SCI_ADDTEXT, 1, reinterpret_cast<LPARAM>("!"));
    shown = false;
    filedialogtest::Run([&](HWND dialog) {
        shown = true;
        require(GetDlgItem(dialog, IDCANCEL) != nullptr);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }, [&] { execute(L"Close Project"); });
    auto* modified = frame.plugins.Document(directId);
    require(shown && modified && modified->Modified() && frame.plugins.Document(sourceId));
    modified->Send(SCI_SETSAVEPOINT);
    execute(L"Close Project");
    require(!frame.plugins.Document(directId) && !frame.plugins.Document(sourceId) && frame.plugins.Document(nestedId));

    shown = false;
    filedialogtest::Run([&](HWND dialog) {
        shown = true;
        require(SendDlgItemMessageW(dialog, PROJECT_LIST, LB_GETCOUNT, 0, 0) == 2);
        PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    }, [&] { execute(L"Project Files..."); });
    require(shown && frame.Active()->path == source);
    execute(L"Close Project");
    const auto nestedPosition = frame.plugins.Message(NPPM_GETPOSFROMBUFFERID, nestedId, 0);
    require(nestedPosition >= 0 && frame.Close(static_cast<int>(nestedPosition)));
    const auto originalPosition = frame.plugins.Message(NPPM_GETPOSFROMBUFFERID, frame.plugins.Buffer(&original), 0);
    require(originalPosition >= 0);
    frame.tabs.SetActivePage(static_cast<int>(originalPosition));
    frame.Refresh();
}

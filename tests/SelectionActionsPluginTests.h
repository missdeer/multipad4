#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>
#include <stdexcept>

inline void RunSelectionActionsPluginTests(MainFrame& frame, EditorDocument& document,
                                           const std::filesystem::path& directory) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Action on Selection plugin assertion at line " + std::to_string(where.line()));
    };
    for (const UINT original : {CMD_OPEN_PATH_OR_LINK, CMD_OPEN_CONTAINING_FOLDER, CMD_CALCULATE_EXPR,
                                CMD_ONLINE_SEARCH_GOOGLE, CMD_ONLINE_SEARCH_BING, CMD_ONLINE_SEARCH_WIKI,
                                CMD_CUSTOM_ACTION1, CMD_CUSTOM_ACTION2}) {
        require(GetMenuState(frame.GetMenu(), original, MF_BYCOMMAND) == UINT(-1));
        require(!frame.State(original).supported);
    }
    const auto plugin = std::find_if(frame.plugins.plugins.begin(), frame.plugins.plugins.end(),
        [](const PluginHost::Plugin& candidate) { return candidate.module == L"SelectionActions.dll"; });
    const bool installed = frame.plugins.onlyModule == L"__bundled__";
    require((plugin != frame.plugins.plugins.end()) == installed);
    UniqueMenu root(LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_POPUPMENU)));
    const HMENU context = GetSubMenu(root.get(), 0);
    require(GetMenuState(context, CMD_OPEN_PATH_OR_LINK, MF_BYCOMMAND) == UINT(-1));
    const int contextItems = GetMenuItemCount(context);
    document.Load({"選択.txt:2:2"});
    document.Send(SCI_GOTOPOS, 3);
    frame.plugins.PopulateEditorContextMenu(context);
    require(GetMenuItemCount(context) == contextItems + (installed ? 2 : 0));
    if (!installed) return;

    require(plugin->name == L"Action &on Selection" && plugin->count == 11);
    auto command = [&](int position) { return GetMenuItemID(plugin->submenu, position); };
    auto enabled = [&](int position) {
        frame.SendMessage(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(plugin->submenu));
        return !(GetMenuState(plugin->submenu, command(position), MF_BYCOMMAND) & MF_GRAYED);
    };
    require(command(2) == 0 && command(4) == 0 && command(8) == 0);
    require(enabled(0) && enabled(1) && !enabled(3) && !enabled(5));
    const UINT contextCommand = GetMenuItemID(context, contextItems + 1);
    require(contextCommand == command(0) && GetMenuState(context, contextCommand, MF_BYCOMMAND) == MF_ENABLED);
    wchar_t label[32]{};
    require(GetMenuStringW(context, contextCommand, label, _countof(label), MF_BYCOMMAND) > 0 &&
        std::wstring_view(label) == L"&Open");
    const auto target = directory / L"選択.txt";
    files::WriteAtomic(target, {"first\nsecond"});
    const auto originalPath = document.path;
    const int originalTab = frame.tabs.GetActivePage();
    document.path = directory / L"reference.txt";
    frame.SendMessage(WM_COMMAND, contextCommand);
    auto* opened = frame.Active();
    require(opened && opened->path == target && opened->Send(SCI_GETCURRENTPOS) == 7);
    require(frame.Close(frame.tabs.GetActivePage()));
    frame.tabs.SetActivePage(originalTab);
    document.path = originalPath;
    require(frame.Active() == &document);

    document.Load({"sqrt(9)+1"});
    document.Send(SCI_SELECTALL);
    const auto custom = frame.settings.ReadString(L"Tools", L"CustomAction1");
    frame.settings.WriteString(L"Tools", L"CustomAction1", L"");
    require(!enabled(9));
    frame.settings.WriteString(L"Tools", L"CustomAction1", L"example.exe %s");
    require(enabled(9) && enabled(5) && enabled(3));
    frame.settings.WriteString(L"Tools", L"CustomAction1", custom);
    ACCEL shortcuts[32]{};
    const int count = CopyAcceleratorTableW(frame.plugins.accelerators, shortcuts, _countof(shortcuts));
    for (int position : {9, 10}) {
        require(std::any_of(shortcuts, shortcuts + count, [&](const ACCEL& shortcut) {
            return shortcut.cmd == command(position) && shortcut.key == '1' + position - 9 &&
                (shortcut.fVirt & (FVIRTKEY | FCONTROL | FSHIFT | FALT)) == (FVIRTKEY | FCONTROL | FSHIFT);
        }));
    }
    CaptureQAPopup(frame, plugin->submenu, L"Action-on-Selection-plugin-menu");
    CLSID scriptClass{};
    if (SUCCEEDED(CLSIDFromProgID(L"JavaScript", &scriptClass))) {
        frame.SendMessage(WM_COMMAND, command(3));
        require(document.Text() == "sqrt(9)+1 4");
        document.Send(SCI_UNDO);
        require(document.Text() == "sqrt(9)+1");
        document.Send(SCI_SELECTALL);
    }
    document.Send(SCI_SETREADONLY, TRUE);
    require(!enabled(3) && enabled(5));
    document.Send(SCI_SETREADONLY, FALSE);
}

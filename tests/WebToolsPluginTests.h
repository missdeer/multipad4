#pragma once
#include "../src/MainFrame.h"
#include "../src/StyleManager.h"
#include "QAArtifacts.h"
#include <source_location>
#include <stdexcept>

inline void RunWebToolsPluginTests(MainFrame& frame, EditorDocument& document) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Web Tools plugin assertion at line " + std::to_string(where.line()));
    };
    for (UINT original : {IDM_EDIT_CODE_COMPRESS, IDM_EDIT_CODE_PRETTY, CMD_EVALUATE_JS_EXPR,
                          IDM_EDIT_XHTML_ESCAPE_CHAR, IDM_EDIT_XHTML_UNESCAPE_CHAR,
                          IDM_EDIT_URLENCODE, IDM_EDIT_URLCOMPONENTENCODE, IDM_EDIT_URLDECODE}) {
        require(GetMenuState(frame.GetMenu(), original, MF_BYCOMMAND) == UINT(-1));
        require(!frame.State(original).supported);
    }
    const auto plugin = std::find_if(frame.plugins.plugins.begin(), frame.plugins.plugins.end(),
        [](const PluginHost::Plugin& candidate) { return candidate.module == L"WebTools.dll"; });
#ifdef MULTIPAD4_TEST_WEB_TOOLS
    const bool installed = frame.plugins.onlyModule == L"__bundled__";
#else
    const bool installed = false;
#endif
    require((plugin != frame.plugins.plugins.end()) == installed);
    if (!installed) return;
    require(plugin->name == L"&Web Tools" && plugin->count == 10 && GetMenuItemCount(plugin->submenu) == 10);
    auto command = [&](int position) { return GetMenuItemID(plugin->submenu, position); };
    auto run = [&](int position) { frame.SendMessage(WM_COMMAND, command(position)); };
    auto shortcut = [&](UINT key, UINT modifier) {
        BYTE original[256]{}, keys[256]{};
        require(GetKeyboardState(original) != FALSE);
        keys[modifier] = keys[VK_SHIFT] = 0x80;
        require(SetKeyboardState(keys) != FALSE);
        MSG message{};
        message.hwnd = document.hwnd;
        message.message = modifier == VK_MENU ? WM_SYSKEYDOWN : WM_KEYDOWN;
        message.wParam = key;
        message.lParam = modifier == VK_MENU ? 1L << 29 : 0;
        const bool handled = frame.PreTranslateMessage(&message) != FALSE;
        require(SetKeyboardState(original) != FALSE);
        require(handled);
    };
    auto load = [&](const char* text) {
        document.Load({text});
        document.Send(SCI_SELECTALL);
    };
    auto enabled = [&](int position) {
        frame.SendMessage(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(plugin->submenu));
        return !(GetMenuState(plugin->submenu, command(position), MF_BYCOMMAND) & (MF_GRAYED | MF_DISABLED));
    };
    require(command(2) == 0 && command(6) == 0);
    auto localizedCommands = reinterpret_cast<PFUNCGETFUNCSARRAY>(GetProcAddress(plugin->library, "getFuncsArray"));
    require(localizedCommands != nullptr);
    const auto language = GetThreadUILanguage();
    SetThreadUILanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
    int count = 0;
    const std::wstring chinese = localizedCommands(&count)[0]._itemName;
    SetThreadUILanguage(language);
    localizedCommands(&count);
    require(chinese == L"CSS/JS/JSON 压缩(&C)");

    load("中文 /?");
    require(enabled(8) && !enabled(0));
    run(8);
    require(document.Text() == "%E4%B8%AD%E6%96%87%20%2F%3F");
    shortcut('R', VK_CONTROL);
    require(document.Text() == "中文 /?");
    document.Send(SCI_UNDO);
    require(document.Text() == "%E4%B8%AD%E6%96%87%20%2F%3F");
    load("https://example.test/a b");
    shortcut('E', VK_CONTROL);
    require(document.Text() == "https://example.test/a%20b");

    load("<中文>&\"'");
    shortcut('X', VK_MENU);
    require(document.Text() == "&lt;中文&gt;&amp;&quot;&apos;");
    shortcut('H', VK_MENU);
    require(document.Text() == "<中文>&\"'");
    load("< _ &");
    document.Send(SCI_SETMULTIPLESELECTION, TRUE);
    document.Send(SCI_SETSEL, 1, 0);
    document.Send(SCI_ADDSELECTION, 5, 4);
    run(4);
    require(document.Text() == "&lt; _ &amp;" && document.Send(SCI_GETSELECTIONS) == 2);
    document.Send(SCI_SETMULTIPLESELECTION, FALSE);

    load("({toString:function(){return '中文';}})");
    CLSID scriptClass{};
    if (SUCCEEDED(CLSIDFromProgID(L"JavaScript", &scriptClass))) {
        run(3);
        require(document.Text().ends_with(" 中文"));
        load("(()");
        run(3);
        require(document.Text() == "(()" && !document.Modified());
    }

    load("{ \"x\": [1, 2] }");
    require(SelectDocumentScheme(document, L"JSON"));
    CaptureQAPopup(frame, plugin->submenu, L"Web-Tools-plugin-menu");
    document.Send(SCI_GOTOPOS, 0);
    const auto originalConfiguration = document.style.configuration;
    const int scheme = document.style.resolvedScheme;
    auto alternateConfiguration = std::make_shared<ConfigurationSnapshot>(*originalConfiguration);
    auto alternateLexer = std::make_shared<LanguageDefinition>(*originalConfiguration->catalog[scheme]);
    alternateLexer->lexerAttr &= ~LexerAttr_CodePretty;
    alternateConfiguration->languages[scheme] = alternateLexer;
    alternateConfiguration->catalog[scheme] = alternateLexer.get();
    document.style.configuration = alternateConfiguration;
    require(!enabled(0) && !enabled(1));
    run(0);
    require(document.Text() == R"({ "x": [1, 2] })");
    document.style.configuration = originalConfiguration;
    require(enabled(0) && !enabled(8));
    run(0);
    require(document.Text() == R"({"x":[1,2]})");
    run(1);
    require(document.Text().find_first_of("\r\n") != std::string::npos);
    run(0);
    require(document.Text() == R"({"x":[1,2]})");

    document.Send(SCI_SETREADONLY, TRUE);
    require(!enabled(0) && !enabled(8));
    run(1);
    require(document.Text() == R"({"x":[1,2]})");
    document.Send(SCI_SETREADONLY, FALSE);
    document.style.scheme = 0;
    document.ConfigureLexer();
}

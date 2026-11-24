#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>
#include <stdexcept>

inline void RunBase64PluginTests(MainFrame& frame, EditorDocument& document) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Base64 plugin assertion at line " + std::to_string(where.line()));
    };
    require(GetMenuState(frame.GetMenu(), IDM_EDIT_BASE64_ENCODE, MF_BYCOMMAND) == UINT(-1));
    const auto plugin = std::find_if(frame.plugins.plugins.begin(), frame.plugins.plugins.end(),
        [](const PluginHost::Plugin& candidate) { return candidate.module == L"Base64.dll"; });
#ifdef MULTIPAD4_TEST_BASE64
    const bool installed = frame.plugins.onlyModule == L"__bundled__";
#else
    const bool installed = false;
#endif
    require((plugin != frame.plugins.plugins.end()) == installed);
    if (!installed) return;
    require(plugin->count == 5 && GetMenuItemCount(plugin->submenu) == 5);
    const auto command = [&](int position) { return GetMenuItemID(plugin->submenu, position); };
    for (int position = 0; position < plugin->count; ++position)
        require(command(position) == static_cast<UINT>(plugin->items[position]._cmdID));
    wchar_t label[menuItemSize]{};
    GetMenuStringW(plugin->submenu, 0, label, _countof(label), MF_BYPOSITION);
    require(std::wstring_view(label) == L"Standard &Encode");
    const auto getCommands = reinterpret_cast<PFUNCGETFUNCSARRAY>(GetProcAddress(plugin->library, "getFuncsArray"));
    require(getCommands != nullptr);
    const auto language = GetThreadUILanguage();
    SetThreadUILanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
    int count = 0;
    const std::wstring chinese = getCommands(&count)[0]._itemName;
    SetThreadUILanguage(language);
    getCommands(&count);
    require(chinese == L"标准编码(&E)");

    auto load = [&](std::string text, files::Encoding encoding = files::Encoding::Utf8) {
        document.Load({std::move(text), encoding});
        document.Send(SCI_SELECTALL);
    };
    auto run = [&](int position) { frame.SendMessage(WM_COMMAND, command(position)); };
    auto enabled = [&] {
        frame.SendMessage(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(plugin->submenu));
        return !(GetMenuState(plugin->submenu, command(0), MF_BYCOMMAND) & (MF_GRAYED | MF_DISABLED));
    };
    load("中文 hello");
    require(enabled());
    CaptureQAPopup(frame, plugin->submenu, L"Base64-plugin-menu");
    run(0);
    require(document.Text() == "5Lit5paHIGhlbGxv");
    run(3);
    require(document.Text() == "中文 hello");
    document.Send(SCI_UNDO);
    require(document.Text() == "5Lit5paHIGhlbGxv");

    load("é", files::FromCodePage(1252));
    run(0);
    require(document.Text() == "6Q==");
    run(3);
    require(document.Text() == "é");

    const std::string bytes("\0\xFB\xFF", 3);
    load(bytes);
    run(1);
    require(document.Text() == "APv_");
    run(3);
    require(document.Text() == bytes);

    load("a");
    document.path = L"image.png";
    run(2);
    require(document.Text() == R"(<img src="data:image/png;base64,YQ==" />)");
    document.path.clear();
    frame.Refresh();

    load("AAECAwQFBgcICQoLDA0ODxA=");
    document.Send(SCI_SETEOLMODE, SC_EOL_CRLF);
    run(4);
    require(document.Text() == "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\r\n10");

    load("bad!");
    bool rejected = false;
    try {
        frame.plugins.Execute(command(3));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected && document.Text() == "bad!" && !document.Modified());

    load("abc\ndef");
    document.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
    document.Send(SCI_SETRECTANGULARSELECTIONANCHOR, 0);
    document.Send(SCI_SETRECTANGULARSELECTIONCARET, 5);
    run(0);
    require(document.Text() == "abc\ndef");
    document.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    document.Send(SCI_SELECTALL);
    document.Send(SCI_SETREADONLY, TRUE);
    require(!enabled());
    run(0);
    require(document.Text() == "abc\ndef");
    document.Send(SCI_SETREADONLY, FALSE);
    document.Send(SCI_GOTOPOS, 0);
    require(!enabled());
    run(0);
    require(document.Text() == "abc\ndef");
}

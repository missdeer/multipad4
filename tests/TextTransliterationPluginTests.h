#pragma once
#include "../src/MainFrame.h"
#include "QAArtifacts.h"
#include <source_location>
#include <stdexcept>

inline void RunTextTransliterationPluginTests(MainFrame& frame, EditorDocument& document) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Text Transliteration plugin assertion at line " + std::to_string(where.line()));
    };
    for (UINT command = IDM_EDIT_MAP_FULLWIDTH; command <= IDM_EDIT_MAP_HANJA_HANGUL; ++command) {
        require(GetMenuState(frame.GetMenu(), command, MF_BYCOMMAND) == UINT(-1));
        require(!frame.State(command).supported);
    }
    const auto plugin = std::find_if(frame.plugins.plugins.begin(), frame.plugins.plugins.end(),
        [](const PluginHost::Plugin& candidate) { return candidate.module == L"TextTransliteration.dll"; });
#ifdef MULTIPAD4_TEST_TEXT_TRANSLITERATION
    const bool installed = frame.plugins.onlyModule == L"__bundled__";
#else
    const bool installed = false;
#endif
    require((plugin != frame.plugins.plugins.end()) == installed);
    if (!installed) return;
    require(plugin->name == L"Text &Transliteration" && plugin->count == 16);
    auto command = [&](int position) { return GetMenuItemID(plugin->submenu, position); };
    auto run = [&](int position) { frame.SendMessage(WM_COMMAND, command(position)); };
    auto enabled = [&](int position) {
        frame.SendMessage(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(plugin->submenu));
        return !(GetMenuState(plugin->submenu, command(position), MF_BYCOMMAND) & (MF_GRAYED | MF_DISABLED));
    };
    require(command(2) == 0 && command(5) == 0 && command(8) == 0 && command(11) == 0);

    auto localizedCommands = reinterpret_cast<PFUNCGETFUNCSARRAY>(GetProcAddress(plugin->library, "getFuncsArray"));
    require(localizedCommands != nullptr);
    const auto language = GetThreadUILanguage();
    SetThreadUILanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
    int count = 0;
    const std::wstring chinese = localizedCommands(&count)[0]._itemName;
    SetThreadUILanguage(language);
    localizedCommands(&count);
    require(chinese == L"半角转全角(&W)");

    document.Load({"AB CD"});
    require(!enabled(0));
    document.Send(SCI_SETSEL, 0, 2);
    document.Send(SCI_ADDSELECTION, 5, 3);
    require(enabled(0));
    CaptureQAPopup(frame, plugin->submenu, L"Text-Transliteration-plugin-menu");
    run(0);
    require(document.Text() == "ＡＢ ＣＤ");
    document.Send(SCI_UNDO);
    require(document.Text() == "AB CD" && !document.Modified());

    document.Load({"漢字"});
    document.Send(SCI_SELECTALL);
    run(3);
    require(document.Text() == "汉字");
    document.Send(SCI_SETREADONLY, TRUE);
    require(!enabled(4) && !enabled(9));
    run(4);
    require(document.Text() == "汉字");
    document.Send(SCI_SETREADONLY, FALSE);

    struct ImeProbe {
        static LRESULT CALLBACK Observe(HWND editor, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
            if (message == WM_IME_KEYDOWN && w == VK_HANJA) {
                *reinterpret_cast<bool*>(data) = true;
                return 0;
            }
            return DefSubclassProc(editor, message, w, l);
        }
    };
    bool receivedHanja = false;
    document.Send(SCI_SELECTALL);
    require(SetWindowSubclass(document.hwnd, ImeProbe::Observe, 0, reinterpret_cast<DWORD_PTR>(&receivedHanja)));
    run(9);
    RemoveWindowSubclass(document.hwnd, ImeProbe::Observe, 0);
    require(receivedHanja);
}

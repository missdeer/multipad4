#pragma once
#include "../src/MainFrame.h"
#include "../src/FileCommands.h"
#include "../src/StyleManager.h"
#include "../src/resource.h"
#include <array>
#include <functional>
#include <source_location>
#include <stdexcept>
#include <tuple>

namespace settingscanceltest {
// Compare preferences, excluding asynchronous occurrence/calltip bookkeeping.
inline auto Preferences(const DocumentStyleState& s) {
    return std::make_tuple(s.scheme, s.dialect, s.dark, s.folding, s.matchBraces,
        s.highlightBlock, s.completion, s.completionIgnoreCase, s.autoIndent, s.latex,
        s.colorCalltip, s.fontKind, s.csvConfigured, s.csvOptions,
        s.tabWidth, s.indentWidth, s.tabsAsSpaces, s.useSchemeTabs, s.tabsOverride,
        s.tabIndents, s.backspaceUnindents, s.detectIndentation, s.backspaceMask,
        s.wrapMode, s.wrapIndent, s.wrapSymbols, s.wrapSymbolsShown,
        s.lineHighlight, s.highlightSubline, s.longLineColumn, s.longLineMode,
        s.longLineEnabled, s.completionOptions, s.completionFillups,
        s.markOccurrences, s.font, s.fontSize, s.codeFont, s.textFont,
        s.codeFontSize, s.textFontSize, s.overrides);
}
inline auto EditorSettings(const EditorDocument& d) {
    return std::array<LRESULT, 13>{d.Send(SCI_GETTABWIDTH), d.Send(SCI_GETINDENT),
        d.Send(SCI_GETUSETABS), d.Send(SCI_GETTABINDENTS), d.Send(SCI_GETBACKSPACEUNINDENTS),
        d.Send(SCI_GETWRAPMODE), d.Send(SCI_GETWRAPINDENTMODE),
        d.Send(SCI_GETWRAPVISUALFLAGS), d.Send(SCI_GETWRAPVISUALFLAGSLOCATION),
        d.Send(SCI_GETCARETLINEHIGHLIGHTSUBLINE), d.Send(SCI_AUTOCGETMAXHEIGHT),
        d.Send(SCI_AUTOCGETIGNORECASE), d.Send(SCI_GETLEXER)};
}
inline auto Profile(AppSettings& settings, const DocumentStyleState& s) {
    std::map<std::wstring, std::wstring> result;
    for (const auto* key : {L"Font", L"CodeFont", L"TextFont", L"CodeFontSize", L"TextFontSize",
        L"Dark", L"FontSize", L"FontKind", L"Completion", L"IgnoreCase", L"AutoIndent", L"LaTeX",
        L"Folding", L"MatchBraces", L"MarkOccurrences", L"ColorCalltip", L"HighlightBlock",
        L"LineHighlight", L"HighlightSubline", L"BackspaceMask", L"TabIndents", L"DetectIndentation",
        L"WrapMode", L"WrapIndent", L"WrapSymbols", L"WrapSymbolsShown", L"CsvOptions",
        L"CsvConfigured", L"CompletionFillups"})
        result[key] = settings.ReadString(L"Editor Defaults", key, L"<unset>");
    for (const auto& [id, value] : s.completionOptions) {
        (void)value;
        auto key = L"Completion" + std::to_wstring(id);
        result[key] = settings.ReadString(L"Editor Defaults", key.c_str(), L"<unset>");
    }
    return result;
}
inline thread_local HWND owner = nullptr;
inline thread_local std::function<void(HWND)> edit;
inline thread_local bool activated = false;
inline thread_local std::exception_ptr failure;
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(owner);
    wchar_t type[32]{}; GetClassNameW(dialog, type, _countof(type));
    if (!dialog || dialog == owner || wcscmp(type, L"#32770")) return;
    KillTimer(nullptr, timer); activated = true;
    try { edit(dialog); } catch (...) { failure = std::current_exception(); }
    PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
}
inline void Cancel(MainFrame& frame, UINT command, std::function<void(HWND)> callback) {
    owner = frame; edit = std::move(callback); activated = false; failure = nullptr;
    const auto timer = SetTimer(nullptr, 0, 10, Timer);
    if (!timer) throw std::runtime_error("Settings cancel timer failed");
    frame.SendMessage(WM_COMMAND, command);
    KillTimer(nullptr, timer); edit = {}; owner = nullptr;
    if (failure) std::rethrow_exception(failure);
    if (!activated) throw std::runtime_error("Settings cancel dialog did not open");
}
}

inline void RunSettingsCancelTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Settings cancel assertion at line " + std::to_string(where.line()));
    };
    using namespace settingscanceltest;
    const auto savedDefaults = frame.defaultStyle;
    const auto savedSettings = frame.settings;
    const int initialCount = frame.tabs.GetPageCount();
    auto* priorActive = frame.Active();
    auto activate = [&](EditorDocument* document) {
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i)
            if (frame.tabs.GetPageData(i) == document) { frame.tabs.SetActivePage(i); frame.Refresh(); return; }
        throw std::runtime_error("Settings cancel document disappeared");
    };
    auto restore = [&] {
        while (frame.tabs.GetPageCount() > initialCount) {
            auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetPageCount() - 1));
            document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
            if (!frame.Close(frame.tabs.GetPageCount() - 1)) break;
        }
        frame.defaultStyle = savedDefaults; frame.settings = savedSettings;
        if (priorActive) activate(priorActive);
    };
    try {
        // Keep all profile reads/writes in memory; never touch the user's INI.
        frame.settings = AppSettings{}; frame.settings.persistent = false;
        frame.defaultStyle = DocumentStyleState{};
        frame.settings.WriteString(L"Editor Defaults", L"TabIndents", L"1");
        frame.settings.Set(IDM_VIEW_WORDWRAP, SC_WRAP_AUTO);
        auto* peer = frame.NewDocument(); require(peer != nullptr);
        auto* document = frame.NewDocument(); require(document != nullptr);
        document->Load({"# tab-always-indent: nil; tab-width: 9; indent-tabs-mode: nil\n"});
        ApplyFileVariables(*document);
        require(document->fileVariables.active && !document->style.tabIndents);
        require(frame.defaultStyle.tabIndents && peer->Send(SCI_GETTABINDENTS));
        // File preferences deliberately differ from defaults in all three groups.
        document->style.wrapMode = SC_WRAP_CHAR; document->style.wrapIndent = 5;
        document->style.completion = false; document->style.autoIndent = false;
        document->style.completionOptions[IDC_AUTOC_VISIBLE_ITEM_COUNT] = 8;
        ConfigureDocumentStyle(*document);
        document->Send(SCI_SETSAVEPOINT);
        const auto defaults = Preferences(frame.defaultStyle);
        const auto profile = Profile(frame.settings, document->style);
        const int wrapOption = frame.settings.Get(IDM_VIEW_WORDWRAP);
        const auto original = Preferences(document->style);
        const auto originalEditor = EditorSettings(*document);
        const auto peerStyle = Preferences(peer->style);
        const auto peerEditor = EditorSettings(*peer);
        const auto baselineTabWidth = peer->Send(SCI_GETTABWIDTH);
        for (UINT command : {IDM_VIEW_TABSETTINGS, IDM_VIEW_WORDWRAPSETTINGS, IDM_VIEW_AUTOCOMPLETION_SETTINGS}) {
            activate(document);
            Cancel(frame, command, [command](HWND dialog) {
                if (command == IDM_VIEW_TABSETTINGS) {
                    SetDlgItemInt(dialog, IDC_GLOBAL_TAB_WIDTH, 11, FALSE);
                    SetDlgItemInt(dialog, IDC_SCHEME_TAB_WIDTH, 12, FALSE);
                    CheckDlgButton(dialog, IDC_FILE_USE_SCHEME_TAB, BST_UNCHECKED);
                    SetDlgItemInt(dialog, IDC_FILE_TAB_WIDTH, 13, FALSE);
                    CheckDlgButton(dialog, IDC_TAB_INDENT, BST_CHECKED);
                    CheckDlgButton(dialog, IDC_DETECT_INDENTATION, BST_UNCHECKED);
                } else if (command == IDM_VIEW_WORDWRAPSETTINGS) {
                    SendDlgItemMessageW(dialog, IDC_WRAP_MODE, CB_SETCURSEL, SC_WRAP_WORD, 0);
                    SendDlgItemMessageW(dialog, IDC_WRAP_INDENT, CB_SETCURSEL, 2, 0);
                    CheckDlgButton(dialog, IDC_WRAP_HIGHLIGHT_SUBLINE, BST_UNCHECKED);
                } else {
                    CheckDlgButton(dialog, IDC_AUTO_COMPLETE_WORD, BST_CHECKED);
                    CheckDlgButton(dialog, IDC_AUTO_INDENT_TEXT, BST_CHECKED);
                    SetDlgItemInt(dialog, IDC_AUTOC_VISIBLE_ITEM_COUNT, 17, FALSE);
                }
            });
            require(Preferences(document->style) == original && EditorSettings(*document) == originalEditor);
            require(Preferences(frame.defaultStyle) == defaults);
            require(Preferences(peer->style) == peerStyle && EditorSettings(*peer) == peerEditor);
            require(Profile(frame.settings, document->style) == profile);
            require(frame.settings.Get(IDM_VIEW_WORDWRAP) == wrapOption);
            auto* fresh = frame.NewDocument(); require(fresh != nullptr);
            require(fresh->style.tabIndents && fresh->Send(SCI_GETTABINDENTS));
            require(fresh->Send(SCI_GETTABWIDTH) == baselineTabWidth);
            require(fresh->style.completion == frame.defaultStyle.completion);
            require(fresh->style.wrapMode == frame.defaultStyle.wrapMode);
            require(frame.Close(frame.tabs.GetActivePage()));
        }
        restore();
    } catch (...) { restore(); throw; }
}

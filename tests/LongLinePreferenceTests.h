#pragma once
#include "../src/MainFrame.h"
#include "../src/FrameCommands.h"
#include "../src/FileCommands.h"
#include "../src/StyleManager.h"
#include "../src/resource.h"
#include <functional>
#include <source_location>
#include <stdexcept>
#include <tuple>

namespace longlinepreferencetest {
inline auto Style(const DocumentStyleState& style) {
    return std::make_tuple(style.longLineColumn, style.longLineMode, style.longLineEnabled);
}
inline auto Editor(const EditorDocument& document) {
    return std::make_tuple(document.Send(SCI_GETEDGECOLUMN), document.Send(SCI_GETEDGEMODE), document.edgeMode);
}
inline auto Profile(const AppSettings& settings) {
    return std::make_tuple(settings.ReadString(L"Editor Defaults", L"EdgeColumn", L"<unset>"),
        settings.ReadString(L"Editor Defaults", L"EdgeMode", L"<unset>"),
        settings.ReadString(L"Editor Defaults", L"EdgeVisible", L"<unset>"));
}
inline thread_local HWND owner = nullptr;
inline thread_local std::function<void(HWND)> edit;
inline thread_local UINT finish = IDCANCEL;
inline thread_local bool activated = false;
inline thread_local std::exception_ptr failure;
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(owner);
    if (!dialog || dialog == owner || !GetDlgItem(dialog, IDC_LONGLINE_LIMIT)) return;
    KillTimer(nullptr, timer); activated = true;
    try { edit(dialog); } catch (...) { failure = std::current_exception(); }
    PostMessageW(dialog, WM_COMMAND, failure ? IDCANCEL : finish, 0);
}
inline void Dialog(MainFrame& frame, UINT result, std::function<void(HWND)> callback) {
    owner = frame; edit = std::move(callback); finish = result; activated = false; failure = nullptr;
    const auto timer = SetTimer(nullptr, 0, 10, Timer);
    if (!timer) throw std::runtime_error("Long line preference timer failed");
    frame.SendMessage(WM_COMMAND, IDM_VIEW_LONGLINESETTINGS);
    KillTimer(nullptr, timer); owner = nullptr; edit = {};
    if (failure) std::rethrow_exception(failure);
    if (!activated) throw std::runtime_error("Long line preference dialog did not open");
}
}

inline void RunLongLinePreferenceTests(MainFrame& frame) {
    using namespace longlinepreferencetest;
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Long line preference assertion at line " + std::to_string(where.line()));
    };
    const auto savedDefaults = frame.defaultStyle;
    const auto savedSettings = frame.settings;
    const int initialCount = frame.tabs.GetPageCount();
    auto* priorActive = frame.Active();
    struct Snapshot {
        EditorDocument* document;
        int column, mode, styleColumn, styleMode;
        bool enabled;
        LRESULT visible;
        FileVariableState variables;
    };
    std::vector<Snapshot> snapshots;
    for (const auto& document : frame.documents)
        snapshots.push_back({document.get(), static_cast<int>(document->Send(SCI_GETEDGECOLUMN)), document->edgeMode,
            document->style.longLineColumn, document->style.longLineMode, document->style.longLineEnabled,
            document->Send(SCI_GETEDGEMODE), document->fileVariables});
    auto activate = [&](EditorDocument* document) {
        for (int i = 0; i < frame.tabs.GetPageCount(); ++i)
            if (frame.tabs.GetPageData(i) == document) { frame.tabs.SetActivePage(i); frame.Refresh(); return; }
        throw std::runtime_error("Long line preference document disappeared");
    };
    auto restore = [&] {
        while (frame.tabs.GetPageCount() > initialCount) {
            auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetPageCount() - 1));
            document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
            if (!frame.Close(frame.tabs.GetPageCount() - 1)) break;
        }
        frame.defaultStyle = savedDefaults; frame.settings = savedSettings;
        for (const auto& snapshot : snapshots) {
            auto& document = *snapshot.document;
            document.edgeMode = snapshot.mode; document.style.longLineColumn = snapshot.styleColumn;
            document.style.longLineMode = snapshot.styleMode; document.style.longLineEnabled = snapshot.enabled;
            document.fileVariables = snapshot.variables;
            document.Send(SCI_SETEDGECOLUMN, snapshot.column); document.Send(SCI_SETEDGEMODE, snapshot.visible);
        }
        if (priorActive) activate(priorActive);
    };
    try {
        // All preference persistence remains in memory; never write the user's profile.
        frame.settings = AppSettings{}; frame.settings.persistent = false;
        frame.defaultStyle = DocumentStyleState{};
        auto* peer = frame.NewDocument(); require(peer != nullptr);
        auto* file = frame.NewDocument(); require(file != nullptr);
        file->Load({"# fill-column: 91\n"}); ApplyFileVariables(*file);
        require(file->fileVariables.active && file->Send(SCI_GETEDGECOLUMN) == 91);
        auto* document = frame.NewDocument(); require(document != nullptr);
        document->Load({"# fill-column: 97\n"}); ApplyFileVariables(*document);
        require(document->Send(SCI_GETEDGECOLUMN) == 97);
        frame.SendMessage(WM_COMMAND, IDM_VIEW_LONGLINEMARKER);
        require(frame.defaultStyle.longLineEnabled && frame.defaultStyle.longLineColumn == 80);
        require(frame.settings.ReadString(L"Editor Defaults", L"EdgeColumn") == L"80");
        // An unrelated setting must not promote either an effective file column or
        // a prior document-specific long line style into the new-document defaults.
        document->style.longLineColumn = 97;
        RememberDocumentPreferences(frame, *document, IDM_VIEW_LINENUMBERS);
        require(frame.defaultStyle.longLineColumn == 80);
        const auto defaults = Style(frame.defaultStyle), original = Style(document->style), peerStyle = Style(peer->style);
        const auto editor = Editor(*document), peerEditor = Editor(*peer), fileEditor = Editor(*file);
        const auto profile = Profile(frame.settings);
        Dialog(frame, IDCANCEL, [&](HWND dialog) {
            require(GetDlgItemInt(dialog, IDC_LONGLINE_LIMIT, nullptr, FALSE) == 97);
            SetDlgItemInt(dialog, IDC_LONGLINE_LIMIT, 143, FALSE);
            CheckRadioButton(dialog, IDC_LONGLINE_EDGE_LINE, IDC_LONGLINE_BACK_COLOR, IDC_LONGLINE_BACK_COLOR);
        });
        require(Style(frame.defaultStyle) == defaults && Style(document->style) == original);
        require(Editor(*document) == editor && Editor(*peer) == peerEditor && Editor(*file) == fileEditor);
        require(Style(peer->style) == peerStyle && Profile(frame.settings) == profile);
        auto* fresh = frame.NewDocument(); require(fresh != nullptr);
        require(fresh->Send(SCI_GETEDGECOLUMN) == 80 && fresh->Send(SCI_GETEDGEMODE) == EDGE_LINE);
        activate(document);
        Dialog(frame, IDOK, [](HWND dialog) {
            SetDlgItemInt(dialog, IDC_LONGLINE_LIMIT, 120, FALSE);
            CheckRadioButton(dialog, IDC_LONGLINE_EDGE_LINE, IDC_LONGLINE_BACK_COLOR, IDC_LONGLINE_BACK_COLOR);
        });
        require(frame.defaultStyle.longLineColumn == 120 && frame.defaultStyle.longLineMode == EDGE_BACKGROUND);
        require(frame.defaultStyle.longLineEnabled && document->Send(SCI_GETEDGECOLUMN) == 120);
        require(peer->Send(SCI_GETEDGECOLUMN) == 120 && fresh->Send(SCI_GETEDGECOLUMN) == 120);
        require(file->Send(SCI_GETEDGECOLUMN) == 91 && file->Send(SCI_GETEDGEMODE) == EDGE_BACKGROUND);
        require(frame.settings.ReadString(L"Editor Defaults", L"EdgeColumn") == L"120");
        require(frame.settings.ReadString(L"Editor Defaults", L"EdgeMode") == std::to_wstring(EDGE_BACKGROUND));
        require(frame.settings.ReadString(L"Editor Defaults", L"EdgeVisible") == L"1");
        // Restoring modelines uses the updated global baseline; a user-confirmed
        // current-file limit survives restoration of the old modeline value.
        RestoreFileVariables(*file); require(file->Send(SCI_GETEDGECOLUMN) == 120);
        RestoreFileVariables(*document); require(document->Send(SCI_GETEDGECOLUMN) == 120);
        frame.SendMessage(WM_COMMAND, IDM_VIEW_LONGLINEMARKER);
        require(!frame.defaultStyle.longLineEnabled && frame.defaultStyle.longLineColumn == 120);
        fresh = frame.NewDocument(); require(fresh != nullptr);
        require(fresh->Send(SCI_GETEDGECOLUMN) == 120 && fresh->Send(SCI_GETEDGEMODE) == EDGE_NONE);
        // A style refresh must agree with the defaults used at document creation.
        ConfigureDocumentStyle(*fresh);
        require(fresh->style.longLineColumn == 120 && fresh->edgeMode == EDGE_BACKGROUND);
        require(fresh->Send(SCI_GETEDGECOLUMN) == 120 && fresh->Send(SCI_GETEDGEMODE) == EDGE_NONE);
        restore();
    } catch (...) { restore(); throw; }
}

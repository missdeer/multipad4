#pragma once
#include "MainFrame.h"
#include <source_location>
#include "../tests/QAArtifacts.h"
// Regression checks execute real menu/toolbar IDs and modeless dialog callbacks.
inline void RunMigrationSmokeTest(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Migration assertion failed at line " + std::to_string(where.line()));
    };
    for (UINT id : {IDD_ABOUT, IDD_SEARCH, IDD_RUN, IDD_OPENWITH, IDD_SYSTEM_INTEGRATION,
        IDD_DEFENCODING, IDD_DEFEOLMODE, IDD_WARNLINEENDS, IDD_LINENUM, IDD_FILEMRU, IDD_CHANGENOTIFY,
        IDD_STYLESELECT, IDD_STYLECONFIG, IDD_TABSETTINGS, IDD_LONGLINES, IDD_WORDWRAP, IDD_PAGESETUP,
        IDD_FAVORITES, IDD_ADDTOFAV, IDD_COLUMNWRAP, IDD_MODIFYLINES, IDD_INSERTTAG, IDD_ENCLOSESELECTION,
        IDD_INFOBOX, IDD_SORT, IDD_SELECT_ENCODING, IDD_ALIGN, IDD_ZOOMLEVEL, IDD_AUTOCOMPLETION,
        IDD_AUTOSAVE, IDD_CSV_OPTIONS})
        require(FindResourceW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(id), RT_DIALOG) != nullptr);
    require(FindResourceW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDD_FIND), RT_DIALOG) == nullptr);
    require(FindResourceW(_Module.GetModuleInstance(), MAKEINTRESOURCEW(IDD_REPLACE), RT_DIALOG) == nullptr);
    require(ImageList_GetImageCount(frame.toolbarImages) == 27);
    require(::SendMessageW(frame.m_hWndToolBar, TB_COMMANDTOINDEX, IDT_FILE_SAVE, 0) >= 0);
    require(GetMenuState(frame.GetMenu(), IDM_EDIT_REPLACE, MF_BYCOMMAND) != static_cast<UINT>(-1));
    require(frame.State(IDM_SET_FILE_AUTOSAVE).enabled);
    require(!frame.State(45990).supported);
    auto* first = frame.Active(); const auto original = first->Text(); const auto originalEncoding = first->encoding;
    const bool originalWrap = first->Send(SCI_GETWRAPMODE) != 0;
    frame.NewDocument(); auto* target = frame.Active();
    target->Load({"alpha ALPHA alphabet\n中文 alpha\nlast"});
    target->search.findText = L"alpha";
    target->search.replaceText = L"x";
    target->search.flags = SCFIND_MATCHCASE | SCFIND_WHOLEWORD;
    require(target->FindNext());
    require(target->Send(SCI_GETSELECTIONSTART) == 0 && target->Send(SCI_GETSELECTIONEND) == 5);
    target->Send(SCI_GOTOPOS, target->Send(SCI_GETLENGTH)); require(target->FindNext(true));
    const auto lastMatch = target->Send(SCI_GETSELECTIONSTART);
    require(lastMatch > 20);
    require(target->FindNext(true) && target->Send(SCI_GETSELECTIONSTART) == 0);
    target->search.wrap = false;
    require(!target->FindNext(true));
    target->search.wrap = true;
    require(target->ReplaceAll() == 2);
    require(target->Text() == "x ALPHA alphabet\n中文 x\nlast");
    frame.SendMessage(WM_COMMAND, IDM_EDIT_UNDO);
    require(target->Text() == "alpha ALPHA alphabet\n中文 alpha\nlast");
    // Replacement containing its own needle must terminate, and be one undo step.
    target->search.findText = L"alpha"; target->search.replaceText = L"alphaalpha";
    target->Send(SCI_SETSEL, 0, 5);
    require(target->ReplaceAll(true) == 1);
    require(target->Text().starts_with("alphaalpha ALPHA"));
    frame.SendMessage(WM_COMMAND, IDM_EDIT_UNDO);
    // The final line selection must not accidentally start at the document start.
    target->Send(SCI_GOTOPOS, target->Send(SCI_GETLENGTH));
    frame.SendMessage(WM_COMMAND, IDM_EDIT_SELECTLINE);
    require(target->Send(SCI_GETSELECTIONSTART) == target->Send(SCI_GETLENGTH) - 4);
    auto targetWrap = target->Send(SCI_GETWRAPMODE);
    frame.SendMessage(WM_COMMAND, IDT_VIEW_WORDWRAP);
    require(target->Send(SCI_GETWRAPMODE) == (targetWrap ? SC_WRAP_NONE : target->style.wrapMode));
    require((first->Send(SCI_GETWRAPMODE) != 0) == originalWrap);
    frame.SendMessage(WM_COMMAND, IDM_FILE_READONLY_MODE);
    auto before = target->Text();
    require(!frame.State(IDM_EDIT_CLEARDOCUMENT).enabled);
    require(!frame.State(IDM_ENCODING_UNICODE).enabled);
    frame.SendMessage(WM_COMMAND, IDM_EDIT_CLEARDOCUMENT);
    require(target->Text() == before && target->ReplaceAll() == 0);
    frame.SendMessage(WM_COMMAND, IDM_FILE_READONLY_MODE);
    target->Send(SCI_SETSAVEPOINT);
    frame.SendMessage(WM_COMMAND, IDM_ENCODING_UNICODE);
    require(target->Modified() && !target->Send(SCI_GETMODIFY));
    require(first->encoding == originalEncoding);
    frame.SendMessage(WM_COMMAND, IDM_LINEENDINGS_CRLF);
    require(target->Send(SCI_GETEOLMODE) == SC_EOL_CRLF && target->Text().find("\r\n") != std::string::npos);
    auto copyPath = first->path.parent_path() / L"copy.txt";
    auto oldPath = target->path;
    require(frame.SaveTo(*target, copyPath, true));
    require(target->path == oldPath && target->Modified());
    auto copy = files::Read(copyPath);
    require(copy.encoding == files::Encoding::Utf16LE && copy.utf8 == target->Text());
    require(frame.SaveTo(*target, first->path.parent_path() / L"reload.txt"));
    files::WriteAtomic(target->path, {"from disk", files::Encoding::Utf8Bom});
    target->Send(SCI_SETREADONLY, TRUE);
    require(frame.Reload(*target));
    require(target->Text() == "from disk" && target->encoding == files::Encoding::Utf8Bom);
    require(target->Send(SCI_GETREADONLY) && !target->Modified());
    target->Send(SCI_SETREADONLY, FALSE); target->Load(copy);
    frame.SendMessage(WM_COMMAND, IDM_VIEW_LINENUMBERS);
    require(target->Send(SCI_GETMARGINWIDTHN, 0) == 0);
    frame.Refresh(); require(::SendMessageW(frame.m_hWndStatusBar, SB_GETPARTS, 0, 0) == 13);
    frame.ShowSearch(true);
    require(frame.searchDialog.hwnd != nullptr && frame.searchDialog.document == target);
    auto dialog = frame.searchDialog.hwnd;
    CaptureQAWindow(dialog, L"Search-dialog");
    SetDlgItemTextW(dialog, IDC_FINDTEXT, L"alpha");
    SetDlgItemTextW(dialog, IDC_REPLACETEXT, L"replacement");
    target->Send(SCI_SETSEL, 0, 5);
    const int targetIndex = frame.tabs.GetActivePage();
    const auto targetText = target->Text();
    frame.tabs.SetActivePage(0); frame.Refresh();
    require(frame.Active() == first && frame.searchDialog.document == first);
    ::SendMessageW(dialog, WM_COMMAND, IDC_REPLACE, 0);
    require(target->Text() == targetText && first->Text() == original);
    frame.tabs.SetActivePage(targetIndex); frame.Refresh();
    ::SendMessageW(dialog, WM_COMMAND, IDC_REPLACE, 0);
    require(target->Text().starts_with("replacement ALPHA"));
    require(first->Text() == original);
    // Closing the target also closes the modeless dialog; no dangling receiver.
    target->Send(SCI_SETSAVEPOINT); target->encodingDirty = false;
    require(frame.Close(targetIndex)); require(!IsWindow(dialog) && !frame.searchDialog.document);
    require(first->Text() == original);
}

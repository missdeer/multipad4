#pragma once
#include "../src/AppSettings.h"
#include "../src/EditorCommands.h"
#include "../src/InfoDialog.h"
#include "../src/resource.h"
#include "QAArtifacts.h"
#include <functional>
#include <source_location>
#include <stdexcept>
namespace EditDialogTest {
inline thread_local HWND owner = nullptr;
inline thread_local std::function<void(HWND)> fill;
inline thread_local UINT finish = IDOK;
inline thread_local bool opened = false;
inline thread_local unsigned ticks = 0;
inline void CALLBACK Timer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(owner);
    wchar_t name[32]{};
    GetClassNameW(dialog, name, 32);
    if (dialog && dialog != owner && wcscmp(name, L"#32770") == 0) {
        KillTimer(nullptr, timer);
        opened = true;
        if (fill)
            fill(dialog);
        CaptureQAWindow(dialog, L"edit-dialog");
        PostMessageW(dialog, WM_COMMAND, finish, 0);
    } else if (++ticks > 100) {
        KillTimer(nullptr, timer);
        PostMessageW(owner, WM_CANCELMODE, 0, 0);
    }
}
inline void Run(EditorDocument &d, UINT command, std::function<void(HWND)> callback, UINT result = IDOK) {
    owner = GetAncestor(d.hwnd, GA_ROOT);
    fill = std::move(callback);
    finish = result;
    opened = false;
    ticks = 0;
    auto timer = SetTimer(nullptr, 0, 10, Timer);
    if (!timer)
        throw std::runtime_error("Cannot create edit dialog test timer");
    ExecuteEditorCommand(d, command);
    KillTimer(nullptr, timer);
    fill = {};
    owner = nullptr;
    if (!opened)
        throw std::runtime_error("Edit dialog did not open for " + std::to_string(command));
}
} // namespace EditDialogTest
inline void RunEditDialogTests(EditorDocument &d) {
    auto require = [](bool condition, std::source_location source = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error("Edit dialog assertion at line " + std::to_string(source.line()));
    };
    auto load = [&](const char *text) {
        d.Send(SCI_SETREADONLY, FALSE);
        d.Load({text});
        d.Send(SCI_SELECTALL);
    };
    using EditDialogTest::Run;
    for (UINT command : {IDM_EDIT_SORTLINES, IDM_EDIT_MODIFYLINES, IDM_EDIT_ENCLOSESELECTION, IDM_EDIT_INSERT_XMLTAG,
                         IDM_EDIT_ALIGN, IDM_EDIT_COLUMNWRAP}) {
        load("c\nb\na");
        auto original = d.Text();
        Run(d, command, [](HWND dialog) { SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L"changed"); }, IDCANCEL);
        require(d.Text() == original && !d.Modified());
    }
    load("b\r\na\r\nb\r\n");
    Run(d, IDM_EDIT_SORTLINES, [](HWND dialog) {
        CheckRadioButton(dialog, IDC_SORT_NONE, IDC_SORT_SHUFFLE, IDC_SORT_ASC);
        CheckDlgButton(dialog, IDC_SORT_MERGE_DUP, BST_CHECKED);
    });
    require(d.Text() == "a\r\nb\r\n");
    d.Send(SCI_UNDO);
    require(d.Text() == "b\r\na\r\nb\r\n");
    load("a\nb");
    Run(d, IDM_EDIT_MODIFYLINES, [](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L"$(L):");
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_APPEND, L"-$(I)");
    });
    require(d.Text() == "1:a-0\n2:b-1");
    d.Send(SCI_UNDO);
    require(d.Text() == "a\nb");
    load("abc");
    Run(d, IDM_EDIT_ENCLOSESELECTION, [](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L"[");
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_APPEND, L"]");
    });
    require(d.Text() == "[abc]");
    require(d.Send(SCI_GETSELECTIONSTART) == 1 && d.Send(SCI_GETSELECTIONEND) == 4);
    d.Send(SCI_UNDO);
    require(d.Text() == "abc");
    load("abc");
    Run(d, IDM_EDIT_INSERT_XMLTAG, [](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L"<span class='x'>");
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_MODIFY_LINE_PREFIX, EN_CHANGE),
                     reinterpret_cast<LPARAM>(GetDlgItem(dialog, IDC_MODIFY_LINE_PREFIX)));
    });
    if (d.Text() != "<span class='x'>abc</span>")
        throw std::runtime_error("XML dialog actual text: [" + d.Text() + "]");
    d.Send(SCI_UNDO);
    require(d.Text() == "abc");
    load("abc\nx");
    Run(d, IDM_EDIT_ALIGN,
        [](HWND dialog) { CheckRadioButton(dialog, IDC_ALIGN_LEFT, IDC_ALIGN_JUSTIFY_PAR, IDC_ALIGN_RIGHT); });
    require(d.Text() == "abc\n  x");
    d.Send(SCI_UNDO);
    require(d.Text() == "abc\nx");
    load("one two three");
    d.Send(SCI_SETEOLMODE, SC_EOL_CRLF);
    Run(d, IDM_EDIT_COLUMNWRAP, [](HWND dialog) { SetDlgItemInt(dialog, IDC_COLUMNWRAP, 7, FALSE); });
    require(d.Text() == "one two\r\nthree");
    d.Send(SCI_UNDO);
    require(d.Text() == "one two three");
    load("😀x");
    d.Send(SCI_SETEOLMODE, SC_EOL_LF);
    Run(d, IDM_EDIT_COLUMNWRAP, [](HWND dialog) { SetDlgItemInt(dialog, IDC_COLUMNWRAP, 1, FALSE); });
    require(d.Text() == "😀\nx");
    d.Send(SCI_UNDO);
    require(d.Text() == "😀x");
    load("x b\ny a");
    d.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
    d.Send(SCI_SETRECTANGULARSELECTIONANCHOR, 2);
    d.Send(SCI_SETRECTANGULARSELECTIONCARET, 7);
    Run(d, IDM_EDIT_SORTLINES,
        [](HWND dialog) { CheckRadioButton(dialog, IDC_SORT_NONE, IDC_SORT_SHUFFLE, IDC_SORT_ASC); });
    require(d.Text() == "y a\nx b");
    require(d.Send(SCI_SELECTIONISRECTANGLE));
    d.Send(SCI_UNDO);
    require(d.Text() == "x b\ny a");
    d.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    load("x a\ny a");
    d.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
    d.Send(SCI_SETRECTANGULARSELECTIONANCHOR, 2);
    d.Send(SCI_SETRECTANGULARSELECTIONCARET, 7);
    Run(d, IDM_EDIT_SORTLINES, [](HWND dialog) {
        CheckRadioButton(dialog, IDC_SORT_NONE, IDC_SORT_SHUFFLE, IDC_SORT_ASC);
        CheckDlgButton(dialog, IDC_SORT_MERGE_DUP, BST_CHECKED);
    });
    require(d.Text() == "x a\ny a");
    d.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    load("first second");
    d.Send(SCI_GOTOPOS, d.Send(SCI_GETLENGTH));
    d.search.findText = L"first";
    AppSettings infoSettings;
    infoSettings.Initialize(true);
    EditDialogTest::owner = GetAncestor(d.hwnd, GA_ROOT);
    EditDialogTest::opened = false;
    EditDialogTest::ticks = 0;
    EditDialogTest::finish = IDCANCEL;
    EditDialogTest::fill = {};
    auto timer = SetTimer(nullptr, 0, 10, EditDialogTest::Timer);
    require(timer != 0);
    auto beforeCaret = d.Send(SCI_GETCURRENTPOS);
    bool wrapped = d.FindNext(false, [&](bool backwards) {
        require(!backwards);
        return ShowInfoDialog(EditDialogTest::owner, infoSettings, L"Reached end. Continue at beginning?", L"Find Text",
                              MB_OKCANCEL | MB_ICONINFORMATION, L"MsgFindWrap1") == IDOK;
    });
    KillTimer(nullptr, timer);
    EditDialogTest::owner = nullptr;
    EditDialogTest::finish = IDOK;
    require(EditDialogTest::opened && !wrapped && d.Send(SCI_GETCURRENTPOS) == beforeCaret);
    require(d.Text() == "first second");
    require(d.FindNext(false, [](bool) { return true; }));
    require(d.Send(SCI_GETSELECTIONSTART) == 0);
    d.Send(SCI_SETSAVEPOINT);
    d.encodingDirty = false;
}

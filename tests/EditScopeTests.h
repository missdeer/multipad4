#pragma once
#include "../src/EditorCommands.h"
#include "../src/StyleManager.h"
#include "EditDialogTests.h"
#include "StyleDialogTests.h"
#include <source_location>
inline void RunEditScopeTests(EditorDocument &d) {
    auto require = [](bool condition, std::source_location source = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error("Edit scope assertion at line " + std::to_string(source.line()));
    };
    auto load = [&](const char *text) {
        d.Send(SCI_SETREADONLY, FALSE);
        d.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
        d.Load({text});
        d.Send(SCI_SETTABWIDTH, 4);
        d.Send(SCI_SELECTALL);
    };
    load("a\tz\tx");
    d.Send(SCI_SETSEL, 0, 3);
    ExecuteEditorCommand(d, IDM_EDIT_CONVERTTABS);
    require(d.Text() == "a   z\tx");
    d.Send(SCI_UNDO);
    require(d.Text() == "a\tz\tx");
    load("a   z    q");
    d.Send(SCI_SETSEL, 0, 5);
    ExecuteEditorCommand(d, IDM_EDIT_CONVERTSPACES);
    require(d.Text() == "a\tz    q");
    d.Send(SCI_UNDO);
    require(d.Text() == "a   z    q");
    load("one  two  three");
    d.Send(SCI_SETSEL, 3, 8);
    ExecuteEditorCommand(d, IDM_EDIT_COMPRESSWS);
    require(d.Text() == "one two  three");
    d.Send(SCI_UNDO);
    require(d.Text() == "one  two  three");
    load("a  \nb  ");
    d.Send(SCI_SETSEL, 0, 6);
    ExecuteEditorCommand(d, IDM_EDIT_TRIMLINES);
    require(d.Text() == "a\nb  ");
    d.Send(SCI_UNDO);
    require(d.Text() == "a  \nb  ");
    load("  abc\n  xyz");
    d.Send(SCI_SETSEL, 1, 9);
    ExecuteEditorCommand(d, IDM_EDIT_TRIMLEAD);
    require(d.Text() == "  abc\nxyz");
    d.Send(SCI_UNDO);
    require(d.Text() == "  abc\n  xyz");
    const auto undoSelectionHistory = d.Send(SCI_GETUNDOSELECTIONHISTORY);
    d.Send(SCI_SETUNDOSELECTIONHISTORY, SC_UNDO_SELECTION_HISTORY_DISABLED);
    auto requireDuplicateText = [&](const char *expected, const char *operation) {
        const auto actual = d.Text();
        if (actual != expected)
            throw std::runtime_error(std::string("Duplicate lines ") + operation + ": expected [" + expected +
                                     "], actual [" + actual + "], selection [" +
                                     std::to_string(d.Send(SCI_GETSELECTIONSTART)) + ", " +
                                     std::to_string(d.Send(SCI_GETSELECTIONEND)) + "]");
    };
    load("one\ntwo\none");
    require(EditorCommandState(&d, IDM_EDIT_MERGEDUPLICATELINE).enabled);
    ExecuteEditorCommand(d, IDM_EDIT_MERGEDUPLICATELINE);
    requireDuplicateText("one\ntwo", "merge");
    d.Send(SCI_UNDO);
    requireDuplicateText("one\ntwo\none", "undo merge");
    // Scintilla's default undo behavior restores text and collapses the selection.
    // Upstream requires more than one selected line for either duplicate command.
    require(d.Send(SCI_GETSELECTIONEMPTY));
    require(!EditorCommandState(&d, IDM_EDIT_MERGEDUPLICATELINE).enabled);
    require(!EditorCommandState(&d, IDM_EDIT_REMOVEDUPLICATELINE).enabled);
    ExecuteEditorCommand(d, IDM_EDIT_REMOVEDUPLICATELINE);
    requireDuplicateText("one\ntwo\none", "remove without selection");
    d.Send(SCI_SELECTALL);
    require(EditorCommandState(&d, IDM_EDIT_REMOVEDUPLICATELINE).enabled);
    ExecuteEditorCommand(d, IDM_EDIT_REMOVEDUPLICATELINE);
    requireDuplicateText("two", "remove");
    d.Send(SCI_UNDO);
    requireDuplicateText("one\ntwo\none", "undo remove");
    d.Send(SCI_SETUNDOSELECTIONHISTORY, undoSelectionHistory);
    load("  \n\nbody");
    d.Send(SCI_SETSEL, 1, 4);
    ExecuteEditorCommand(d, IDM_EDIT_REMOVEBLANKLINES);
    require(d.Text() == "  \nbody");
    load("first\nsecond\nthird");
    d.Send(SCI_GOTOPOS, d.Send(SCI_POSITIONFROMLINE, 1));
    EditDialogTest::Run(d, IDM_EDIT_MODIFYLINES, [](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L">");
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_APPEND, L"");
        CheckDlgButton(dialog, IDC_MODIFY_LINE_SKIP_EMPTY, BST_UNCHECKED);
    });
    require(d.Text() == "first\n>second\nthird");
    require(d.Send(SCI_GETCURRENTPOS) == 6 && d.Send(SCI_GETANCHOR) == 6);
    d.Send(SCI_UNDO);
    require(d.Text() == "first\nsecond\nthird");
    load("a\n");
    d.Send(SCI_GOTOPOS, d.Send(SCI_GETLENGTH));
    EditDialogTest::Run(d, IDM_EDIT_MODIFYLINES, [](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L">");
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_APPEND, L"");
        CheckDlgButton(dialog, IDC_MODIFY_LINE_SKIP_EMPTY, BST_UNCHECKED);
    });
    require(d.Text() == "a\n>");
    require(d.Send(SCI_GETCURRENTPOS) == 2 && d.Send(SCI_GETANCHOR) == 2);
    load("long header\n  x");
    d.Send(SCI_GOTOPOS, d.Send(SCI_POSITIONFROMLINE, 1));
    EditDialogTest::Run(d, IDM_EDIT_ALIGN, [](HWND dialog) {
        CheckRadioButton(dialog, IDC_ALIGN_LEFT, IDC_ALIGN_JUSTIFY_PAR, IDC_ALIGN_LEFT);
    });
    require(d.Text() == "long header\n  x");
    load("one two three more");
    d.Send(SCI_SETSEL, 0, 13);
    d.Send(SCI_SETEOLMODE, SC_EOL_LF);
    EditDialogTest::Run(d, IDM_EDIT_COLUMNWRAP, [](HWND dialog) { SetDlgItemInt(dialog, IDC_COLUMNWRAP, 7, FALSE); });
    require(d.Text() == "one two\nthree more");
    load("x\nx\nx\nx\nx\nx\nx\nx\nx\nx\n");
    EditDialogTest::Run(d, IDM_EDIT_MODIFYLINES, [](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L"$(0I):");
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_APPEND, L"");
    });
    require(d.Text().starts_with("0:x\n1:x\n"));
    auto numbered = std::string(97, '\n') + "x\nx\nx";
    load(numbered.c_str());
    d.Send(SCI_SETSEL, 97, d.Send(SCI_GETLENGTH));
    EditDialogTest::Run(d, IDM_EDIT_MODIFYLINES, [](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_PREFIX, L"$(0L):");
        SetDlgItemTextW(dialog, IDC_MODIFY_LINE_APPEND, L"");
    });
    require(d.Text().substr(97) == "098:x\n099:x\n100:x");
    auto savedStyle = d.style;
    require(SelectDocumentScheme(d, L"asm"));
    load("mov eax, 1");
    auto owner = GetAncestor(d.hwnd, GA_ROOT);
    StyleDialogTest::Run(owner, d, IDD_AUTOCOMPLETION, [](HWND dialog) {
        CheckRadioButton(dialog, IDC_ASM_LINE_COMMENT_SEMICOLON, IDC_ASM_LINE_COMMENT_AT, IDC_ASM_LINE_COMMENT_SLASH);
        CheckDlgButton(dialog, IDC_AUTO_INSERT_SPACE_COMMENT, BST_UNCHECKED);
    });
    ExecuteEditorCommand(d, IDM_EDIT_LINECOMMENT);
    require(d.Text() == "//mov eax, 1");
    d.Send(SCI_SELECTALL);
    ExecuteEditorCommand(d, IDM_EDIT_LINECOMMENT);
    require(d.Text() == "mov eax, 1");
    StyleDialogTest::Run(owner, d, IDD_AUTOCOMPLETION,
                         [](HWND dialog) { CheckDlgButton(dialog, IDC_AUTO_INSERT_SPACE_COMMENT, BST_CHECKED); });
    d.Send(SCI_SELECTALL);
    ExecuteEditorCommand(d, IDM_EDIT_LINECOMMENT);
    require(d.Text() == "// mov eax, 1");
    load("mov eax, 1");
    StyleDialogTest::Run(owner, d, IDD_AUTOCOMPLETION, [](HWND dialog) {
        CheckRadioButton(dialog, IDC_ASM_LINE_COMMENT_SEMICOLON, IDC_ASM_LINE_COMMENT_AT, IDC_ASM_LINE_COMMENT_SHARP);
        CheckDlgButton(dialog, IDC_AUTO_INSERT_SPACE_COMMENT, BST_UNCHECKED);
    });
    ExecuteEditorCommand(d, IDM_EDIT_LINECOMMENT);
    require(d.Text() == "# mov eax, 1");
    require(SelectDocumentScheme(d, L"bat"));
    load("@REM text");
    ExecuteEditorCommand(d, IDM_EDIT_LINECOMMENT);
    require(d.Text() == "text");
    load("@remember text");
    ExecuteEditorCommand(d, IDM_EDIT_LINECOMMENT);
    require(d.Text() == "@rem @remember text");
    require(SelectDocumentScheme(d, L"cpp"));
    load("plain\n//existing");
    d.style.completionOptions[IDC_AUTO_INSERT_SPACE_COMMENT] = 1;
    ExecuteEditorCommand(d, IDM_EDIT_LINECOMMENT);
    require(d.Text() == "// plain\n//existing");
    require(SelectDocumentScheme(d, L"matlab"));
    load("value = 1;\n");
    ExecuteEditorCommand(d, IDM_EDIT_STREAMCOMMENT);
    require(d.Text() == "%{\nvalue = 1;\n%}\n");
    require(d.Send(SCI_GETSELECTIONSTART) == 3 && d.Send(SCI_GETSELECTIONEND) == 14);
    ExecuteEditorCommand(d, IDM_EDIT_STREAMCOMMENT);
    require(d.Text() == "value = 1;\n");
    require(d.Send(SCI_GETSELECTIONSTART) == 0 && d.Send(SCI_GETSELECTIONEND) == 11);
    d.Send(SCI_UNDO);
    require(d.Text() == "%{\nvalue = 1;\n%}\n");
    d.Send(SCI_UNDO);
    require(d.Text() == "value = 1;\n");
    require(SelectDocumentScheme(d, L"au3"));
    load("#CS\nbody\n#CE\n");
    d.Send(SCI_SETSEL, 4, 9);
    ExecuteEditorCommand(d, IDM_EDIT_STREAMCOMMENT);
    require(d.Text() == "body\n");
    require(SelectDocumentScheme(d, L"cpp"));
    load("body");
    ExecuteEditorCommand(d, IDM_EDIT_STREAMCOMMENT);
    require(d.Text() == "/*body*/" && d.Send(SCI_GETSELECTIONSTART) == 2 && d.Send(SCI_GETSELECTIONEND) == 6);
    ExecuteEditorCommand(d, IDM_EDIT_STREAMCOMMENT);
    require(d.Text() == "body");
    load("\"/*literal*/\"");
    d.Send(SCI_SETSEL, 3, 10);
    ExecuteEditorCommand(d, IDM_EDIT_STREAMCOMMENT);
    require(d.Text() == "\"/*/*literal*/*/\"");
    d.style = std::move(savedStyle);
    d.ConfigureLexer();
    d.Send(SCI_SETSAVEPOINT);
    d.encodingDirty = false;
}

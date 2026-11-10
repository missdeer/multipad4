#pragma once
#include "../src/EditClipboard.h"
#include "../src/EditExpression.h"
#include "../src/EditorCommands.h"
#include "../src/resource.h"
#include "EditScopeTests.h"
#include <source_location>
#include <stdexcept>
inline void RunEditorFeatureTests(EditorDocument &document) {
    auto require = [](bool condition, std::source_location where = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error("Advanced edit assertion at line " + std::to_string(where.line()));
    };
    auto load = [&](const char *text) {
        document.Send(SCI_SETREADONLY, FALSE);
        document.Load({text});
        document.search = {};
        document.Send(SCI_SELECTALL);
    };
    load("  α  \r\n  β\t\r\n");
    require(ExecuteEditorCommand(document, IDM_EDIT_TRIMLINES));
    require(document.Text() == "  α\r\n  β\r\n");
    document.Send(SCI_UNDO);
    require(document.Text() == "  α  \r\n  β\t\r\n");
    load("😀alpha\nβeta");
    ExecuteEditorCommand(document, IDM_EDIT_STRIP1STCHAR);
    require(document.Text() == "alpha\neta");
    load("one\ntwo\none");
    ExecuteEditorCommand(document, IDM_EDIT_REMOVEDUPLICATELINE);
    require(document.Text() == "two");
    load("中文 hello");
    ExecuteEditorCommand(document, IDM_EDIT_BASE64_ENCODE);
    auto encoded = document.Text();
    require(encoded == "5Lit5paHIGhlbGxv");
    ExecuteEditorCommand(document, IDM_EDIT_BASE64_DECODE);
    require(document.Text() == "中文 hello");
    load("中文 /?");
    ExecuteEditorCommand(document, IDM_EDIT_URLCOMPONENTENCODE);
    require(document.Text() == "%E4%B8%AD%E6%96%87%20%2F%3F");
    ExecuteEditorCommand(document, IDM_EDIT_URLDECODE);
    require(document.Text() == "中文 /?");
    load("a\n");
    ExecuteEditorCommand(document, IDM_EDIT_ESCAPECCHARS);
    require(document.Text() == "a\\n");
    ExecuteEditorCommand(document, IDM_EDIT_UNESCAPECCHARS);
    require(document.Text() == "a\n");
    load("aBc");
    ExecuteEditorCommand(document, IDM_EDIT_INVERTCASE);
    require(document.Text() == "AbC");
    load("中");
    ExecuteEditorCommand(document, IDM_EDIT_CHAR2HEX);
    require(document.Text() == "\\u4E2D");
    ExecuteEditorCommand(document, IDM_EDIT_HEX2CHAR);
    require(document.Text() == "中");
    load("é");
    document.encoding = files::FromCodePage(1252);
    ExecuteEditorCommand(document, IDM_EDIT_BASE64_ENCODE);
    require(document.Text() == "6Q==");
    ExecuteEditorCommand(document, IDM_EDIT_BASE64_DECODE);
    require(document.Text() == "é");
    ExecuteEditorCommand(document, IDM_EDIT_SHOW_HEX);
    require(document.Text() == "é[E9]");
    document.encoding = files::Encoding::Utf8;
    load("-0xFF");
    ExecuteEditorCommand(document, IDM_EDIT_NUM2DEC);
    require(document.Text() == "-255");
    load("42");
    ExecuteEditorCommand(document, IDM_EDIT_NUM2BIN);
    require(document.Text() == "0b101010");
    load("0xFF 0o10 0b11 256");
    ExecuteEditorCommand(document, IDM_EDIT_NUM2BIN);
    require(document.Text() == "0b11111111 0b1000 0b11 0b1_00000000");
    load("0012");
    ExecuteEditorCommand(document, CMD_INCREASENUM);
    require(document.Text() == "0013");
    load("abc123 abc456\n中文");
    document.Send(SCI_GOTOPOS, 0);
    document.search.regex = true;
    document.search.findText = L"abc([0-9]+)";
    document.search.replaceText = L"value=\\1";
    require(document.ReplaceAll() == 2);
    require(document.Text() == "value=123 value=456\n中文");
    document.Send(SCI_UNDO);
    require(document.Text() == "abc123 abc456\n中文");
    load("prefix123 prefixABC");
    document.search.regex = true;
    document.search.useBoostRegex = true;
    document.search.findText = L"(?<=prefix)[0-9]+";
    document.Send(SCI_GOTOPOS, 0);
    require(document.FindNext());
    require(document.Send(SCI_GETSELECTIONSTART) == 6);
    load("first\nsecond\n");
    document.Send(SCI_GOTOPOS, 0);
    document.search.regex = true;
    document.search.findText = L"^";
    document.search.replaceText = L">";
    require(document.ReplaceAll() >= 2);
    require(document.Text().starts_with(">first\n>second"));
    load("a1 a2 ab\nA3");
    document.search.wildcard = true;
    document.search.findText = L"a?";
    document.search.flags = SCFIND_MATCHCASE;
    require(document.FindAll() == 3);
    require(document.FindAll(true) == 3);
    require(document.Send(SCI_GETSELECTIONS) == 3);
    require(document.Send(SCI_INDICATORVALUEAT, 12, 0) != 0);
    load("ab");
    document.search.regex = true;
    document.search.wrap = false;
    document.search.findText = L"(?=.)";
    document.search.useBoostRegex = true;
    document.Send(SCI_GOTOPOS, 0);
    require(document.FindNext());
    require(document.Send(SCI_GETCURRENTPOS) == 0);
    require(document.FindNext());
    require(document.Send(SCI_GETCURRENTPOS) == 1);
    require(!document.FindNext());
    document.search.lastMatchStart = document.search.lastMatchEnd = -1;
    document.search.findText = L"$";
    document.Send(SCI_GOTOPOS, 0);
    require(document.FindNext());
    require(!document.FindNext());
    require(!document.FindNext());
    document.search.findText = L"[";
    bool invalid = false;
    try {
        document.ReplaceAll();
    } catch (const std::exception &) {
        invalid = true;
    }
    require(invalid);
    require(document.Text() == "ab");
    load("one\ttwo\nthree");
    document.search.transformBackslashes = true;
    document.search.findText = L"\\t";
    document.Send(SCI_GOTOPOS, 0);
    require(document.FindNext());
    require(document.Send(SCI_GETSELECTIONSTART) == 3);
    load("text");
    ExecuteEditorCommand(document, IDM_INSERT_UNICODE_ZWJ);
    require(document.Text() == "\xE2\x80\x8D");
    load("hello");
    ExecuteEditorCommand(document, IDM_EDIT_SHOW_HEX);
    require(document.Text() == "hello[68 65 6C 6C 6F]");
    load("$Date: old $\n$Date: second $");
    document.Send(SCI_GOTOPOS, 0);
    ExecuteEditorCommand(document, CMD_TIMESTAMPS);
    require(document.Text().find("old") == std::string::npos);
    require(document.Text().find("second") == std::string::npos);
    load("");
    ExecuteEditorCommand(document, IDM_EDIT_INSERT_GUID);
    require(document.Text().size() == 36);
    require(document.Text()[8] == '-' && document.Text()[13] == '-');
    require(document.Text().find_first_of("{}ABCDEF") == std::string::npos);
    load("");
    ExecuteEditorCommand(document, IDM_EDIT_INSERT_LOC_DATETIME);
    require(document.Text().size() == 19 && document.Text()[4] == '-' && document.Text()[10] == ' ');
    load("");
    ExecuteEditorCommand(document, IDM_EDIT_INSERT_UTC_DATETIME);
    require(document.Text().size() == 20 && document.Text()[10] == 'T' && document.Text().back() == 'Z');
    load("");
    document.runtime.dateTimeShort = L"%Y-%m";
    ExecuteEditorCommand(document, IDM_EDIT_INSERT_SHORTDATE);
    require(document.Text().size() == 7 && document.Text()[4] == '-');
    document.runtime.dateTimeShort.clear();
    load("retain");
    document.runtime.dateTimeShort = std::wstring(1000, L'x');
    bool longTemplate = false;
    try {
        ExecuteEditorCommand(document, IDM_EDIT_INSERT_SHORTDATE);
    } catch (const std::exception &) {
        longTemplate = true;
    }
    require(longTemplate && document.Text() == "retain");
    document.runtime.dateTimeShort.clear();
    load("é");
    document.search.findText = L"\\xC3\\xA9";
    document.search.transformBackslashes = true;
    document.Send(SCI_GOTOPOS, 0);
    require(document.FindNext());
    require(document.Send(SCI_GETSELECTIONEND) == 2);
    const char *clipboardReplacement = "literal\\1";
    document.Send(SCI_COPYTEXT, strlen(clipboardReplacement), reinterpret_cast<LPARAM>(clipboardReplacement));
    load("abc");
    document.search.regex = true;
    document.search.findText = L"(abc)";
    document.search.replaceText = L"^c";
    require(document.ReplaceAll() == 1);
    if (document.Text() != "literal\\1")
        throw std::runtime_error("Clipboard replacement actual=[" + document.Text() + "]");
    load("abc");
    ExecuteEditorCommand(document, CMD_ENCLOSE_TRIPLE_SQ);
    require(document.Text() == std::string(3, '\'') + "abc" + std::string(3, '\''));
    require(document.Send(SCI_GETSELECTIONSTART) == 3 && document.Send(SCI_GETSELECTIONEND) == 6);
    load("");
    ExecuteEditorCommand(document, CMD_ENCLOSE_TRIPLE_BT);
    require(document.Text() == "``````");
    require(document.Send(SCI_GETCURRENTPOS) == 3 && document.Send(SCI_GETANCHOR) == 3);
    auto clipboardText = [&] {
        require(OpenEditClipboard(document.hwnd));
        auto memory = GetClipboardData(CF_UNICODETEXT);
        std::wstring value;
        if (memory) {
            auto *text = static_cast<const wchar_t *>(GlobalLock(memory));
            if (text) {
                value = text;
                GlobalUnlock(memory);
            }
        }
        CloseClipboard();
        return files::ToUtf8(value);
    };
    load("one");
    document.Send(SCI_COPY);
    load("two");
    ExecuteEditorCommand(document, IDM_EDIT_COPYADD);
    require(clipboardText() == "one\r\n\r\ntwo");
    load("swap");
    ExecuteEditorCommand(document, IDM_EDIT_SWAP);
    require(document.Text() == "one\r\n\r\ntwo" && clipboardText() == "swap");
    load("");
    ExecuteEditorCommand(document, IDM_EDIT_SWAP);
    require(document.Text() == "swap");
    require(!IsClipboardFormatAvailable(CF_UNICODETEXT));
    load("first\n\t中😀");
    document.Send(SCI_SETTABWIDTH, 4);
    require(document.Send(SCI_GETCOLUMN, document.Send(SCI_GETLENGTH)) == 6);
    load("a\nbbb\ncc");
    document.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
    document.Send(SCI_SETRECTANGULARSELECTIONANCHOR, 1);
    document.Send(SCI_SETRECTANGULARSELECTIONCARET, 8);
    const auto rectMode = document.Send(SCI_SELECTIONISRECTANGLE);
    const auto selected0 = document.Send(SCI_GETLINESELENDPOSITION, 0),
               selected1 = document.Send(SCI_GETLINESELENDPOSITION, 1),
               selected2 = document.Send(SCI_GETLINESELENDPOSITION, 2);
    ExecuteEditorCommand(document, IDM_EDIT_PADWITHSPACES);
    if (document.Text() != "a \nbbb\ncc")
        throw std::runtime_error("Pad actual=[" + document.Text() + "] rect=" + std::to_string(rectMode) +
                                 " ends=" + std::to_string(selected0) + "," + std::to_string(selected1) + "," +
                                 std::to_string(selected2));
    require(document.Send(SCI_SELECTIONISRECTANGLE));
    auto beforeRectangle = document.Text();
    ExecuteEditorCommand(document, IDM_EDIT_BASE64_ENCODE);
    require(document.Text() == beforeRectangle);
    document.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    load("c\nb\nc");
    document.Send(SCI_GOTOPOS, 0);
    require(!EditorCommandState(&document, IDM_EDIT_REMOVEDUPLICATELINE).enabled);
    require(!EditorCommandState(&document, IDM_EDIT_SORTLINES).enabled);
    ExecuteEditorCommand(document, IDM_EDIT_REMOVEDUPLICATELINE);
    require(document.Text() == "c\nb\nc");
    document.Send(SCI_SETSEL, 0, 1);
    require(!EditorCommandState(&document, IDM_EDIT_SORTLINES).enabled);
    document.Send(SCI_SELECTALL);
    require(EditorCommandState(&document, IDM_EDIT_REMOVEDUPLICATELINE).enabled);
    ExecuteEditorCommand(document, IDM_EDIT_REMOVEDUPLICATELINE);
    require(document.Text() == "b");
    load("abc");
    document.Send(SCI_GOTOPOS, 0);
    require(!EditorCommandState(&document, IDM_EDIT_BASE64_ENCODE).enabled);
    ExecuteEditorCommand(document, IDM_EDIT_BASE64_ENCODE);
    require(document.Text() == "abc");
    load("color\té");
    document.Send(SCI_STYLESETFORE, STYLE_DEFAULT, RGB(1, 2, 3));
    ExecuteEditorCommand(document, IDM_EDIT_COPYRTF);
    require(OpenEditClipboard(document.hwnd));
    auto rich = GetClipboardData(RegisterClipboardFormatW(L"Rich Text Format"));
    require(rich != nullptr);
    auto *richBytes = static_cast<const char *>(GlobalLock(rich));
    require(richBytes != nullptr);
    std::string richText = richBytes;
    GlobalUnlock(rich);
    CloseClipboard();
    require(richText.starts_with("{\\rtf1"));
    require(richText.find("\\u233?") != std::string::npos);
    CLSID scriptClass{};
    if (SUCCEEDED(CLSIDFromProgID(L"JavaScript", &scriptClass))) {
        load("sqrt(9)+1");
        EvaluateDocumentExpression(document, true);
        require(document.Text() == "sqrt(9)+1 4");
        document.Send(SCI_UNDO);
        require(document.Text() == "sqrt(9)+1");
        load("({toString:function(){return '中文';}})");
        EvaluateDocumentExpression(document, false);
        require(document.Text().ends_with(" 中文"));
        load("(()");
        bool failed = false;
        try {
            EvaluateDocumentExpression(document, false);
        } catch (const std::exception &) {
            failed = true;
        }
        require(failed && document.Text() == "(()");
    }
    RunEditScopeTests(document);
    document.Send(SCI_SETSAVEPOINT);
    document.encodingDirty = false;
}

#pragma once
#include "../src/MainFrame.h"
#include "../src/StyleManager.h"
#include "../src/CompletionEngine.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

// Byte and line results from the actual document configuration path, including
// dialect detection, keyword installation and per-document folding properties.
inline std::string LexerDocumentBaseline(const EditorDocument& document) {
    std::ostringstream out;
    const auto* lexer = GetDocumentLexer(document);
    out << "language " << lexer->rid << " engine " << document.Send(SCI_GETLEXER)
        << " dialect " << document.style.resolvedDialect << '\n';
    out << "styles";
    for (LRESULT index = 0; index < document.Send(SCI_GETLENGTH); ++index)
        out << ' ' << document.Send(SCI_GETSTYLEINDEXAT, index);
    out << "\nfolds";
    for (LRESULT line = 0; line < document.Send(SCI_GETLINECOUNT); ++line)
        out << ' ' << document.Send(SCI_GETFOLDLEVEL, line);
    const auto& cache = GetCompletionCache(*lexer);
    const auto mask = [&](const char* name, const uint32_t (&values)[8]) {
        out << '\n' << name;
        for (auto value : values) out << ' ' << value;
    };
    mask("word", cache.CurrentWordCharSet);
    mask("prefix", cache.CharacterPrefixMask);
    mask("raw", cache.RawStringStyleMask);
    mask("generic", cache.GenericTypeStyleMask);
    mask("ignored", cache.IgnoreWordStyleMask);
    mask("comment", cache.CommentStyleMask);
    mask("string", cache.AllStringStyleMask);
    mask("plain", cache.PlainTextStyleMask);
    out << '\n';
    return out.str();
}

inline void RunLexerBaseline(MainFrame& frame, const std::filesystem::path& path, bool verify) {
    struct Fixture { const wchar_t* filename; const char* text; };
    const Fixture fixtures[] = {
        {L"baseline.cpp", "#include <vector>\n/* block\n comment */\nint main() {\n const char* s = R\"x(a\\b)x\";\n return 42; // comment\n}\n"},
        {L"baseline.py", "@decorator\ndef example(value):\n    text = f\"value={value!r}\"\n    if value:\n        return '''multi\nline'''\n# end\n"},
        {L"baseline.js", "/** @param {string} value */\nfunction example(value) {\n const text = `value=${value}`;\n return /a+/g.test(text);\n}\n"},
        {L"baseline.tsx", "interface Item { value: string; }\nconst render = (item: Item) => <div>{item.value}</div>;\n"},
        {L"baseline.html", "<!DOCTYPE html>\n<div class=\"test\">\n<script>const value = `x${1}`;</script>\n<style>div { color: red; }</style>\n</div>\n"},
        {L"baseline.scss", "$color: #abcdef;\n.test {\n &:hover { color: $color; }\n}\n"},
        {L"baseline.json", "{\n  \"key\": [true, null, 42, \"\\u4e2d\"]\n}\n"},
        {L"baseline.xml", "<?xml version=\"1.0\"?>\n<root a=\"1\"><!-- comment -->\n<![CDATA[value]]></root>\n"},
        {L"baseline.sh", "#!/bin/sh\nif test -n \"$HOME\"; then\n cat <<'END'\nvalue\nEND\nfi\n"},
        {L"baseline.md", "# Heading\n\n```cpp\nint value = 42;\n```\n*emphasis* [link](https://example.org)\n"},
        {L"baseline.m", "% matlab\nfunction value = example(x)\n  value = x + 1;\nend\n"},
        {L"unknown.extension", "#!/usr/bin/env python3\ndef example():\n    return 42\n"},
        {L"baseline.txt", "plain UTF-8: 中文\r\nsecond line\r\n"},
    };
    frame.NewDocument(); auto* first = frame.Active();
    frame.NewDocument(); auto* second = frame.Active();
    if (!first || !second || first == second) throw std::runtime_error("Cannot create baseline documents");
    second->path = L"isolated.py";
    second->Load({"def isolated():\n    return 'unchanged'\n"});
    ConfigureDocumentStyle(*second);
    const auto isolated = LexerDocumentBaseline(*second);
    std::ostringstream out;
    out << "MultiPad4 lexer behavior baseline v1\n";
    for (const auto& fixture : fixtures) {
        first->style = {};
        first->path = fixture.filename;
        first->Load({fixture.text});
        ConfigureDocumentStyle(*first);
        first->Send(SCI_COLOURISE, 0, -1);
        out << "fixture " << files::ToUtf8(fixture.filename) << '\n' << LexerDocumentBaseline(*first);
        if (LexerDocumentBaseline(*second) != isolated)
            throw std::runtime_error("Language configuration leaked between baseline documents");
    }
    // Switch languages and apply a document style override in the other view.
    const auto firstColor = first->Send(SCI_STYLEGETFORE, 0);
    second->style.overrides[(static_cast<unsigned>(NP2LEX_CPP) << 8)] = L"fore:#123456";
    if (!SelectDocumentScheme(*second, L"cpp")) throw std::runtime_error("Cannot select baseline C++ scheme");
    ConfigureDocumentStyle(*second);
    if (second->Send(SCI_STYLEGETFORE, 0) != RGB(0x12, 0x34, 0x56) || first->Send(SCI_STYLEGETFORE, 0) != firstColor)
        throw std::runtime_error("Document style override leaked between views");
    const auto unchanged = LexerDocumentBaseline(*first);
    second->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("/* changed */\nint value;\n"));
    second->Send(SCI_COLOURISE, 0, -1);
    if (LexerDocumentBaseline(*first) != unchanged)
        throw std::runtime_error("Editing background document changed baseline results");
    const auto data = out.str();
    if (verify) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read lexer behavior baseline");
        const std::string expected{std::istreambuf_iterator<char>(input), {}};
        if (expected != data) throw std::runtime_error("Lexer styles/folds/detection/completion differ from baseline");
    } else {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output) throw std::runtime_error("Cannot write lexer behavior baseline");
    }
    first->Send(SCI_SETSAVEPOINT);
    second->Send(SCI_SETSAVEPOINT);
}

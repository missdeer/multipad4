// Development-only export of the actual compiled, pinned notepad4 objects.
// Upstream data notices: 3rdparty/notepad4/License.txt.
#include <cstdint>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include "EditLexer.h"
#include "StaticLexerCatalog.h"
#include "LexerBaselineIds.h"
#include "CompletionEngine.h"
#include "resource.h"

namespace {
std::string Utf8(const wchar_t* value) {
    if (!value) throw std::runtime_error("Unexpected null string");
    const int length = static_cast<int>(wcslen(value));
    if (!length) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid UTF-16 in baseline");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, result.data(), count, nullptr, nullptr);
    return result;
}
std::string Escape(const std::string& value) {
    std::string result;
    for (unsigned char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\r': result += "&#13;"; break;
        case '\n': result += "&#10;"; break;
        case '\t': result += "&#9;"; break;
        default:
            if (ch < 32) throw std::runtime_error("Unrepresentable XML control character");
            result += static_cast<char>(ch);
        }
    }
    return result;
}
void Text(std::ostream& out, const char* field, const wchar_t* value) {
    out << "    <" << field;
    if (!value) out << " null=\"true\"/>\n";
    else out << '>' << Escape(Utf8(value)) << "</" << field << ">\n";
}
const char* StyleId(unsigned rid) {
    for (const auto& entry : styleBaselineIdentities) if (entry.rid == rid) return entry.id;
    throw std::runtime_error("Unmapped style resource " + std::to_string(rid));
}
void ExtraKeywords(std::ostream& out, const char* const* words) {
    for (unsigned index = 0; index < 4; ++index) {
        out << "      <slot index=\"" << index << '"';
        if (!words || !words[index]) out << " null=\"true\"/>\n";
        else out << '>' << Escape(words[index]) << "</slot>\n";
    }
}
std::string Export() {
    static_assert(std::size(lexerCatalog) == std::size(lexerBaselineIdentities));
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!-- P0 lossless baseline; internal engine numbers are not a public theme contract. -->\n"
        << "<lexer-baseline version=\"1\" upstream=\"3fcfb70fd0d3c8802100c85f7293ccb326aa8b64\""
        << " license=\"3rdparty/notepad4/License.txt\">\n";
    std::set<std::string> ids;
    std::set<int> resources;
    for (size_t index = 0; index < std::size(lexerCatalog); ++index) {
        const auto& lexer = *lexerCatalog[index];
        const auto& identity = lexerBaselineIdentities[index];
        if (identity.lexer != &lexer) throw std::runtime_error("Baseline identity/catalogue order mismatch");
        if (!ids.insert(identity.id).second || !resources.insert(lexer.rid).second)
            throw std::runtime_error("Duplicate language identity");
        if (lexer.szExtensions || lexer.szStyleBuf) throw std::runtime_error("Export must precede runtime mutations");
        if (lexer.iNameLen != wcslen(lexer.pszName)) throw std::runtime_error("Invalid language name length");
        out << "  <language id=\"" << identity.id << "\" legacyIndex=\"" << index
            << "\" symbol=\"" << identity.symbol << "\" source=\"" << identity.source << "\">\n";
#define FIELD(name) out << "    <field name=\"" #name "\" value=\"" << +lexer.name << "\"/>\n"
        FIELD(iLexer); FIELD(rid); FIELD(lexerAttr);
        FIELD(defaultTabWidth); FIELD(defaultIndentWidth); FIELD(defaultFoldLevelMask); FIELD(defaultFoldIgnoreInner);
        FIELD(escapeCharacterStart); FIELD(escapeCharacterStyle); FIELD(formatSpecifierStyle); FIELD(autoIdentWordStyle);
        FIELD(characterLiteralStyle); FIELD(noneSingleQuotedStyle); FIELD(operatorStyle); FIELD(operatorStyle2);
        FIELD(keywordAttr); FIELD(commentStyleMarker); FIELD(stringStyleFirst); FIELD(stringStyleLast);
        FIELD(keywordCount); FIELD(iStyleTheme); FIELD(bStyleChanged); FIELD(bUseDefaultCodeStyle);
        FIELD(iFavoriteOrder); FIELD(iStyleCount); FIELD(iNameLen);
#undef FIELD
        Text(out, "name", lexer.pszName);
        Text(out, "extensions", lexer.pszDefExt);
        Text(out, "runtimeExtensions", lexer.szExtensions);
        Text(out, "runtimeStyleBuffer", lexer.szStyleBuf);
        out << "    <keywords>\n";
        if (lexer.keywordCount > 16) throw std::runtime_error("Keyword attribute width exceeded");
        for (unsigned slot = 0; slot < lexer.keywordCount; ++slot) {
            const char* words = lexer.pszKeyWords[slot];
            out << "      <slot index=\"" << slot << "\" attributes=\"" << ((lexer.keywordAttr >> (4 * slot)) & 15) << '"';
            if (!words) out << " null=\"true\"/>\n";
            else out << '>' << Escape(words) << "</slot>\n";
        }
        out << "    </keywords>\n    <styles>\n";
        std::set<std::string> styleIds;
        for (unsigned slot = 0; slot < lexer.iStyleCount; ++slot) {
            const auto& style = lexer.Styles[slot];
            const std::string id = std::string(identity.id) + '.' + StyleId(style.rid);
            if (!styleIds.insert(id).second) throw std::runtime_error("Duplicate style identity: " + id);
            if (style.iNameLen != wcslen(style.pszName)) throw std::runtime_error("Invalid style name length");
            out << "      <style id=\"" << id << "\" legacyIndex=\"" << slot << "\" rid=\"" << style.rid
                << "\" nameLength=\"" << style.iNameLen << "\" packed=\"" << style.iStyle
                << "\" name=\"" << Escape(Utf8(style.pszName)) << "\">";
            if (!style.pszDefault) throw std::runtime_error("Unexpected null style default");
            out << Escape(Utf8(style.pszDefault)) << "</style>\n";
        }
        out << "    </styles>\n    <completionExtras>\n";
        ExtraKeywords(out, GetCompletionCache(lexer).extraKeywords);
        out << "    </completionExtras>\n  </language>\n";
    }
    for (int command : {IDM_LEXER_XSD, IDM_LEXER_XSLT, IDM_LEXER_APACHE, IDM_LEXER_PROPERTY_LIST}) {
        out << "  <dialectCompletion command=\"" << command << "\">\n";
        ExtraKeywords(out, GetCompletionExtraKeywords(command));
        out << "  </dialectCompletion>\n";
    }
    out << "</lexer-baseline>\n";
    return out.str();
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3 || (std::wstring(argv[1]) != L"--export" && std::wstring(argv[1]) != L"--verify"))
            throw std::runtime_error("Usage: lexer_baseline_export --export|--verify FILE");
        const auto data = Export();
        const auto path = std::filesystem::path(argv[2]);
        if (std::wstring(argv[1]) == L"--verify") {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open baseline");
            const std::string expected{std::istreambuf_iterator<char>(input), {}};
            if (expected != data) throw std::runtime_error("Compiled catalogue differs from baseline; review upstream/data changes before regenerating");
        } else {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            output.close();
            if (!output) throw std::runtime_error("Cannot write baseline");
        }
        std::cout << std::size(lexerCatalog) << " language configurations verified/exported\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

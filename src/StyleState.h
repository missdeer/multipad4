#pragma once
#include <string>
#include <map>
#include <cstdint>
#include <memory>
#include <vector>
struct ConfigurationSnapshot;
struct ThemeDefinition;
struct LanguageDefinition;
struct CompletionKeywordCache {
    std::weak_ptr<const ConfigurationSnapshot> configuration;
    const LanguageDefinition* lexer = nullptr;
    int dialect = 0;
    std::map<std::string, std::vector<std::string>, std::less<>> words;
};
struct DocumentStyleState {
    std::shared_ptr<const ConfigurationSnapshot> configuration;
    std::shared_ptr<const ThemeDefinition> theme;
    std::wstring themePath;
    std::string languageId;
    uint64_t appliedRules = 0;
    unsigned appliedLexer = 0;
    std::map<std::string,std::wstring> semanticOverrides;
    int scheme = 0, dialect = 0, resolvedScheme = 0, resolvedDialect = 0;
    bool dark = false, folding = true, matchBraces = true;
    bool paletteDark = false;
    bool highlightBlock = true;
    bool completion = true, completionIgnoreCase = false, autoIndent = true;
    bool latex = false;
    int colorCalltip = 0;
    int fontKind = -1;
    bool csvConfigured = false;
    int csvOptions = ('"' << 8) | ',';
    int tabWidth = 4, indentWidth = 4;
    bool tabsAsSpaces = false, useSchemeTabs = true;
    bool tabsOverride = false;
    bool tabIndents = true, backspaceUnindents = false, detectIndentation = true;
    int backspaceMask = 2;
    int wrapMode = 4, wrapIndent = 0, wrapSymbols = 2;
    bool wrapSymbolsShown = false;
    int lineHighlight = 2;
    bool highlightSubline = true;
    int longLineColumn = 80, longLineMode = 1;
    bool longLineEnabled = false;
    int appliedTabScheme = -1;
    std::map<int, int> completionOptions;
    std::wstring completionFillups = L";,()[]{}\\/";
    int markOccurrences = 1;
    std::wstring font = L"Consolas";
    int fontSize = 11;
    std::wstring codeFont, textFont;
    int codeFontSize = 11, textFontSize = 11;
    std::map<intptr_t, char> autoClosers;
    intptr_t colorStart = -1, colorEnd = -1;
    uint32_t calltipColor = 0;
    std::string calltipHex;
    uint64_t revision = 0, occurrenceRevision = 0;
    std::string occurrenceNeedle;
    intptr_t occurrenceCursor = 0;
    size_t occurrences = 0;
    int occurrenceFlags = 0;
    bool occurrencePending = false;
    bool completionPending = false;
    uint64_t completionEpoch = 0;
    intptr_t completionStart = 0, completionPosition = 0;
    uint64_t completionRevision = 0;
    std::weak_ptr<const ConfigurationSnapshot> completionConfiguration;
    const LanguageDefinition* completionLexer = nullptr;
    int completionDialect = 0;
    std::weak_ptr<const ThemeDefinition> completionTheme;
    std::shared_ptr<CompletionKeywordCache> completionKeywords;
    std::map<unsigned, std::wstring> overrides;
};

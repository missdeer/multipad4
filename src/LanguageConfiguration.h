#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "../3rdparty/notepad4/src/EditLexer.h"
struct LanguageStyle {
    unsigned iStyle = 0, rid = 0, iNameLen = 0;
    std::wstring id, name, value;
    const wchar_t* pszName = nullptr;
    const wchar_t* pszDefault = nullptr;
};
struct StyleCategoryBinding { std::string id, group; unsigned nativeStyle = 0; };
struct LanguageVariant { std::string id; int command = 0, parameter = 0; std::wstring extensions; };
struct LanguageDefinition : std::enable_shared_from_this<LanguageDefinition> {
    std::string id;
    int defaultDialect = 0, defaultCommand = 0;
    std::vector<LanguageVariant> variants;
    std::vector<std::wstring> aliases;
    uint64_t rulesVersion = 1;
    int iLexer = 0;
    int rid = 0;
    unsigned lexerAttr = 0;
    unsigned defaultTabWidth = 0;
    unsigned defaultIndentWidth = 0;
    unsigned defaultFoldLevelMask = 0;
    unsigned defaultFoldIgnoreInner = 0;
    unsigned escapeCharacterStart = 0;
    unsigned escapeCharacterStyle = 0;
    unsigned formatSpecifierStyle = 0;
    unsigned autoIdentWordStyle = 0;
    unsigned characterLiteralStyle = 0;
    unsigned noneSingleQuotedStyle = 0;
    unsigned operatorStyle = 0;
    unsigned operatorStyle2 = 0;
    uint64_t keywordAttr = 0;
    unsigned commentStyleMarker = 0;
    unsigned stringStyleFirst = 0;
    unsigned stringStyleLast = 0;
    unsigned keywordCount = 0;
    unsigned iStyleTheme = 0;
    unsigned bStyleChanged = 0;
    unsigned bUseDefaultCodeStyle = 0;
    unsigned iFavoriteOrder = 0;
    unsigned iStyleCount = 0;
    unsigned iNameLen = 0;
    std::wstring name, extensions;
    const wchar_t* pszName = nullptr;
    const wchar_t* pszDefExt = nullptr;
    std::vector<LanguageStyle> Styles;
    std::array<std::optional<std::string>,16> keywords;
    std::array<const char*,16> pszKeyWords{};
    std::array<std::optional<std::string>,4> extraWords;
    std::array<const char*,4> completionExtras{};
    std::vector<StyleCategoryBinding> categories;
    void Seal();
};
struct StylePatch {
    std::optional<uint32_t> foreground, background;
    std::optional<std::wstring> font;
    std::optional<int> size;
    std::optional<unsigned> decorations;
    bool clearForeground = false, clearBackground = false;
};
struct ImportDiagnostic { std::wstring source, message; unsigned line = 0, column = 0; std::string status; };
struct ThemeDefinition {
    uint64_t version = 1;
    std::wstring name;
    std::map<std::string, StylePatch> styles;
    std::map<std::string, std::wstring> legacy;
    std::map<std::pair<std::string,unsigned>,std::string> keywords;
    std::map<std::string, std::wstring> extensions;
    std::vector<ImportDiagnostic> diagnostics;
};
struct ConfigurationSnapshot {
    uint64_t version = 1;
    uint64_t rulesVersion = 1, themeVersion = 1;
    std::array<std::shared_ptr<const LanguageDefinition>,90> languages;
    std::array<const LanguageDefinition*,90> catalog{};
    std::shared_ptr<const ThemeDefinition> theme;
    std::map<std::pair<std::string,unsigned>,std::wstring> styleValues, userStyleValues;
    std::map<std::string,std::wstring> semanticStyles;
    std::map<int,int> commandLanguages;
    std::map<int,std::array<std::optional<std::string>,4>> dialectWords;
    std::map<int,std::array<const char*,4>> dialectPointers;
};
std::shared_ptr<const ConfigurationSnapshot> CurrentConfiguration();
const std::array<const LanguageDefinition*,90>& LanguageCatalog();
const LanguageDefinition& Language(const std::string& id);
const LanguageDefinition& BuiltinLanguage(const std::string& id);
int LanguageIndex(const std::string& id);
bool ReloadLanguageConfiguration(const std::filesystem::path& file = {});
void SetConfigurationUserDirectory(const std::filesystem::path& directory);
const std::vector<ImportDiagnostic>& ConfigurationDiagnostics();
uint64_t NextConfigurationVersion();
void RecordConfigurationDiagnostic(const std::filesystem::path& source,const std::exception& error);
std::filesystem::path ConfigurationDirectory();
std::shared_ptr<const ConfigurationSnapshot> LoadLanguageConfiguration(const std::filesystem::path& file);
std::shared_ptr<const ThemeDefinition> LoadTheme(const std::filesystem::path& file);
std::shared_ptr<const ThemeDefinition> DefaultTheme(bool dark);
void SaveNativeTheme(const ThemeDefinition& theme, const std::filesystem::path& file);
const char* const* ConfigurationDialectWords(int command);
std::wstring ThemeStyleValue(const ThemeDefinition& theme, const std::string& id);
std::shared_ptr<const ConfigurationSnapshot> ConfigurationWithTheme(std::shared_ptr<const ConfigurationSnapshot> base, const ThemeDefinition& theme);

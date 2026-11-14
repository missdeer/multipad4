#include "LanguageConfiguration.h"
#include "ConfigurationXml.h"
#include "LexerBindings.h"
#include <windows.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <algorithm>
namespace {
std::shared_ptr<const ConfigurationSnapshot> current;
std::filesystem::path userDirectory;
std::vector<ImportDiagnostic> diagnostics;
uint64_t nextSnapshotVersion=0;
uint64_t Hash(const std::string& value,uint64_t seed=14695981039346656037ULL) { for(unsigned char ch:value){seed^=ch;seed*=1099511628211ULL;}return seed; }
void Diagnostic(const std::filesystem::path& source,const std::exception& error) {
    ImportDiagnostic item{source.wstring(),ConfigurationWide(error.what()),0,0,"rejected"};diagnostics.push_back(item);
    OutputDebugStringW((item.source+L": "+item.message+L"\n").c_str());
}
std::shared_ptr<ConfigurationSnapshot> Decode(const ConfigurationXmlNode& root) {
    if(root.name!=L"languages"||root.Attribute(L"version")!=L"1")throw std::runtime_error("Unsupported language schema version");
    auto result=std::make_shared<ConfigurationSnapshot>();result->version=++nextSnapshotVersion;std::set<std::string> ids;
    for(const auto& node:root.children) {
        if(node.name==L"dialectCompletion") {
            int command=static_cast<int>(ConfigurationInteger(node.Attribute(L"command"),65535));
            if(!result->dialectWords.emplace(command,std::array<std::optional<std::string>,4>{}).second)throw std::runtime_error("Duplicate dialect");
            std::set<unsigned> slots;
            for(const auto& slot:node.children){unsigned i=static_cast<unsigned>(ConfigurationInteger(slot.Attribute(L"index"),3));if(!slots.insert(i).second)throw std::runtime_error("Duplicate dialect slot");if(slot.Attribute(L"null")!=L"true")result->dialectWords[command][i]=ConfigurationUtf8(slot.text);}
            if(slots.size()!=4)throw std::runtime_error("Incomplete dialect keywords");continue;
        }
        if(node.name!=L"language") { if(node.name.starts_with(L"x-"))continue;throw std::runtime_error("Unknown required language element"); }
        static const std::set<std::wstring> languageElements={L"field",L"name",L"extensions",L"keywords",L"styles",L"completionExtras",L"categories",L"variants",L"aliases"};
        for(const auto& element:node.children)if(!languageElements.contains(element.name)){
            if(element.name.starts_with(L"x-"))diagnostics.push_back({L"languages",L"Ignored optional extension: "+element.name,element.line,element.column,"unsupported"});
            else throw std::runtime_error("Unknown required language field at "+std::to_string(element.line)+":"+std::to_string(element.column));
        }
        auto language=std::make_shared<LanguageDefinition>();language->id=ConfigurationUtf8(node.Attribute(L"id"));
        auto index=static_cast<unsigned>(ConfigurationInteger(node.Attribute(L"legacyIndex"),89));
        const auto& binding=lexerBindings[index];
        if(language->id!=binding.id||result->languages[index]||!ids.insert(language->id).second)throw std::runtime_error("Duplicate/invalid language binding");
        std::map<std::wstring,uint64_t> values;
        for(const auto& field:node.children)if(field.name==L"field")if(!values.emplace(field.Attribute(L"name"),ConfigurationInteger(field.Attribute(L"value"))).second)throw std::runtime_error("Duplicate language field");
        if(!values.contains(L"iLexer")||values[L"iLexer"]>UINT32_MAX)throw std::runtime_error("Invalid field iLexer");
        language->iLexer=static_cast<decltype(language->iLexer)>(values[L"iLexer"]);
        if(!values.contains(L"rid")||values[L"rid"]>UINT32_MAX)throw std::runtime_error("Invalid field rid");
        language->rid=static_cast<decltype(language->rid)>(values[L"rid"]);
        if(!values.contains(L"lexerAttr")||values[L"lexerAttr"]>UINT32_MAX)throw std::runtime_error("Invalid field lexerAttr");
        language->lexerAttr=static_cast<decltype(language->lexerAttr)>(values[L"lexerAttr"]);
        if(!values.contains(L"defaultTabWidth")||values[L"defaultTabWidth"]>255)throw std::runtime_error("Invalid field defaultTabWidth");
        language->defaultTabWidth=static_cast<decltype(language->defaultTabWidth)>(values[L"defaultTabWidth"]);
        if(!values.contains(L"defaultIndentWidth")||values[L"defaultIndentWidth"]>255)throw std::runtime_error("Invalid field defaultIndentWidth");
        language->defaultIndentWidth=static_cast<decltype(language->defaultIndentWidth)>(values[L"defaultIndentWidth"]);
        if(!values.contains(L"defaultFoldLevelMask")||values[L"defaultFoldLevelMask"]>255)throw std::runtime_error("Invalid field defaultFoldLevelMask");
        language->defaultFoldLevelMask=static_cast<decltype(language->defaultFoldLevelMask)>(values[L"defaultFoldLevelMask"]);
        if(!values.contains(L"defaultFoldIgnoreInner")||values[L"defaultFoldIgnoreInner"]>255)throw std::runtime_error("Invalid field defaultFoldIgnoreInner");
        language->defaultFoldIgnoreInner=static_cast<decltype(language->defaultFoldIgnoreInner)>(values[L"defaultFoldIgnoreInner"]);
        if(!values.contains(L"escapeCharacterStart")||values[L"escapeCharacterStart"]>255)throw std::runtime_error("Invalid field escapeCharacterStart");
        language->escapeCharacterStart=static_cast<decltype(language->escapeCharacterStart)>(values[L"escapeCharacterStart"]);
        if(!values.contains(L"escapeCharacterStyle")||values[L"escapeCharacterStyle"]>255)throw std::runtime_error("Invalid field escapeCharacterStyle");
        language->escapeCharacterStyle=static_cast<decltype(language->escapeCharacterStyle)>(values[L"escapeCharacterStyle"]);
        if(!values.contains(L"formatSpecifierStyle")||values[L"formatSpecifierStyle"]>255)throw std::runtime_error("Invalid field formatSpecifierStyle");
        language->formatSpecifierStyle=static_cast<decltype(language->formatSpecifierStyle)>(values[L"formatSpecifierStyle"]);
        if(!values.contains(L"autoIdentWordStyle")||values[L"autoIdentWordStyle"]>255)throw std::runtime_error("Invalid field autoIdentWordStyle");
        language->autoIdentWordStyle=static_cast<decltype(language->autoIdentWordStyle)>(values[L"autoIdentWordStyle"]);
        if(!values.contains(L"characterLiteralStyle")||values[L"characterLiteralStyle"]>255)throw std::runtime_error("Invalid field characterLiteralStyle");
        language->characterLiteralStyle=static_cast<decltype(language->characterLiteralStyle)>(values[L"characterLiteralStyle"]);
        if(!values.contains(L"noneSingleQuotedStyle")||values[L"noneSingleQuotedStyle"]>255)throw std::runtime_error("Invalid field noneSingleQuotedStyle");
        language->noneSingleQuotedStyle=static_cast<decltype(language->noneSingleQuotedStyle)>(values[L"noneSingleQuotedStyle"]);
        if(!values.contains(L"operatorStyle")||values[L"operatorStyle"]>255)throw std::runtime_error("Invalid field operatorStyle");
        language->operatorStyle=static_cast<decltype(language->operatorStyle)>(values[L"operatorStyle"]);
        if(!values.contains(L"operatorStyle2")||values[L"operatorStyle2"]>255)throw std::runtime_error("Invalid field operatorStyle2");
        language->operatorStyle2=static_cast<decltype(language->operatorStyle2)>(values[L"operatorStyle2"]);
        if(!values.contains(L"keywordAttr")||values[L"keywordAttr"]>UINT64_MAX)throw std::runtime_error("Invalid field keywordAttr");
        language->keywordAttr=static_cast<decltype(language->keywordAttr)>(values[L"keywordAttr"]);
        if(!values.contains(L"commentStyleMarker")||values[L"commentStyleMarker"]>255)throw std::runtime_error("Invalid field commentStyleMarker");
        language->commentStyleMarker=static_cast<decltype(language->commentStyleMarker)>(values[L"commentStyleMarker"]);
        if(!values.contains(L"stringStyleFirst")||values[L"stringStyleFirst"]>255)throw std::runtime_error("Invalid field stringStyleFirst");
        language->stringStyleFirst=static_cast<decltype(language->stringStyleFirst)>(values[L"stringStyleFirst"]);
        if(!values.contains(L"stringStyleLast")||values[L"stringStyleLast"]>255)throw std::runtime_error("Invalid field stringStyleLast");
        language->stringStyleLast=static_cast<decltype(language->stringStyleLast)>(values[L"stringStyleLast"]);
        if(!values.contains(L"keywordCount")||values[L"keywordCount"]>255)throw std::runtime_error("Invalid field keywordCount");
        language->keywordCount=static_cast<decltype(language->keywordCount)>(values[L"keywordCount"]);
        if(!values.contains(L"iStyleTheme")||values[L"iStyleTheme"]>255)throw std::runtime_error("Invalid field iStyleTheme");
        language->iStyleTheme=static_cast<decltype(language->iStyleTheme)>(values[L"iStyleTheme"]);
        if(!values.contains(L"bStyleChanged")||values[L"bStyleChanged"]>1)throw std::runtime_error("Invalid field bStyleChanged");
        language->bStyleChanged=static_cast<decltype(language->bStyleChanged)>(values[L"bStyleChanged"]);
        if(!values.contains(L"bUseDefaultCodeStyle")||values[L"bUseDefaultCodeStyle"]>1)throw std::runtime_error("Invalid field bUseDefaultCodeStyle");
        language->bUseDefaultCodeStyle=static_cast<decltype(language->bUseDefaultCodeStyle)>(values[L"bUseDefaultCodeStyle"]);
        if(!values.contains(L"iFavoriteOrder")||values[L"iFavoriteOrder"]>255)throw std::runtime_error("Invalid field iFavoriteOrder");
        language->iFavoriteOrder=static_cast<decltype(language->iFavoriteOrder)>(values[L"iFavoriteOrder"]);
        if(!values.contains(L"iStyleCount")||values[L"iStyleCount"]>65535)throw std::runtime_error("Invalid field iStyleCount");
        language->iStyleCount=static_cast<decltype(language->iStyleCount)>(values[L"iStyleCount"]);
        if(!values.contains(L"iNameLen")||values[L"iNameLen"]>65535)throw std::runtime_error("Invalid field iNameLen");
        language->iNameLen=static_cast<decltype(language->iNameLen)>(values[L"iNameLen"]);
        if(values.size()!=26||(language->lexerAttr&~8191U)||language->name.size()>65535||language->iLexer!=binding.engine||language->rid!=binding.resource||language->keywordCount>16||!language->defaultTabWidth||language->iStyleCount>256)throw std::runtime_error("Unsupported engine/settings binding");
        language->name=node.Child(L"name").text;if(language->name.empty())throw std::runtime_error("Language name is required");language->extensions=node.Child(L"extensions").text;
        if(language->name.size()!=language->iNameLen)throw std::runtime_error("Language display length mismatch");
        std::set<unsigned> keywordSlots;
        for(const auto& slot:node.Child(L"keywords").children){if(slot.name!=L"slot")throw std::runtime_error("Unknown keyword element");unsigned i=static_cast<unsigned>(ConfigurationInteger(slot.Attribute(L"index"),15));if(!keywordSlots.insert(i).second||i>=language->keywordCount)throw std::runtime_error("Invalid keyword slot");if(ConfigurationInteger(slot.Attribute(L"attributes"),15)!=((language->keywordAttr>>(4*i))&15))throw std::runtime_error("Keyword attribute mismatch");if(slot.Attribute(L"null")!=L"true")language->keywords[i]=ConfigurationUtf8(slot.text);}
        if(keywordSlots.size()!=language->keywordCount)throw std::runtime_error("Missing keyword slot");
        std::set<std::wstring> styles;
        for(const auto& style:node.Child(L"styles").children) {
            if(style.name!=L"style")throw std::runtime_error("Unknown style element");
            LanguageStyle item;item.id=style.Attribute(L"id");item.name=style.Attribute(L"name");item.value=style.text;ValidateLegacyStyle(item.value);
            item.iStyle=static_cast<unsigned>(ConfigurationInteger(style.Attribute(L"packed"),UINT32_MAX));item.rid=static_cast<unsigned>(ConfigurationInteger(style.Attribute(L"rid"),65535));item.iNameLen=static_cast<unsigned>(ConfigurationInteger(style.Attribute(L"nameLength"),65535));
            if(ConfigurationInteger(style.Attribute(L"legacyIndex"))!=language->Styles.size()||!styles.insert(item.id).second||item.name.size()!=item.iNameLen)throw std::runtime_error("Invalid/duplicate style binding");
            language->Styles.push_back(std::move(item));
        }
        if(language->Styles.size()!=language->iStyleCount)throw std::runtime_error("Missing style definition");
        std::set<unsigned> extraSlots;
        for(const auto& slot:node.Child(L"completionExtras").children){unsigned i=static_cast<unsigned>(ConfigurationInteger(slot.Attribute(L"index"),3));if(!extraSlots.insert(i).second)throw std::runtime_error("Duplicate completion slot");if(slot.Attribute(L"null")!=L"true")language->extraWords[i]=ConfigurationUtf8(slot.text);}
        if(extraSlots.size()!=4)throw std::runtime_error("Missing completion slots");
        std::set<std::string> categories;
        for(const auto& category:node.Child(L"categories").children){StyleCategoryBinding item{ConfigurationUtf8(category.Attribute(L"id")),ConfigurationUtf8(category.Attribute(L"group")),static_cast<unsigned>(ConfigurationInteger(category.Attribute(L"native"),255))};if(!categories.insert(item.id).second||!styles.contains(ConfigurationWide(item.group)))throw std::runtime_error("Invalid category binding");language->categories.push_back(std::move(item));}
        const auto& variants=node.Child(L"variants");language->defaultDialect=static_cast<int>(ConfigurationInteger(variants.Attribute(L"default"),65535));language->defaultCommand=static_cast<int>(ConfigurationInteger(variants.Attribute(L"defaultCommand"),65535));
        std::set<std::string> variantIds;
        for(const auto& variant:variants.children){LanguageVariant value{ConfigurationUtf8(variant.Attribute(L"id")),static_cast<int>(ConfigurationInteger(variant.Attribute(L"command"),65535)),static_cast<int>(ConfigurationInteger(variant.Attribute(L"parameter"),65535)),variant.Attribute(L"extensions")};if(variant.name!=L"variant"||!variantIds.insert(value.id).second||!result->commandLanguages.emplace(value.command,index).second)throw std::runtime_error("Duplicate/invalid variant binding");language->variants.push_back(std::move(value));}
        for(const auto& alias:node.Child(L"aliases").children){if(alias.name!=L"alias"||alias.text.empty()||std::find(language->aliases.begin(),language->aliases.end(),alias.text)!=language->aliases.end())throw std::runtime_error("Duplicate/invalid language alias");language->aliases.push_back(alias.text);}
        std::ostringstream rules;
        for(const auto& [key,value]:values)if(key!=L"iStyleTheme"&&key!=L"bStyleChanged"&&key!=L"iFavoriteOrder"&&key!=L"iNameLen"&&key!=L"bUseDefaultCodeStyle")rules<<ConfigurationUtf8(key)<<'='<<value<<';';
        rules<<"dialect="<<language->defaultDialect;for(const auto& variant:language->variants)rules<<variant.id<<variant.command<<variant.parameter;
        for(const auto& word:language->keywords)rules<<(word?"value:"+*word:"null")<<'\0';
        for(const auto& word:language->extraWords)rules<<(word?"value:"+*word:"null")<<'\0';
        language->rulesVersion=Hash(rules.str());language->Seal();result->languages[index]=language;result->catalog[index]=language.get();
    }
    for(auto value:result->catalog)if(!value)throw std::runtime_error("Incomplete language catalogue");
    result->rulesVersion=14695981039346656037ULL;
    for(const auto& language:result->languages)result->rulesVersion=Hash(std::to_string(language->rulesVersion)+language->id,result->rulesVersion);
    for(auto& [command,words]:result->dialectWords){auto& pointers=result->dialectPointers[command];for(size_t i=0;i<4;++i)pointers[i]=words[i]?words[i]->c_str():nullptr;}
    return result;
}
void ValidateBinding(const ConfigurationSnapshot& candidate,const ConfigurationSnapshot& defaults) {
    if(candidate.dialectWords.size()!=defaults.dialectWords.size())throw std::runtime_error("Unsupported dialect bindings");
    for(const auto& [command,words]:candidate.dialectWords)if(!defaults.dialectWords.contains(command))throw std::runtime_error("Unknown dialect");
    for(size_t i=0;i<90;++i){const auto& a=*candidate.languages[i];const auto& b=*defaults.languages[i];
        if(a.defaultDialect!=b.defaultDialect||a.defaultCommand!=b.defaultCommand||a.variants.size()!=b.variants.size())throw std::runtime_error("Unsupported variant binding");
        for(size_t j=0;j<a.variants.size();++j)if(a.variants[j].id!=b.variants[j].id||a.variants[j].command!=b.variants[j].command||a.variants[j].parameter!=b.variants[j].parameter)throw std::runtime_error("Unsupported dialect parameter");
        if(a.Styles.size()!=b.Styles.size()||a.categories.size()!=b.categories.size())throw std::runtime_error("Style binding shape changed");
        for(size_t j=0;j<a.Styles.size();++j)if(a.Styles[j].id!=b.Styles[j].id||a.Styles[j].rid!=b.Styles[j].rid||a.Styles[j].iStyle!=b.Styles[j].iStyle)throw std::runtime_error("Unsupported native style binding");
        for(size_t j=0;j<a.categories.size();++j)if(a.categories[j].id!=b.categories[j].id||a.categories[j].group!=b.categories[j].group||a.categories[j].nativeStyle!=b.categories[j].nativeStyle)throw std::runtime_error("Unsupported semantic binding");
    }
}
std::shared_ptr<ConfigurationSnapshot> Defaults(){static auto defaults=Decode(ParseConfigurationXml(EmbeddedConfiguration(5520),L"embedded:languages"));return defaults;}
void ApplyLanguageOverrides(ConfigurationXmlNode& root,const ConfigurationXmlNode& patch) {
    if(patch.name!=L"language-overrides"||patch.Attribute(L"version")!=L"1")throw std::runtime_error("Unsupported language override schema");
    std::set<std::wstring> languages;
    for(const auto& change:patch.children){
        if(change.name.starts_with(L"x-"))continue;
        if(change.name!=L"language"||!languages.insert(change.Attribute(L"id")).second)throw std::runtime_error("Duplicate/invalid override language");
        auto target=std::find_if(root.children.begin(),root.children.end(),[&](const auto& node){return node.name==L"language"&&node.Attribute(L"id")==change.Attribute(L"id");});
        if(target==root.children.end())throw std::runtime_error("Unknown override language");
        std::set<std::wstring> fields;
        for(const auto& value:change.children){
            auto key=value.name+L":"+value.Attribute(L"name")+L":"+value.Attribute(L"index");
            if(!fields.insert(key).second)throw std::runtime_error("Duplicate override field");
            auto mode=value.Attribute(L"mode",L"replace");
            if(mode==L"inherit")continue;
            if(value.name==L"extensions"){
                if(mode!=L"replace")throw std::runtime_error("Extensions require replace or inherit");
                for(auto& node:target->children)if(node.name==L"extensions")node.text=value.text;
            }else if(value.name==L"field"){
                static const std::set<std::wstring> allowed={L"lexerAttr",L"defaultTabWidth",L"defaultIndentWidth",L"defaultFoldLevelMask",L"defaultFoldIgnoreInner",L"bUseDefaultCodeStyle"};
                if(mode!=L"replace"||!allowed.contains(value.Attribute(L"name")))throw std::runtime_error("Unsupported override property");
                for(auto& node:target->children)if(node.name==L"field"&&node.Attribute(L"name")==value.Attribute(L"name"))node.attributes[L"value"]=value.Attribute(L"value");
            }else if(value.name==L"slot"){
                unsigned index=static_cast<unsigned>(ConfigurationInteger(value.Attribute(L"index"),15));bool found=false;
                for(auto& words:target->children)if(words.name==L"keywords")for(auto& slot:words.children)if(slot.Attribute(L"index")==std::to_wstring(index)){
                    if(mode==L"append"){slot.text+=L" "+value.text;slot.attributes.erase(L"null");}
                    else if(mode==L"replace"){slot.text=value.text;slot.attributes.erase(L"null");}
                    else throw std::runtime_error("Invalid keyword mode");
                    auto attr=ConfigurationInteger(slot.Attribute(L"attributes"),15)&~uint64_t(KeywordAttr_PreSorted);
                    slot.attributes[L"attributes"]=std::to_wstring(attr);
                    for(auto& field:target->children)if(field.name==L"field"&&field.Attribute(L"name")==L"keywordAttr"){
                        auto bits=ConfigurationInteger(field.Attribute(L"value"));bits=(bits&~(uint64_t(15)<<(4*index)))|(attr<<(4*index));field.attributes[L"value"]=std::to_wstring(bits);
                    }found=true;
                }
                if(!found)throw std::runtime_error("Unsupported keyword slot");
            }else if(!value.name.starts_with(L"x-"))throw std::runtime_error("Unknown override element");
        }
    }
}
}
void LanguageDefinition::Seal() {
    pszName=name.c_str();pszDefExt=id=="global"?nullptr:extensions.c_str();
    for(size_t i=0;i<16;++i)pszKeyWords[i]=keywords[i]?keywords[i]->c_str():nullptr;
    for(size_t i=0;i<4;++i)completionExtras[i]=extraWords[i]?extraWords[i]->c_str():nullptr;
    for(auto& style:Styles){style.pszName=style.name.c_str();style.pszDefault=style.value.c_str();}
}
std::filesystem::path ConfigurationDirectory() {wchar_t filename[32768]{};GetModuleFileNameW(nullptr,filename,32768);return std::filesystem::path(filename).parent_path()/L"config";}
std::shared_ptr<const ConfigurationSnapshot> LoadLanguageConfiguration(const std::filesystem::path& path) {
    auto xml=ReadConfigurationXml(path);try{auto result=Decode(xml);ValidateBinding(*result,*Defaults());return result;}catch(const std::exception& error){throw std::runtime_error(ConfigurationUtf8(path.wstring())+":"+std::to_string(xml.line)+":"+std::to_string(xml.column)+": "+error.what());}
}
std::shared_ptr<const ConfigurationSnapshot> CurrentConfiguration(){if(!current)ReloadLanguageConfiguration();return current;}
const std::array<const LanguageDefinition*,90>& LanguageCatalog(){if(!current)ReloadLanguageConfiguration();return current->catalog;}
const LanguageDefinition& Language(const std::string& id){for(auto language:LanguageCatalog())if(language->id==id)return *language;throw std::runtime_error("Unknown language: "+id);}
const LanguageDefinition& BuiltinLanguage(const std::string& id){for(auto language:Defaults()->catalog)if(language->id==id)return *language;throw std::runtime_error("Unknown built-in language: "+id);}
int LanguageIndex(const std::string& id){const auto& catalog=LanguageCatalog();for(size_t i=0;i<catalog.size();++i)if(catalog[i]->id==id)return static_cast<int>(i);return 0;}
void SetConfigurationUserDirectory(const std::filesystem::path& directory){userDirectory=directory;}
const std::vector<ImportDiagnostic>& ConfigurationDiagnostics(){return diagnostics;}
uint64_t NextConfigurationVersion(){return ++nextSnapshotVersion;}
void RecordConfigurationDiagnostic(const std::filesystem::path& source,const std::exception& error){Diagnostic(source,error);}
bool ReloadLanguageConfiguration(const std::filesystem::path& file) {
    auto source=file.empty()?ConfigurationDirectory()/L"languages/default.xml":file;
    auto activeSource=source;
    try {
        auto xml=ReadConfigurationXml(source);
        auto candidate=Decode(xml);ValidateBinding(*candidate,*Defaults());
        if(file.empty()&&!userDirectory.empty()) {
            auto directory=userDirectory/L"config/languages";
            if(std::filesystem::exists(directory)){
                std::vector<std::filesystem::path> files;for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==L".xml")files.push_back(entry.path());std::sort(files.begin(),files.end());
                for(const auto& user:files){activeSource=user;auto patch=ReadConfigurationXml(user);if(patch.name==L"languages")xml=std::move(patch);else ApplyLanguageOverrides(xml,patch);}
                candidate=Decode(xml);ValidateBinding(*candidate,*Defaults());
            }
        }
        current=std::move(candidate);return true;
    }catch(const std::exception& error){Diagnostic(activeSource,error);if(!current)current=Defaults();return false;}
}
const char* const* ConfigurationDialectWords(int command){auto snapshot=CurrentConfiguration();auto found=snapshot->dialectPointers.find(command);return found==snapshot->dialectPointers.end()?nullptr:found->second.data();}

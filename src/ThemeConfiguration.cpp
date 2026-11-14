#include "LanguageConfiguration.h"
#include "ConfigurationXml.h"
#include <windows.h>
#include <set>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>
namespace {
uint32_t Color(const std::wstring& value) {
    if(value.size()!=6)throw std::runtime_error("Color must contain six RGB hex digits");
    unsigned result=0;for(wchar_t ch:value){unsigned digit=ch>=L'0'&&ch<=L'9'?ch-L'0':ch>=L'A'&&ch<=L'F'?ch-L'A'+10:ch>=L'a'&&ch<=L'f'?ch-L'a'+10:99;if(digit>15)throw std::runtime_error("Invalid RGB color");result=(result<<4)|digit;}return result;
}
std::wstring Hex(uint32_t value) {std::wostringstream out;out<<std::uppercase<<std::hex<<std::setw(6)<<std::setfill(L'0')<<value;return out.str();}
StylePatch ParsePatch(const ConfigurationXmlNode& node) {
    StylePatch result;
    static const std::set<std::wstring> attributes={L"id",L"fg",L"bg",L"font",L"size",L"decorations",L"clear",L"legacy",L"inherit"};for(const auto& [key,value]:node.attributes)if(!attributes.contains(key)&&!key.starts_with(L"x-"))throw std::runtime_error("Unknown native theme style field: "+ConfigurationUtf8(key));
    auto foreground=node.Attribute(L"fg");auto background=node.Attribute(L"bg");
    if(!foreground.empty())result.foreground=Color(foreground);
    if(!background.empty())result.background=Color(background);
    auto font=node.attributes.find(L"font");
    if(font!=node.attributes.end()){if(font->second.size()>128||font->second.find_first_of(L";\r\n")!=std::wstring::npos)throw std::runtime_error("Invalid font name");result.font=font->second;}
    auto size=node.Attribute(L"size");if(!size.empty()){int number=static_cast<int>(ConfigurationInteger(size,512));if(number<2)throw std::runtime_error("Invalid font size");result.size=number;}
    auto flags=node.Attribute(L"decorations");if(!flags.empty())result.decorations=static_cast<unsigned>(ConfigurationInteger(flags,7));
    std::wistringstream clear(node.Attribute(L"clear"));std::wstring field;
    while(std::getline(clear,field,L',')){
        if(field==L"fg"){result.foreground.reset();result.clearForeground=true;}
        else if(field==L"bg"){result.background.reset();result.clearBackground=true;}
        else if(field==L"font")result.font=L"";
        else if(field==L"size")result.size=0;
        else if(field==L"decorations")result.decorations=0;
        else throw std::runtime_error("Unknown theme clear field");
    }
    return result;
}
StylePatch Patch(const ConfigurationXmlNode& node) {
    try{return ParsePatch(node);}catch(const std::exception& error){throw ConfigurationValidationError(node.line,node.column,error.what());}
}
std::set<std::string> KnownStyles() {
    std::set<std::string> result{"editor.default","editor.selection","editor.selection.additional","editor.caret","editor.caret.additional","editor.fold.margin"};
    for(auto language:LanguageCatalog()){
        for(const auto& style:language->Styles)result.insert(ConfigurationUtf8(style.id));
        for(const auto& category:language->categories)result.insert(category.id);
    }return result;
}
void Report(ThemeDefinition& theme,const std::filesystem::path& file,const ConfigurationXmlNode& node,const std::string& status,const std::wstring& message) {
    theme.diagnostics.push_back({file.wstring(),message,node.line,node.column,status});
}
std::shared_ptr<ThemeDefinition> DecodeTheme(const ConfigurationXmlNode& root,const std::filesystem::path& source) {
    auto result=std::make_shared<ThemeDefinition>();result->name=root.Attribute(L"name",source.stem().wstring());auto known=KnownStyles();
    if(root.name==L"theme") {
        if(root.Attribute(L"version")!=L"1")throw std::runtime_error("Unsupported theme schema version");
        std::set<std::string> ids;
        for(const auto& node:root.children) {
            if(node.name==L"style") {
                auto id=ConfigurationUtf8(node.Attribute(L"id"));if(!ids.insert(id).second)throw std::runtime_error("Duplicate theme style");
                if(!known.contains(id)){Report(*result,source,node,"unsupported",L"Unknown semantic style: "+ConfigurationWide(id));continue;}
                if(node.Attribute(L"inherit")==L"true")continue;
                for(const auto& [key,value]:node.attributes)if(key.starts_with(L"x-"))Report(*result,source,node,"unsupported",L"Ignored optional style attribute: "+key);
                result->styles[id]=Patch(node);
                if(node.attributes.contains(L"legacy")){ValidateLegacyStyle(node.Attribute(L"legacy"));result->legacy[id]=node.Attribute(L"legacy");}
                Report(*result,source,node,"exact",ConfigurationWide(id));
            } else if(node.name==L"keywords") {
                auto id=ConfigurationUtf8(node.Attribute(L"language"));const auto& language=Language(id);
                unsigned slot=static_cast<unsigned>(ConfigurationInteger(node.Attribute(L"slot"),15));
                if(slot>=language.keywordCount||((language.keywordAttr>>(4*slot))&KeywordAttr_NoLexer))throw std::runtime_error("Unsupported keyword slot");
                auto key=std::make_pair(id,slot);if(result->keywords.contains(key))throw std::runtime_error("Duplicate keyword override");
                auto mode=node.Attribute(L"mode",L"append");if(mode!=L"append"&&mode!=L"replace"&&mode!=L"inherit")throw std::runtime_error("Invalid keyword merge mode");
                if(mode!=L"inherit")result->keywords[key]=(mode==L"replace"?"replace:":"append:")+ConfigurationUtf8(node.text);
            }else if(node.name==L"extensions"){
                auto id=ConfigurationUtf8(node.Attribute(L"language"));
                if(Language(id).id=="global"||result->extensions.contains(id))throw std::runtime_error("Duplicate/invalid extension language");
                if(node.text.size()>2048||node.text.find_first_of(L"<>|/\\:\r\n")!=std::wstring::npos)throw std::runtime_error("Invalid filename extensions");
                result->extensions[id]=node.text;
            }else if(node.name==L"diagnostic"){
                auto status=ConfigurationUtf8(node.Attribute(L"status"));if(status!="approximate"&&status!="fallback"&&status!="unsupported")throw std::runtime_error("Invalid persisted diagnostic status");
                result->diagnostics.push_back({node.Attribute(L"source"),node.text,static_cast<unsigned>(ConfigurationInteger(node.Attribute(L"line",L"0"),UINT32_MAX)),static_cast<unsigned>(ConfigurationInteger(node.Attribute(L"column",L"0"),UINT32_MAX)),status});
            }else if(node.name.starts_with(L"x-"))Report(*result,source,node,"unsupported",L"Ignored optional extension: "+node.name);
            else throw std::runtime_error("Unknown required theme element");
        }
    }else throw std::runtime_error("Expected a MultiPad4 theme document");
    std::string fingerprint;
    for(const auto& [id,patch]:result->styles)fingerprint+=id+ConfigurationUtf8(ThemeStyleValue(*result,id));
    for(const auto& [key,value]:result->keywords)fingerprint+=key.first+std::to_string(key.second)+value;
    for(const auto& [id,value]:result->extensions)fingerprint+=id+ConfigurationUtf8(value);
    result->version=14695981039346656037ULL;for(unsigned char ch:fingerprint){result->version^=ch;result->version*=1099511628211ULL;}
    return result;
}
std::string Escape(const std::wstring& value) {
    auto text=ConfigurationUtf8(value);std::string result;for(char ch:text)switch(ch){case '&':result+="&amp;";break;case '<':result+="&lt;";break;case '>':result+="&gt;";break;case '"':result+="&quot;";break;case '\r':result+="&#13;";break;case '\n':result+="&#10;";break;default:result+=ch;}return result;
}
}
std::shared_ptr<const ThemeDefinition> LoadTheme(const std::filesystem::path& file) {
    auto xml=ReadConfigurationXml(file);try{return DecodeTheme(xml,file);}catch(const ConfigurationValidationError& error){throw std::runtime_error(ConfigurationUtf8(file.wstring())+":"+std::to_string(error.line)+":"+std::to_string(error.column)+": "+error.what());}catch(const std::exception& error){throw std::runtime_error(ConfigurationUtf8(file.wstring())+":"+std::to_string(xml.line)+":"+std::to_string(xml.column)+": "+error.what());}
}
std::shared_ptr<const ThemeDefinition> DefaultTheme(bool dark) {
    static std::shared_ptr<const ThemeDefinition> light,darkTheme;auto& cached=dark?darkTheme:light;if(cached)return cached;
    auto source=ConfigurationDirectory()/L"themes"/(dark?L"dark.xml":L"light.xml");
    try{cached=LoadTheme(source);}catch(const std::exception& error){RecordConfigurationDiagnostic(source,error);cached=DecodeTheme(ParseConfigurationXml(EmbeddedConfiguration(dark?5522:5521),L"embedded:theme"),L"embedded:theme");}return cached;
}
std::wstring ThemeStyleValue(const ThemeDefinition& theme,const std::string& id) {
    std::wstring result;auto legacy=theme.legacy.find(id);if(legacy!=theme.legacy.end())result=legacy->second;
    auto found=theme.styles.find(id);
    if(found==theme.styles.end()){
        static const std::map<std::string,std::string> fallbacks={{"escapesequence","string"},{"formatspecifier","string"},{"macro","preprocessor"},{"taskmarker","comment"},{"formattedstring","string"},{"triplequotedfstring","triplequotedstring"},{"rawstring","string"},{"bytes","string"},{"triplequotedbytes","triplequotedstring"},{"templateliteral","string"},{"functiondefinition","function"}};
        auto dot=id.find('.');if(dot!=std::string::npos){auto fallback=fallbacks.find(id.substr(dot+1));if(fallback!=fallbacks.end())found=theme.styles.find(id.substr(0,dot+1)+fallback->second);}
    }
    if(found==theme.styles.end())return result;const auto& patch=found->second;
    if(patch.foreground)result+=L"; fore:#"+Hex(*patch.foreground);else if(patch.clearForeground)result+=L"; fore:inherit";
    if(patch.background)result+=L"; back:#"+Hex(*patch.background);else if(patch.clearBackground)result+=L"; back:inherit";
    if(patch.font)result+=L"; font:"+*patch.font;
    if(patch.size)result+=L"; size:"+std::to_wstring(*patch.size);
    if(patch.decorations){unsigned flags=*patch.decorations;result+=L"; bold:"+std::to_wstring(flags&1?1:0)+L"; italic:"+std::to_wstring(flags&2?1:0)+L"; underline:"+std::to_wstring(flags&4?1:0);}
    return result;
}
std::shared_ptr<const ConfigurationSnapshot> ConfigurationWithTheme(std::shared_ptr<const ConfigurationSnapshot> base,const ThemeDefinition& theme) {
    auto result=std::make_shared<ConfigurationSnapshot>(*base);result->themeVersion=theme.version;
    for(auto& [command,words]:result->dialectWords){auto& pointers=result->dialectPointers[command];for(size_t i=0;i<4;++i)pointers[i]=words[i]?words[i]->c_str():nullptr;}
    for(size_t i=0;i<90;++i){auto original=base->languages[i];std::shared_ptr<LanguageDefinition> changed;
        for(const auto& [key,words]:theme.keywords)if(key.first==original->id){if(!changed)changed=std::make_shared<LanguageDefinition>(*original);auto slot=key.second;auto body=words.substr(words.find(':')+1);if(words.starts_with("replace:"))changed->keywords[slot]=body;else changed->keywords[slot]=changed->keywords[slot].value_or("")+" "+body;
            // Modified keyword order must never retain the upstream PreSorted flag.
            changed->keywordAttr&=~(uint64_t(KeywordAttr_PreSorted)<<(4*slot));for(unsigned char ch:std::to_string(slot)+":"+std::to_string(words.size())+":"+words){changed->rulesVersion^=ch;changed->rulesVersion*=1099511628211ULL;}}
        if(changed){changed->Seal();result->languages[i]=changed;result->catalog[i]=changed.get();result->rulesVersion^=changed->rulesVersion;}
    }
    return result;
}
void SaveNativeTheme(const ThemeDefinition& theme,const std::filesystem::path& file) {
    std::ostringstream out;out<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<theme version=\"1\" name=\""<<Escape(theme.name)<<"\">\n";
    std::set<std::string> ids;for(const auto& [id,patch]:theme.styles)ids.insert(id);for(const auto& [id,value]:theme.legacy)ids.insert(id);
    for(const auto& id:ids){out<<"  <style id=\""<<id<<"\"";auto legacy=theme.legacy.find(id);if(legacy!=theme.legacy.end())out<<" legacy=\""<<Escape(legacy->second)<<'"';auto found=theme.styles.find(id);if(found!=theme.styles.end()){const auto& p=found->second;if(p.foreground)out<<" fg=\""<<ConfigurationUtf8(Hex(*p.foreground))<<'"';if(p.background)out<<" bg=\""<<ConfigurationUtf8(Hex(*p.background))<<'"';if(p.font)out<<" font=\""<<Escape(*p.font)<<'"';if(p.size&&*p.size)out<<" size=\""<<*p.size<<'"';if(p.decorations)out<<" decorations=\""<<*p.decorations<<'"';std::string clear;if(p.clearForeground)clear="fg";if(p.clearBackground)clear+=(clear.empty()?"":",")+std::string("bg");if(p.size&&!*p.size)clear+=(clear.empty()?"":",")+std::string("size");if(!clear.empty())out<<" clear=\""<<clear<<'"';}out<<"/>\n";}
    for(const auto& [id,value]:theme.extensions)out<<"  <extensions language=\""<<id<<"\">"<<Escape(value)<<"</extensions>\n";
    for(const auto& [key,words]:theme.keywords)out<<"  <keywords language=\""<<key.first<<"\" slot=\""<<key.second<<"\" mode=\""<<(words.starts_with("replace:")?"replace":"append")<<"\">"<<Escape(ConfigurationWide(words.substr(words.find(':')+1)))<<"</keywords>\n";
    for(const auto& diagnostic:theme.diagnostics)if(diagnostic.status!="exact")out<<"  <diagnostic status=\""<<diagnostic.status<<"\" source=\""<<Escape(std::filesystem::path(diagnostic.source).filename().wstring())<<"\" line=\""<<diagnostic.line<<"\" column=\""<<diagnostic.column<<"\">"<<Escape(diagnostic.message)<<"</diagnostic>\n";
    out<<"</theme>\n";auto data=out.str();if(data.size()>8*1024*1024)throw std::runtime_error("Saved theme exceeds the 8 MiB configuration limit");if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());auto temporary=file;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
    {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output.write(data.data(),static_cast<std::streamsize>(data.size()));output.close();if(!output)throw std::runtime_error("Cannot save theme");}
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace saved theme");
}

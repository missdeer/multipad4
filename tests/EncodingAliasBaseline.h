#pragma once
#include "../src/FileIO.h"
#include <algorithm>
#include <cctype>
namespace files {
inline bool OriginalParseEncodingName(const std::wstring& value, Encoding& encoding) {
    auto normalize=[](const std::string& text){std::string result;for(unsigned char c:text)if(isalnum(c))result+=static_cast<char>(tolower(c));return result;};
    auto name=normalize(ToUtf8(value));
    if(name=="utf16"||name=="utf16le"||name=="unicode"||name=="ucs2"){encoding=Encoding::Utf16LEPlain;return true;}
    if(name=="utf16be"||name=="unicodebe"){encoding=Encoding::Utf16BEPlain;return true;}
    if(name=="utf8bom"||name=="utf8sig"||name=="utf8signature"){encoding=Encoding::Utf8Bom;return true;}
    struct Alias { Encoding value; const char* names; };
    static const Alias aliases[]={
#include "../src/FileEncodingAliases.inc"
    };
    for(const auto& entry:aliases){std::string names=entry.names;size_t start=0;while(start<names.size()){
        auto end=names.find(',',start);auto item=names.substr(start,end==std::string::npos?end:end-start);
        if(!item.empty() && normalize(item)==name){encoding=entry.value;return true;}if(end==std::string::npos)break;start=end+1;
    }}
    if(name.starts_with("cp"))name.erase(0,2);else if(name.starts_with("windows"))name.erase(0,7);
    if(!name.empty() && std::all_of(name.begin(),name.end(),[](unsigned char c){return isdigit(c)!=0;})){
        unsigned long page=strtoul(name.c_str(),nullptr,10);if(page<=65535 && IsValidCodePage(page)){encoding=FromCodePage(page);return true;}
    }
    return false;
}
}

#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include "StyleState.h"
struct FileVariableState {
    bool active = false;
    DocumentStyleState baseline, applied;
    int baselineWrap = 0, appliedWrap = 0;
    int baselineEdge = 80, appliedEdge = 80;
};
namespace filevars {
// Adapted from pinned Edit.cpp::FileVars_Find/ParseInt/ParseStr. The upstream
// scan is case-sensitive and examines the final 512 bytes only if the first
// 512 contain no recognized variables.
inline size_t Find(const std::string& text, std::string name) {
    bool suffix=name.starts_with('*');if(suffix)name.erase(0,1);
    size_t position=0;
    while((position=text.find(name,position))!=std::string::npos){
        unsigned char previous=position?text[position-1]:0;position+=name.size();
        if(!(isalnum(previous)||previous=='-'||previous=='_'||previous=='.')||(suffix&&previous=='-')){
            while(position<text.size()&&(text[position]==' '||text[position]=='\t'))++position;
            if(position<text.size()&&(text[position]==':'||text[position]=='='||name.back()==' '))return position;
        }
    }
    return std::string::npos;
}
inline std::string String(const std::string& text,const char* name){
    auto position=Find(text,name);if(position==std::string::npos)return {};
    bool quoted=false;while(position<text.size()&&text[position]&&strchr(":=\"' \t",text[position])){quoted|=text[position]=='\''||text[position]=='"';++position;}
    auto start=position;while(position<text.size()){unsigned char c=text[position];if(!c||!(isalnum(c)||strchr("+-/_#",c)||(quoted&&c==' ')))break;++position;}
    auto value=text.substr(start,position-start);while(!value.empty()&&value.back()==' ')value.pop_back();return value;
}
inline bool Integer(const std::string& text,const char* name,int& value){
    auto token=String(text,name);if(token.empty())return false;char* end=nullptr;auto result=strtol(token.c_str(),&end,10);
    if(end!=token.c_str()){value=static_cast<int>((std::clamp)(result,-1000000L,1000000L));return true;}
    if(token[0]=='t'||token[0]=='y'){value=1;return true;}if(token[0]=='f'||token[0]=='n'){value=0;return true;}return false;
}
inline bool Recognized(const std::string& text){
    for(auto name:{"enable-local-variables","tab-width","*basic-indent","*basic-offset","indent-tabs-mode","*tab-always-indent","truncate-lines","fill-column","mode","encoding","charset","coding","fileencoding","/*!40101 SET NAMES "})if(Find(text,name)!=std::string::npos)return true;
    return false;
}
inline std::string Region(const std::string& text){auto first=text.substr(0,512);if(Recognized(first)||text.size()<=512)return first;return text.substr(text.size()-512);}
}

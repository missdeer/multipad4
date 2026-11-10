#pragma once
#include <cstdint>
struct EDITLEXER;
struct CompletionCache {
    uint32_t CurrentWordCharSet[8]{},CharacterPrefixMask[8]{},RawStringStyleMask[8]{},GenericTypeStyleMask[8]{},IgnoreWordStyleMask[8]{},CommentStyleMask[8]{},AllStringStyleMask[8]{},PlainTextStyleMask[8]{};
    const char* const* extraKeywords=nullptr;
    void Initialize(const EDITLEXER*) noexcept;
    bool Test(const uint32_t (&mask)[8],unsigned value)const{return value<256&&(mask[value>>5]&(1u<<(value&31)));}
    bool Word(unsigned ch)const{return Test(CurrentWordCharSet,ch);}
};
const CompletionCache& GetCompletionCache(const EDITLEXER&);

const char* const* GetCompletionExtraKeywords(int command);

#include <functional>
#include <string>
struct EditorDocument;
// Return 0: general keywords; 1: context only; 2: context and document words.
int AddContextCompletionWords(EditorDocument&,int currentStyle,int previousStyle,int prefix,int previous,const std::function<void(const char*)>&);

void AddDocumentCompletionWords(EditorDocument&,const std::string& root,bool ignoreCase,char prefix,unsigned timeout,const uint32_t (&ignoredStyleMask)[8],const std::function<void(std::string)>&);

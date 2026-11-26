#pragma once
#include "EncodingAliasBaseline.h"
#include <chrono>
#include <fstream>
#include <thread>
#include <atomic>
inline void RunEncodingAliasTests(const std::filesystem::path& directory) {
    auto require=[](bool ok){if(!ok)throw std::runtime_error("Encoding alias assertion");};
    struct Alias {files::Encoding value;const char* names;};using files::Encoding;
    static const Alias aliases[]={
#include "../src/FileEncodingAliases.inc"
    };
    std::vector<std::wstring> queries{L"utf16",L"unicode",L"utf16BE",L"ucs2",L"utf8-BOM",L"utf8-sig",L"utf8signature",L"unknown-name",L"",L"123456789012345678901234567890",L"windows-1252",L"cp932",L"1200",L"65535"};
    for(const auto& entry:aliases){std::string names=entry.names;size_t start=0;while(start<names.size()){auto end=names.find(',',start);auto item=names.substr(start,end==std::string::npos?end:end-start);if(!item.empty()){queries.push_back(files::FromUtf8(item));std::wstring decorated=L" ";for(char ch:item){decorated+=static_cast<wchar_t>(toupper(static_cast<unsigned char>(ch)));decorated+=L'-';}queries.push_back(decorated);}if(end==std::string::npos)break;start=end+1;}}
    std::atomic<bool> valid=true;
    std::vector<std::thread> workers;
    for(int worker=0;worker<8;++worker)workers.emplace_back([&]{for(const auto& query:queries){auto before=files::Encoding::Utf8,after=before;auto oldResult=files::OriginalParseEncodingName(query,before),newResult=files::ParseEncodingName(query,after);if(oldResult!=newResult||before!=after)valid=false;}});
    for(auto& worker:workers)worker.join();require(valid);
    bool rejected=false;try{files::Encoding encoding{};files::ParseEncodingName(std::wstring(1,static_cast<wchar_t>(0xd800)),encoding);}catch(const std::exception&){rejected=true;}require(rejected);
    std::filesystem::create_directories(directory);std::ofstream report(directory/L"encoding-alias-pairs.csv");report<<"case,mode,sample,milliseconds,checksum\n";
    for(int sample=0;sample<7;++sample)for(bool baseline:{true,false}){
        auto begin=std::chrono::steady_clock::now();uint64_t checksum=0;
        for(int repeat=0;repeat<8;++repeat)for(const auto& query:queries){files::Encoding encoding{};const auto ok=baseline?files::OriginalParseEncodingName(query,encoding):files::ParseEncodingName(query,encoding);checksum+=ok?static_cast<unsigned>(encoding):1;}
        report<<"all-aliases,"<<(baseline?"linear":"indexed")<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<checksum<<'\n';
        begin=std::chrono::steady_clock::now();checksum=0;
        for(int repeat=0;repeat<2000;++repeat)for(const auto& query:{L"unknown-encoding",L"windows-1252",L"iso-ir-100"}){files::Encoding encoding{};const auto ok=baseline?files::OriginalParseEncodingName(query,encoding):files::ParseEncodingName(query,encoding);checksum+=ok?static_cast<unsigned>(encoding):1;}
        report<<"late-numeric-miss,"<<(baseline?"linear":"indexed")<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<checksum<<'\n';
    }
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
    std::vector<std::filesystem::path> paths;
    for(int i=0;i<32;++i){auto path=directory/(L"modeline-"+std::to_wstring(i)+L".txt");std::ofstream file(path,std::ios::binary);file<<"# coding: iso-ir-100\n"<<std::string(8192,'a')<<"\xe9\n";file.close();require(bool(file));paths.push_back(path);}
    struct Reset {~Reset(){files::testing::encodingAliasParser=nullptr;}} reset;
    for(int sample=0;sample<7;++sample)for(bool baseline:{true,false}){
        files::testing::encodingAliasParser=baseline?files::OriginalParseEncodingName:nullptr;
        auto begin=std::chrono::steady_clock::now();size_t checksum=0;
        for(const auto& path:paths){const auto text=files::Read(path);require(text.encoding==files::FromCodePage(28591)&&text.utf8.ends_with("\xc3\xa9\n"));checksum+=text.utf8.size();}
        report<<"32-modeline-files,"<<(baseline?"linear":"indexed")<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<checksum<<'\n';
    }
#endif
    report.close();require(bool(report));
}

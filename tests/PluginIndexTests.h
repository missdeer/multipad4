#pragma once
#include "../src/MainFrame.h"
#include <array>
#include <chrono>
#include <fstream>
namespace pluginIndexTest {
inline int calls=0;
inline void __cdecl Command(){++calls;}
inline BOOL __cdecl Disabled(UINT){return FALSE;}
inline BOOL __cdecl First(MSG*){++calls;return FALSE;}
inline BOOL __cdecl Second(MSG*){calls+=100;return TRUE;}
inline LRESULT __cdecl Message(UINT,WPARAM,LPARAM){++calls;return 0;}
}
inline void RunPluginIndexTests(MainFrame& frame,const std::filesystem::path& directory) {
    using namespace pluginIndexTest;
    auto require=[](bool ok){if(!ok)throw std::runtime_error("Plugin index assertion");};
    PluginHost host;host.frame=&frame;
    std::vector<std::array<FuncItem,4>> tables(128);
    ShortcutKey shortcut{};shortcut._key='K';
    for(size_t i=0;i<tables.size();++i){
        PluginHost::Plugin plugin;plugin.module=L"module"+std::to_wstring(i)+L".dll";
        plugin.items=tables[i].data();plugin.count=4;plugin.message=Message;plugin.supportsFastView=i==127;
        for(int j=0;j<4;++j){tables[i][j]._cmdID=46000+static_cast<int>(i)*4+j;tables[i][j]._pFunc=Command;}
        if(i==1)plugin.preTranslate=First;
        if(i==100)plugin.preTranslate=Second;
        if(i==127){plugin.module=L"MODULE0.DLL";tables[i][0]._cmdID=46000;tables[i][0]._pShKey=&shortcut;}
        host.plugins.push_back(std::move(plugin));host.IndexPlugin(i);
    }
    require(host.commands.at(46000).normal.plugin==0 && host.commands.at(46000).fastView.plugin==127);
    require(host.modules.at(L"MoDuLe0.dLl")==0);
    calls=0;require(host.Execute(46000)&&calls==1);require(!host.Execute(123));
    host.plugins[0].commandEnabled=Disabled;require(!host.Execute(46000));host.plugins[0].commandEnabled=nullptr;
    ShortcutKey read{};require(host.Message(NPPM_GETSHORTCUTBYCMDID,46000,reinterpret_cast<LPARAM>(&read))&&read._key=='K');
    require(!host.Message(NPPM_GETSHORTCUTBYCMDID,123,reinterpret_cast<LPARAM>(&read)));
    calls=0;require(host.Message(NPPM_MSGTOPLUGIN,reinterpret_cast<WPARAM>(L"mOdUlE0.DlL"),1)&&calls==1);
    MSG msg{};calls=0;require(host.PreTranslate(&msg)&&calls==101&&host.pretranslators.size()==2);
    std::filesystem::create_directories(directory);
    std::ofstream report(directory/L"plugin-index-pairs.csv");report<<"case,mode,sample,milliseconds,checksum\n";
    for(int sample=0;sample<7;++sample)for(bool before:{true,false}){
        auto begin=std::chrono::steady_clock::now();size_t checksum=0;
        for(unsigned query=0;query<20000;++query){
            const UINT id=46000+(query%520);
            if(before){bool found=false;for(const auto& plugin:host.plugins){for(int j=0;j<plugin.count;++j)if(plugin.items[j]._cmdID==static_cast<int>(id)&&plugin.items[j]._pFunc){++checksum;found=true;break;}if(found)break;}}
            else {auto found=host.commands.find(id);if(found!=host.commands.end()&&found->second.normal.plugin!=SIZE_MAX)++checksum;}
        }
        auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        report<<"command-lookup,"<<(before?"linear":"indexed")<<','<<sample<<','<<elapsed<<','<<checksum<<'\n';
        require(checksum==19658);
        begin=std::chrono::steady_clock::now();calls=0;
        for(unsigned query=0;query<20000;++query){if(before){for(auto& plugin:host.plugins)if(plugin.preTranslate&&plugin.preTranslate(&msg))break;}else host.PreTranslate(&msg);}
        elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        report<<"pretranslate,"<<(before?"linear":"indexed")<<','<<sample<<','<<elapsed<<','<<calls<<'\n';require(calls==2020000);
    }
    std::vector<PluginAdmin::Entry> catalog(4096);
    std::map<std::wstring,size_t,PluginHost::ModuleLess> catalogIndex;
    for(size_t i=0;i<catalog.size();++i){catalog[i].folder=L"folder"+std::to_wstring(i);catalogIndex.emplace(catalog[i].folder,i);}
    for(int sample=0;sample<7;++sample)for(bool before:{true,false}){
        auto begin=std::chrono::steady_clock::now();size_t checksum=0;
        for(unsigned query=0;query<512;++query){const auto folder=L"FOLDER"+std::to_wstring((query*29)%4096);
            if(before){auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& entry){return _wcsicmp(entry.folder.c_str(),folder.c_str())==0;});require(found!=catalog.end());checksum+=found-catalog.begin();}
            else {auto found=catalogIndex.find(folder);require(found!=catalogIndex.end());checksum+=found->second;}
        }
        report<<"admin-catalog-join,"<<(before?"linear":"indexed")<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<checksum<<'\n';
    }
    // One folder in both trees must retain two independent rows.
    const auto fixture=directory/(L"admin-"+std::to_wstring(GetCurrentProcessId()));
    auto savedHome=frame.plugins.home,savedConfig=frame.plugins.config;
    struct Restore {PluginHost& host;std::filesystem::path home,config,fixture;~Restore(){host.home=home;host.config=config;std::error_code error;std::filesystem::remove_all(fixture,error);}} restore{frame.plugins,savedHome,savedConfig,fixture};
    for(const auto& root:{fixture,fixture/L"disabled"}){
        std::filesystem::create_directories(root/L"Base64");
        std::filesystem::copy_file(savedHome/L"Base64"/L"Base64.dll",root/L"Base64"/L"Base64.dll",std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::create_directories(fixture/L"Config");
    std::ofstream(fixture/L"Config"/L"nppPluginList.json")<<R"({"arch":"64","version":"test","npp-plugins":[{"folder-name":"Base64","display-name":"Catalog Base64"}]})";
    frame.plugins.home=fixture;frame.plugins.config=fixture/L"Config";
    PluginAdmin admin;admin.frame=&frame;
    auto begin=std::chrono::steady_clock::now();admin.Load();
    report<<"admin-load,indexed,0,"<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<",2\n";
    require(admin.entries.size()==2);
    bool enabled=false,disabled=false;for(const auto& entry:admin.entries){require(entry.present);if(entry.disabled){disabled=true;require(!entry.loaded);}else enabled=true;}
    require(enabled&&disabled);report.close();require(bool(report));
}


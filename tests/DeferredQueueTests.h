#pragma once
#include "CloseReentrancyTests.h"
#include <chrono>
#include <fstream>
inline void RunDeferredQueueTests(MainFrame& frame,const std::filesystem::path& directory) {
    auto require=[](bool ok){if(!ok)throw std::runtime_error("Deferred queue assertion");};
    std::vector<int> queue;size_t head=0;
    for(int i=0;i<8192;++i)queue.push_back(i);
    for(int i=0;i<8192;++i){require(TakePending(queue,head)==i);if(i==4000)queue.push_back(8192);}
    require(TakePending(queue,head)==8192&&queue.empty()&&head==0);
    auto& runtime=frame.runtime;
    require(runtime.pendingCommands.empty()&&runtime.pendingTabCloses.empty());
    auto first=reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x1234));auto second=reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x5678));
    runtime.pendingTabCloses={first,second};require(TakePending(runtime.pendingTabCloses,runtime.pendingTabCloseHead)==first);
    {FrameOperation operation(runtime,frame);frame.SendMessage(WM_CLOSE_TAB,reinterpret_cast<WPARAM>(first));frame.SendMessage(WM_CLOSE_TAB,reinterpret_cast<WPARAM>(first));}
    require(runtime.pendingTabCloses.size()-runtime.pendingTabCloseHead==2);
    runtime.pendingCommands.emplace_back(0x4D503444,L"");
    frame.SendMessage(WM_APP+0x104);require(!runtime.pendingTabCloses.empty()&&runtime.pendingCommands.size()==1);
    frame.SendMessage(WM_APP+0x104);require(runtime.pendingTabCloses.empty()&&runtime.pendingTabCloseHead==0&&runtime.pendingCommands.size()==1);
    runtime.pendingCommands.clear();runtime.pendingCommandHead=0;
    runtime.pendingTabCloses={first,second};runtime.pendingTabCloseHead=1;runtime.ClearPendingTabCloses();require(runtime.pendingTabCloses.empty()&&runtime.pendingTabCloseHead==0);
    RunCloseReentrancyTests(frame);
    std::filesystem::create_directories(directory);
    std::ofstream report(directory/L"deferred-queue-pairs.csv");report<<"case,mode,sample,milliseconds,checksum\n";
    for(int sample=0;sample<7;++sample)for(bool before:{true,false}){
        std::vector<std::pair<ULONG_PTR,std::wstring>> commands;commands.reserve(4096);
        for(ULONG_PTR i=0;i<4096;++i)commands.emplace_back(i,std::wstring(40,L'x'));
        size_t cursor=0,checksum=0;auto begin=std::chrono::steady_clock::now();
        while(!commands.empty()){
            if(before){auto value=std::move(commands.front());commands.erase(commands.begin());checksum+=value.first;}
            else checksum+=TakePending(commands,cursor).first;
        }
        require(checksum==4095*4096/2&&cursor==0);
        report<<"4096-commands,"<<(before?"front-erase":"head-cursor")<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<checksum<<'\n';
        std::vector<HWND> closes;closes.reserve(16384);for(UINT_PTR i=1;i<=16384;++i)closes.push_back(reinterpret_cast<HWND>(i));
        checksum=0;begin=std::chrono::steady_clock::now();
        while(!closes.empty()){
            HWND value;if(before){value=closes.front();closes.erase(closes.begin());}else value=TakePending(closes,cursor);
            checksum+=reinterpret_cast<UINT_PTR>(value);
        }
        require(checksum==16384u*16385u/2&&cursor==0);
        report<<"16384-closes,"<<(before?"front-erase":"head-cursor")<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<','<<checksum<<'\n';
    }
    report.close();require(bool(report));
}

#pragma once
#include "ConfigurationFeatureTests.h"
#include <chrono>
#include <psapi.h>
inline void RunConfigurationBenchmark(MainFrame& frame,const std::filesystem::path& output) {
#ifndef MULTIPAD4_LEXER_METRICS
    throw std::runtime_error("Build with MULTIPAD4_LEXER_METRICS=ON for character-read measurements");
#endif
    using Clock=std::chrono::steady_clock;
    auto elapsed=[](auto begin){return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();};
    std::filesystem::create_directories(output.parent_path());std::ofstream report(output);
    report<<"case,operation,mode,bytes,total_ms,lexer_ms,fold_ms,layout_redraw_ms,lexer_calls,requested_bytes,lexer_fetched_bytes,style_submitted_bytes,backtrack_bytes,working_set_bytes\n";
    const std::pair<const wchar_t*,std::string> fixtures[]={
        {L"long-lines.cpp",std::string(65536,'a')+"\n/* multi\nline */\nint value=42;\n"},
        {L"comments.cpp","/*\n"+std::string(128*1024,'c')+"\n*/\nint value=42;\n"},
        {L"nested.py","def example(value):\n    return f\"outer {f'inner {value}'}\"\n"},
        {L"embedded.html","<html><script>const text = `value=${1+2}`;</script><style>div {color:red;}</style></html>\n"},
    };
    auto first=frame.NewDocument();auto reference=frame.NewDocument();
    frame.ShowWindow(SW_SHOWNORMAL);
    auto record=[&](const wchar_t* name,const char* operation,const char* mode,EditorDocument& document,auto action){
        lexerMetrics::Reset();auto begin=Clock::now();action();double total=elapsed(begin);
        auto render=Clock::now();RedrawWindow(document.hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);double redraw=elapsed(render);
        auto metrics=lexerMetrics::counters;PROCESS_MEMORY_COUNTERS memory{};GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
        report<<ConfigurationUtf8(name)<<','<<operation<<','<<mode<<','<<document.Send(SCI_GETLENGTH)<<','<<total<<','<<metrics.lexMilliseconds<<','<<metrics.foldMilliseconds<<','<<redraw<<','<<metrics.calls<<','<<metrics.requestedBytes<<','<<metrics.fetchedBytes<<','<<metrics.submittedBytes<<','<<metrics.backtrackBytes<<','<<memory.WorkingSetSize<<'\n';
        lexerMetrics::enabled=false;
    };
    for(const auto& [name,seed]:fixtures){
        std::string text;for(unsigned i=0;i<8;++i)text+=seed;
        first->style={};first->path=name;
        auto sample=std::filesystem::temp_directory_path()/(L"MultiPad4-lexer-"+std::to_wstring(GetCurrentProcessId())+L"-"+name);
        {std::ofstream file(sample,std::ios::binary);file<<text;file.close();if(!file)throw std::runtime_error("Cannot create benchmark input");}
        record(name,"first-open","current",*first,[&]{first->Load(files::Read(sample));});
        std::filesystem::remove(sample);
        for(int sample=0;sample<5;++sample){
            first->style.theme=DefaultTheme(false);
            record(name,"theme-switch","forced-full-baseline",*first,[&]{first->style.appliedRules=0;ConfigureDocumentStyle(*first);});
            first->style.theme=DefaultTheme(true);
            record(name,"theme-switch","current",*first,[&]{ConfigureDocumentStyle(*first);});
        }
        record(name,"local-edit","current",*first,[&]{auto at=first->Send(SCI_GETLENGTH)/2;first->Send(SCI_SETTARGETSTART,at);first->Send(SCI_SETTARGETEND,at);first->Send(SCI_REPLACETARGET,1,reinterpret_cast<LPARAM>("x"));first->Send(SCI_COLOURISE,at,-1);});
        record(name,"cross-line-edit","current",*first,[&]{first->Send(SCI_SETTARGETSTART,0);first->Send(SCI_SETTARGETEND,1);first->Send(SCI_REPLACETARGET,1,reinterpret_cast<LPARAM>(" "));first->Send(SCI_COLOURISE,0,-1);});
        record(name,"jump-end","current",*first,[&]{first->Send(SCI_GOTOPOS,first->Send(SCI_GETLENGTH));first->Send(SCI_SCROLLCARET);});
        record(name,"idle-visible-edit","current",*first,[&]{
            first->Send(SCI_SETIDLESTYLING,SC_IDLESTYLING_TOVISIBLE);first->Send(SCI_GOTOPOS,0);
            first->Send(SCI_SETTARGETSTART,0);first->Send(SCI_SETTARGETEND,0);first->Send(SCI_REPLACETARGET,1,reinterpret_cast<LPARAM>(" "));
            RedrawWindow(first->hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
        });
        first->Send(SCI_COLOURISE,0,-1);first->Send(SCI_SETIDLESTYLING,SC_IDLESTYLING_NONE);
        reference->style={};reference->path=name;reference->Load({first->Text()});ConfigureDocumentStyle(*reference);
        // Semantic colors do not affect byte styles or folding.
        if(LexerDocumentBaseline(*first)!=LexerDocumentBaseline(*reference))throw std::runtime_error("Benchmark incremental/full output mismatch");
        first->Send(SCI_SETSAVEPOINT);reference->Send(SCI_SETSAVEPOINT);
    }
    auto begin=Clock::now();auto configuration=LoadLanguageConfiguration(ConfigurationDirectory()/L"languages/default.xml");
    report<<"configuration,load,current,0,"<<elapsed(begin)<<",0,0,0,0,0,0,0,0,0\n";
    begin=Clock::now();auto theme=LoadTheme(ConfigurationDirectory()/L"themes/dark.xml");
    report<<"theme-configuration,load,current,0,"<<elapsed(begin)<<",0,0,0,0,0,0,0,0,0\n";
    report.close();if(!report)throw std::runtime_error("Cannot write benchmark report");
}
inline void RunThemeCompatibilityReport(MainFrame& frame,const std::filesystem::path& directory,const std::filesystem::path& output) {
    std::ofstream report(output);report<<"theme\texact\tapproximate\tfallback\tunsupported\n";
    size_t count=0;
    struct Probe { EditorDocument* document; unsigned numberStyle; const char* language; };
    std::vector<Probe> probes;
    for(const auto& item:{std::pair{L"theme.cpp",SCE_C_NUMBER},std::pair{L"theme.py",SCE_PY_NUMBER},std::pair{L"theme.js",SCE_JS_NUMBER}}){auto document=frame.NewDocument();if(!document)throw std::runtime_error("Cannot create theme probe");document->path=item.first;document->Load({"value = 42;\n"});ConfigureDocumentStyle(*document);probes.push_back({document,static_cast<unsigned>(item.second),item.second==SCE_C_NUMBER?"cpp":item.second==SCE_PY_NUMBER?"python":"javascript"});}

    for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==L".xml"){
        auto theme=LoadTheme(entry.path());std::map<std::string,unsigned> counts;
        for(const auto& diagnostic:theme->diagnostics)++counts[diagnostic.status];
        for(auto& probe:probes){auto& document=*probe.document;document.style.theme=theme;ConfigureDocumentStyle(document);
            if(document.Send(SCI_GETSTYLEINDEXAT,8)!=probe.numberStyle)throw std::runtime_error("Theme probe changed numeric lexical class");
            auto number=theme->styles.find(std::string(probe.language)+".number");
            if(number!=theme->styles.end()){
                const auto& patch=number->second;
                if(patch.foreground){auto color=*patch.foreground;if(document.Send(SCI_STYLEGETFORE,probe.numberStyle)!=RGB((color>>16)&255,(color>>8)&255,color&255))throw std::runtime_error("Theme number foreground mismatch");}
                if(patch.decorations){auto flags=*patch.decorations;if(bool(document.Send(SCI_STYLEGETBOLD,probe.numberStyle))!=bool(flags&1)||bool(document.Send(SCI_STYLEGETITALIC,probe.numberStyle))!=bool(flags&2)||bool(document.Send(SCI_STYLEGETUNDERLINE,probe.numberStyle))!=bool(flags&4))throw std::runtime_error("Theme font decoration reset mismatch");}
            }
            document.Send(SCI_SETSAVEPOINT);
        }

        report<<ConfigurationUtf8(entry.path().filename().wstring())<<'\t'<<counts["exact"]<<'\t'<<counts["approximate"]<<'\t'<<counts["fallback"]<<'\t'<<counts["unsupported"]<<'\n';++count;
    }
    if(!count)throw std::runtime_error("No themes found");report.close();if(!report)throw std::runtime_error("Cannot write compatibility report");
}

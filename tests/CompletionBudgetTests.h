#pragma once
#include "MainFrame.h"
#include "StyleManager.h"
#include <source_location>
#include <chrono>
#include <imm.h>
#include <psapi.h>
struct CompletionTrace {
    SciFnDirect original;
    HWND window;
    size_t copied = 0, styles = 0, paints = 0;
    LRESULT largestStyle = 0;
    std::function<void()> afterStyle;
    bool failNextRange = false;
    EditorDocument* document = nullptr;
    bool synchronousInput = false;
    std::string candidates;
};
inline thread_local CompletionTrace* completionTrace = nullptr;
inline LRESULT CALLBACK CompletionNotifyTrace(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto& trace=*reinterpret_cast<CompletionTrace*>(data);
    if(message==WM_NOTIFY && trace.synchronousInput){
        const auto& notification=*reinterpret_cast<const NMHDR*>(l);
        if(notification.hwndFrom==trace.window && notification.code==SCN_CHARADDED){
            auto& d=*trace.document;
            if(d.Send(SCI_AUTOCACTIVE) || d.Send(SCI_GETREADONLY))return DefSubclassProc(window,message,w,l);
            d.style.completionKeywords.reset();
            const auto position=d.Send(SCI_GETCURRENTPOS),styled=d.Send(SCI_GETENDSTYLED);
            if(styled<position)d.Send(SCI_COLOURISE,styled,position);
        }
    }
    return DefSubclassProc(window,message,w,l);
}
inline LRESULT CALLBACK CompletionPaintTrace(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    if(message==WM_PAINT)++reinterpret_cast<CompletionTrace*>(data)->paints;
    return DefSubclassProc(window,message,w,l);
}
inline sptr_t CompletionDirect(sptr_t pointer, unsigned int message, uptr_t w, sptr_t l) {
    auto& trace=*completionTrace;
    if(message==SCI_AUTOCSHOW)trace.candidates=reinterpret_cast<const char*>(l);
    if(message==SCI_GETTEXTRANGEFULL && trace.failNextRange){trace.failNextRange=false;throw std::runtime_error("Injected completion prefix failure");}
    if(message==SCI_GETTEXTRANGEFULL){const auto& range=*reinterpret_cast<const Sci_TextRangeFull*>(l);trace.copied+=static_cast<size_t>(range.chrg.cpMax-range.chrg.cpMin);}
    if(message==SCI_COLOURISE){++trace.styles;trace.largestStyle=(std::max)(trace.largestStyle,static_cast<LRESULT>(l)-static_cast<LRESULT>(w));}
    const auto result=trace.original ? trace.original(pointer,message,w,l) : SendMessageW(trace.window,message,w,l);
    if(message==SCI_COLOURISE && trace.afterStyle){auto callback=std::move(trace.afterStyle);trace.afterStyle={};callback();}
    return result;
}
inline void RunCompletionBudgetTests(MainFrame& frame) {
    auto require=[](bool value,std::source_location where=std::source_location::current()){if(!value)throw std::runtime_error("Completion budget regression at "+std::to_string(where.line()));};
    auto* d=frame.NewDocument();require(d!=nullptr);d->path=L"completion.cpp";SelectDocumentScheme(*d,L"cpp");
    d->style.completion=true;d->style.completionOptions[IDC_AUTOC_SCAN_DOCUMENT_WORDS]=0;
    d->style.completionOptions[IDC_AUTOC_ENGLISH_IME_ONLY]=0;
    d->style.completionOptions[IDC_AUTOC_MIN_WORD_LENGTH]=1;d->style.completionOptions[IDC_AUTOC_ONLY_DOCUMENT_WORDS]=0;
    d->style.markOccurrences=0;SetFocus(d->hwnd);
    SCNotification typed{};typed.nmhdr.code=SCN_CHARADDED;typed.ch='t';typed.characterSource=SC_CHARACTERSOURCE_DIRECT_INPUT;
    {
        CompletionTrace trace{d->directFunction,d->hwnd};trace.document=d;completionTrace=&trace;d->directFunction=CompletionDirect;
        require(SetWindowSubclass(d->hwnd,CompletionPaintTrace,1,reinterpret_cast<DWORD_PTR>(&trace))!=FALSE);
        require(SetWindowSubclass(GetParent(d->hwnd),CompletionNotifyTrace,1,reinterpret_cast<DWORD_PTR>(&trace))!=FALSE);
        struct Restore{EditorDocument& d;SciFnDirect original;~Restore(){RemoveWindowSubclass(GetParent(d.hwnd),CompletionNotifyTrace,1);RemoveWindowSubclass(d.hwnd,CompletionPaintTrace,1);d.directFunction=original;completionTrace=nullptr;}}restore{*d,trace.original};
        d->Load({std::string(100000,'a')});d->Send(SCI_GOTOPOS,100000);trace.copied=trace.styles=0;
        HandleStyleNotification(*d,typed);require(trace.copied==0 && trace.styles==0 && !d->style.completionPending);
        d->Load({"ret"});d->Send(SCI_GOTOPOS,3);SetFocus(d->hwnd);d->Send(SCI_STARTSTYLING,0);
        trace.afterStyle=[&]{d->Send(SCI_ADDTEXT,1,reinterpret_cast<LPARAM>("x"));};HandleStyleNotification(*d,typed);
        require(d->Text()=="retx" && !d->Send(SCI_AUTOCACTIVE));
        std::string distant;for(int i=0;i<50000;++i)distant+="int value;\n";distant+="ret";
        auto request=[&]{d->Send(SCI_AUTOCCANCEL);d->Load({distant});d->Send(SCI_GOTOPOS,distant.size());SetFocus(d->hwnd);d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);HandleStyleNotification(*d,typed);if(!d->style.completionPending || d->Send(SCI_AUTOCACTIVE))fprintf(stderr,"Completion request focus=%d composing=%d readonly=%lld large=%d enabled=%d pos=%lld styled=%lld minimum=%d\n",GetFocus()==d->hwnd,d->completionComposing,d->Send(SCI_GETREADONLY),d->LargeFile(),d->style.completion,d->Send(SCI_GETCURRENTPOS),d->Send(SCI_GETENDSTYLED),d->style.completionOptions[IDC_AUTOC_MIN_WORD_LENGTH]);require(d->style.completionPending && !d->Send(SCI_AUTOCACTIVE));};
        request();frame.SendMessage(WM_DOCUMENT_CHANGED,1);require(frame.runtime.workTimerArmed);trace.styles=trace.copied=0;trace.largestStyle=0;
        const auto started=std::chrono::steady_clock::now();
        ProcessStyleBackground(*d,1);require(trace.styles<=1 && trace.largestStyle<=4096 && d->Send(SCI_GETENDSTYLED)<d->Send(SCI_GETLENGTH));
        // Same-token typing refreshes the pending revision and prefix.
        d->Send(SCI_ADDTEXT,1,reinterpret_cast<LPARAM>("u"));typed.ch='u';HandleStyleNotification(*d,typed);
        require(d->style.completionPending && d->style.completionPosition==d->Send(SCI_GETCURRENTPOS) && d->style.completionRevision==d->style.revision);
        for(int i=0;i<300 && d->style.completionPending;++i)frame.SendMessage(WM_TIMER,3);
        require(!d->style.completionPending && d->Send(SCI_AUTOCACTIVE));d->Send(SCI_AUTOCCANCEL);
        const auto completed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        // A nearly completed request must not become synchronous on the next typed byte.
        typed.ch='t';request();d->Send(SCI_STARTSTYLING,d->Send(SCI_GETLENGTH)-1024);d->Send(SCI_ADDTEXT,1,reinterpret_cast<LPARAM>("u"));typed.ch='u';trace.styles=0;HandleStyleNotification(*d,typed);
        require(d->style.completionPending && trace.styles==0 && d->style.completionRevision==d->style.revision);
        typed.ch='t';request();d->Send(SCI_GOTOPOS,0);ProcessStyleBackground(*d,1);require(!d->style.completionPending && !d->Send(SCI_AUTOCACTIVE));
        request();d->Send(SCI_ADDTEXT,1,reinterpret_cast<LPARAM>("x"));ProcessStyleBackground(*d,1);require(!d->style.completionPending);
        request();d->Send(SCI_SETMULTIPLESELECTION,TRUE);d->Send(SCI_ADDSELECTION,0,0);d->Send(SCI_SETMAINSELECTION,0);ProcessStyleBackground(*d,1);require(!d->style.completionPending);d->Send(SCI_CLEARSELECTIONS);
        request();{const auto saved=d->style;SendMessageW(d->hwnd,WM_KILLFOCUS,0,0);d->style=saved;SetFocus(d->hwnd);ProcessStyleBackground(*d,1);require(!d->style.completionPending);}
        request();SendMessageW(d->hwnd,WM_IME_STARTCOMPOSITION,0,0);ProcessStyleBackground(*d,1);require(!d->style.completionPending && d->completionComposing);SendMessageW(d->hwnd,WM_IME_COMPOSITION,0,GCS_RESULTSTR);require(!d->completionComposing);SendMessageW(d->hwnd,WM_IME_ENDCOMPOSITION,0,0);
        request();SendMessageW(d->hwnd,WM_IME_STARTCOMPOSITION,0,0);SendMessageW(d->hwnd,WM_IME_CHAR,'u',0);require(!d->completionComposing);SendMessageW(d->hwnd,WM_IME_ENDCOMPOSITION,0,0);
        request();d->style.configuration=std::make_shared<ConfigurationSnapshot>(*d->style.configuration);ProcessStyleBackground(*d,1);require(!d->style.completionPending);
        request();++d->style.resolvedDialect;ProcessStyleBackground(*d,1);require(!d->style.completionPending);--d->style.resolvedDialect;
        request();d->style.theme=std::make_shared<ThemeDefinition>();ProcessStyleBackground(*d,1);require(!d->style.completionPending);
        request();d->Send(SCI_STARTSTYLING,d->Send(SCI_GETLENGTH));trace.failNextRange=true;ProcessStyleBackground(*d,1);
        require(!d->style.completionPending && !d->Send(SCI_AUTOCACTIVE) && !trace.failNextRange);
        // Explicit completion still styles synchronously through the same distant gap.
        request();trace.styles=0;ExecuteStyleCommand(frame,*d,IDM_EDIT_COMPLETEWORD);require(!d->style.completionPending && trace.styles>0);
        d->Send(SCI_AUTOCCANCEL);
        // A long line waits until the input callback returns; the timer never forces it.
        const bool wasVisible=frame.IsWindowVisible();
        struct RestoreVisibility{MainFrame& frame;bool visible;~RestoreVisibility(){if(!visible)frame.ShowWindow(SW_HIDE);}}visibility{frame,wasVisible};
        frame.ShowWindow(SW_SHOWNORMAL);SetForegroundWindow(frame);SetFocus(d->hwnd);frame.UpdateWindow();
        {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
        request();for(int i=0;i<20;++i)HandleStyleNotification(*d,typed);
        {MSG message{};require(PeekMessageW(&message,frame,WM_COMPLETION_CONTINUE,WM_COMPLETION_CONTINUE,PM_REMOVE)!=FALSE);
            require(!PeekMessageW(&message,frame,WM_COMPLETION_CONTINUE,WM_COMPLETION_CONTINUE,PM_NOREMOVE));}
        // Queued keyboard input also covers automation without depending on desktop foreground permission.
        {require(PostMessageW(d->hwnd,WM_KEYDOWN,VK_F24,0)!=FALSE);require(CompletionInputPending());
        const auto beforeInput=d->Send(SCI_GETENDSTYLED);frame.SendMessage(WM_COMPLETION_CONTINUE);
        require(d->style.completionPending && d->Send(SCI_GETENDSTYLED)==beforeInput);
        {MSG message{};while(PeekMessageW(&message,nullptr,WM_KEYFIRST,WM_KEYLAST,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
        }
        d->style.completionPending=false;
        d->Load({"in"+std::string(550000,' ')});d->Send(SCI_GOTOPOS,2);SetFocus(d->hwnd);d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);typed.ch='n';trace.styles=0;
        HandleStyleNotification(*d,typed);require(d->style.completionPending && trace.styles==0);ProcessStyleBackground(*d,1);require(d->style.completionPending && trace.styles==0);d->style.completionPending=false;
        const std::string longCompletionLine="//"+std::string(550000,' ')+"in";
        d->style.completionOptions[IDC_AUTO_COMPLETE_INSIDE_COMMONT]=1;
        auto longRequest=[&]{
            d->Send(SCI_AUTOCCANCEL);d->Load({longCompletionLine});d->Send(SCI_GOTOPOS,longCompletionLine.size());SetFocus(d->hwnd);
            d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);typed.ch='n';trace.styles=0;
            HandleStyleNotification(*d,typed);require(d->style.completionPending && trace.styles==0);
            const auto tick=std::chrono::steady_clock::now();ProcessStyleBackground(*d,1);
            require(d->style.completionPending && trace.styles==0);
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tick).count();
        };
        longRequest();d->Send(SCI_GOTOPOS,0);ProcessStyleBackground(*d,1);require(!d->style.completionPending);
        longRequest();auto* peer=frame.NewDocument();require(peer!=nullptr);SetFocus(peer->hwnd);ProcessStyleBackground(*d,1);require(!d->style.completionPending);require(frame.Close(frame.tabs.GetActivePage()));SetFocus(d->hwnd);
        longRequest();SendMessageW(d->hwnd,WM_IME_STARTCOMPOSITION,0,0);ProcessStyleBackground(*d,1);require(!d->style.completionPending);SendMessageW(d->hwnd,WM_IME_ENDCOMPOSITION,0,0);
        double longestPosted=0;
        auto drainCompletion=[&]{
            MSG message{};unsigned count=0;
            while(PeekMessageW(&message,frame,WM_COMPLETION_CONTINUE,WM_COMPLETION_CONTINUE,PM_REMOVE)){
                require(++count<1000);const auto start=std::chrono::steady_clock::now();DispatchMessageW(&message);
                longestPosted=(std::max)(longestPosted,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            }
        };
        auto finishPaint=[&]{
            const auto started=std::chrono::steady_clock::now();drainCompletion();InvalidateRect(d->hwnd,nullptr,TRUE);UpdateWindow(d->hwnd);
            frame.SendMessage(WM_DOCUMENT_CHANGED,1);
            const auto deadline=GetTickCount64()+10000;
            while(d->style.completionPending && GetTickCount64()<deadline){
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                if(d->style.completionPending)MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            }
            require(!d->style.completionPending && d->Send(SCI_AUTOCACTIVE) && trace.styles==1);
            d->Send(SCI_AUTOCCANCEL);
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        };
        const auto passiveTimer=longRequest();const auto passivePaint=finishPaint();
        // A bounded short-line slice must not cross into a following long line.
        d->Load({"x\n"+longCompletionLine+"\nin"});d->Send(SCI_GOTOPOS,d->Send(SCI_GETLENGTH));SetFocus(d->hwnd);d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);trace.styles=0;
        HandleStyleNotification(*d,typed);require(d->style.completionPending);ProcessStyleBackground(*d,1);
        require(d->style.completionPending && trace.styles<=1 && d->Send(SCI_GETENDSTYLED)<d->style.completionPosition);
        const auto beforeLong=trace.styles;ProcessStyleBackground(*d,1);require(d->style.completionPending && trace.styles==beforeLong);
        d->style.completionPending=false;
        // First-seen casing survives cached parsing and a new immutable owner.
        d->Send(SCI_AUTOCCANCEL);d->Load({"fo"});
        const auto originalConfiguration=d->style.configuration;
        auto snapshot=std::make_shared<ConfigurationSnapshot>(*d->style.configuration);
        auto language=std::make_shared<LanguageDefinition>(*GetDocumentLexer(*d));
        language->keywords[0]="foobar FooBar for^() function(";language->Seal();
        snapshot->languages[d->style.resolvedScheme]=language;snapshot->catalog[d->style.resolvedScheme]=language.get();d->style.configuration=snapshot;
        d->style.completionIgnoreCase=true;d->Send(SCI_GOTOPOS,2);typed.ch='o';HandleStyleNotification(*d,typed);
        require(d->Send(SCI_AUTOCACTIVE));d->Send(SCI_AUTOCSELECT,0,reinterpret_cast<LPARAM>("foobar"));
        char selected[100]{};d->Send(SCI_AUTOCGETCURRENTTEXT,0,reinterpret_cast<LPARAM>(selected));require(std::string(selected)=="foobar");
        const auto cacheSize=d->style.completionKeywords->words.size();const auto* cached=d->style.completionKeywords->words.begin()->second.data();d->Send(SCI_AUTOCCANCEL);HandleStyleNotification(*d,typed);
        require(d->style.completionKeywords->words.size()==cacheSize && d->style.completionKeywords->words.begin()->second.data()==cached);d->Send(SCI_AUTOCCANCEL);
        auto displayedCandidates=[&]{
            require(d->Send(SCI_AUTOCACTIVE)!=0);
            auto current=[&]{const auto length=d->Send(SCI_AUTOCGETCURRENTTEXT);require(length>=0);std::string value(static_cast<size_t>(length)+1,'\0');d->Send(SCI_AUTOCGETCURRENTTEXT,0,reinterpret_cast<LPARAM>(value.data()));value.resize(static_cast<size_t>(length));return value;};
            const auto selected=current();std::map<LRESULT,std::string> ordered;size_t count=0;
            std::istringstream words(trace.candidates);std::string word;
            while(std::getline(words,word)){
                d->Send(SCI_AUTOCSELECT,0,reinterpret_cast<LPARAM>(word.c_str()));const auto index=d->Send(SCI_AUTOCGETCURRENT);
                require(index>=0 && current()==word && ordered.emplace(index,word).second);++count;
            }
            std::vector<std::string> result;
            for(const auto& [index,value]:ordered){require(index==static_cast<LRESULT>(result.size()));result.push_back(value);}
            require(count==result.size() && count!=0);d->Send(SCI_AUTOCSELECT,0,reinterpret_cast<LPARAM>(selected.c_str()));return result;
        };
        const auto originalTheme=d->style.theme;
        auto theme=std::make_shared<ThemeDefinition>();
        theme->keywords[{"cpp",0}]="append:foozebra foobar FooBar foobaz Foobang";
        d->style.theme=theme;ConfigureDocumentStyle(*d);d->Load({"fo"});d->Send(SCI_GOTOPOS,2);SetFocus(d->hwnd);typed.ch='o';HandleStyleNotification(*d,typed);
        const auto firstThemeCandidates=displayedCandidates();
        require(std::find(firstThemeCandidates.begin(),firstThemeCandidates.end(),"foobar")!=firstThemeCandidates.end());
        require(std::find(firstThemeCandidates.begin(),firstThemeCandidates.end(),"FooBar")==firstThemeCandidates.end());
        const auto themeCache=d->style.completionKeywords;d->Send(SCI_AUTOCCANCEL);
        auto changedTheme=std::make_shared<ThemeDefinition>(*theme);changedTheme->keywords[{"cpp",0}]="append:foozebra FOOBAZ foobee";
        d->style.theme=changedTheme;ConfigureDocumentStyle(*d);HandleStyleNotification(*d,typed);
        const auto changedThemeCandidates=displayedCandidates();
        require(d->style.completionKeywords!=themeCache && std::find(changedThemeCandidates.begin(),changedThemeCandidates.end(),"FOOBAZ")!=changedThemeCandidates.end());
        require(std::find(changedThemeCandidates.begin(),changedThemeCandidates.end(),"foobar")==changedThemeCandidates.end());
        d->Send(SCI_AUTOCCANCEL);d->style.theme=originalTheme;ConfigureDocumentStyle(*d);
        if(GetEnvironmentVariableW(L"MULTIPAD4_COMPLETION_INPUT_BENCHMARK",nullptr,0)){
            d->style.theme=theme;ConfigureDocumentStyle(*d);d->style.completionIgnoreCase=true;
            const std::string inputCorpus=distant.substr(0,distant.size()-3)+"f";
            auto elapsed=[](auto start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
            fprintf(stdout,"CompletionWMCharSettings tabs%zu bytes%zu modeordinary encodingUTF8 ISA_SSE4_2 lexer%s scanDocument%d ignoreCase%d minWord%d onlyDocument%d englishIME%d plugins%d warmProcess1 temporaryIO0\n",static_cast<size_t>(frame.tabs.GetPageCount()),inputCorpus.size(),GetDocumentLexer(*d)->id.c_str(),d->style.completionOptions[IDC_AUTOC_SCAN_DOCUMENT_WORDS],d->style.completionIgnoreCase,d->style.completionOptions[IDC_AUTOC_MIN_WORD_LENGTH],d->style.completionOptions[IDC_AUTOC_ONLY_DOCUMENT_WORDS],d->style.completionOptions[IDC_AUTOC_ENGLISH_IME_ONLY],frame.plugins.enabled);
            for(int sample=0;sample<5;++sample){
                std::array<std::vector<std::string>,7> baselineCandidates;
                for(bool deferred:{false,true}){
                    trace.synchronousInput=!deferred;d->Send(SCI_AUTOCCANCEL);d->Load({inputCorpus});d->Send(SCI_GOTOPOS,inputCorpus.size());SetFocus(d->hwnd);
                    d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);trace.candidates.clear();
                    std::string expected=inputCorpus;
                    for(int step=0;step<7;++step){
                        if(step==4){
                            d->Send(SCI_AUTOCCANCEL);expected=inputCorpus+"o";d->Load({expected});d->Send(SCI_GOTOPOS,expected.size());SetFocus(d->hwnd);d->Send(SCI_COLOURISE,0,expected.size());
                        }
                        if(step>=4)d->Send(SCI_AUTOCCANCEL);
                        const char character=step==0?'o':"oba"[(step-1)%3];expected+=character;
                        trace.paints=trace.copied=trace.styles=0;longestPosted=0;
                        const auto start=std::chrono::steady_clock::now();SendMessageW(d->hwnd,WM_CHAR,character,1);const auto callback=elapsed(start);
                        drainCompletion();InvalidateRect(d->hwnd,nullptr,TRUE);UpdateWindow(d->hwnd);const auto paint=elapsed(start);
                        require(!d->style.completionPending && d->Send(SCI_AUTOCACTIVE) && d->Send(SCI_GETENDSTYLED)>=d->Send(SCI_GETCURRENTPOS));
                        const auto completed=elapsed(start);require(d->Text()==expected);
                        const auto candidates=displayedCandidates();
                        if(!deferred)baselineCandidates[step]=candidates;else require(baselineCandidates[step]==candidates);
                        fprintf(stdout,"CompletionWMChar,%s,%d,%d,%d,%c,%.6f,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu\n",step==0?"first":step<4?"steady-visible":"steady-reopen",sample,deferred,step,character,callback,paint,completed,longestPosted,trace.copied,trace.styles,trace.paints,candidates.size());
                    }
                }
            }
            trace.synchronousInput=false;d->Send(SCI_AUTOCCANCEL);d->style.theme=originalTheme;ConfigureDocumentStyle(*d);
        }
        if(GetEnvironmentVariableW(L"MULTIPAD4_COMPLETION_BENCHMARK",nullptr,0)){
            d->Send(SCI_AUTOCCANCEL);d->style.configuration=originalConfiguration;d->style.completionIgnoreCase=false;
            const auto benchmarkText=distant.substr(0,distant.size()-3)+"in";
            std::vector<double> oldCallbacks,newCallbacks,completionTimes,slices,oldSteady,newSteady;
            auto cpu=[] {FILETIME created{},exited{},kernel{},user{};GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);ULARGE_INTEGER a{},b{};a.LowPart=kernel.dwLowDateTime;a.HighPart=kernel.dwHighDateTime;b.LowPart=user.dwLowDateTime;b.HighPart=user.dwHighDateTime;return(a.QuadPart+b.QuadPart)/10000.0;};
            double oldCpu=0,newCpu=0;size_t oldCopies=0,newCopies=0,oldStyles=0,newStyles=0;
            auto milliseconds=[](auto start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
            for(int sample=0;sample<11;++sample){
                d->Load({benchmarkText});d->Send(SCI_GOTOPOS,benchmarkText.size());SetFocus(d->hwnd);d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);typed.ch='n';
                d->style.completionKeywords.reset();trace.styles=trace.copied=0;
                auto cpuStart=cpu();auto start=std::chrono::steady_clock::now();d->Send(SCI_COLOURISE,0,benchmarkText.size());HandleStyleNotification(*d,typed);oldCallbacks.push_back(milliseconds(start));oldCpu+=cpu()-cpuStart;oldCopies+=trace.copied;oldStyles+=trace.styles;
                require(d->Send(SCI_AUTOCACTIVE));char before[200]{};d->Send(SCI_AUTOCGETCURRENTTEXT,0,reinterpret_cast<LPARAM>(before));d->Send(SCI_AUTOCCANCEL);
                d->Load({benchmarkText});d->Send(SCI_GOTOPOS,benchmarkText.size());SetFocus(d->hwnd);d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);trace.styles=trace.copied=0;
                cpuStart=cpu();start=std::chrono::steady_clock::now();HandleStyleNotification(*d,typed);newCallbacks.push_back(milliseconds(start));require(d->style.completionPending && trace.styles==0);
                for(int pass=0;pass<300 && d->style.completionPending;++pass){auto tick=std::chrono::steady_clock::now();ProcessStyleBackground(*d,1);slices.push_back(milliseconds(tick));}
                completionTimes.push_back(milliseconds(start));newCpu+=cpu()-cpuStart;newCopies+=trace.copied;newStyles+=trace.styles;require(!d->style.completionPending && d->Send(SCI_AUTOCACTIVE));
                char after[200]{};d->Send(SCI_AUTOCGETCURRENTTEXT,0,reinterpret_cast<LPARAM>(after));require(std::string(before)==after);d->Send(SCI_AUTOCCANCEL);
                d->style.completionKeywords.reset();start=std::chrono::steady_clock::now();HandleStyleNotification(*d,typed);oldSteady.push_back(milliseconds(start));d->Send(SCI_AUTOCCANCEL);
                start=std::chrono::steady_clock::now();HandleStyleNotification(*d,typed);newSteady.push_back(milliseconds(start));d->Send(SCI_AUTOCCANCEL);
            }
            for(size_t i=0;i<oldCallbacks.size();++i)fprintf(stdout,"CompletionPair,%zu,%.6f,%.6f,%.6f,%.6f,%.6f\n",i,oldCallbacks[i],newCallbacks[i],completionTimes[i],oldSteady[i],newSteady[i]);
            for(auto* values:{&oldCallbacks,&newCallbacks,&completionTimes,&slices,&oldSteady,&newSteady})std::sort(values->begin(),values->end());
            size_t cacheBytes=0;for(const auto& [list,words]:d->style.completionKeywords->words){cacheBytes+=list.size();for(const auto& word:words)cacheBytes+=word.size();}
            fprintf(stdout,"Completion550KiB oldsync/deferred input median %.3f/%.3fms max %.3f/%.3fms; deferred total median %.3f max %.3fms; longest slice %.3fms.\n",oldCallbacks[5],newCallbacks[5],oldCallbacks.back(),newCallbacks.back(),completionTimes[5],completionTimes.back(),slices.back());
            PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
            fprintf(stdout,"Completion11samples UIcpu old/new %.3f/%.3fms copies%zu/%zu stylecalls%zu/%zu currentprivate%zu peakcommit%zu.\n",oldCpu,newCpu,oldCopies,newCopies,oldStyles,newStyles,memory.PrivateUsage,memory.PeakPagefileUsage);
            fprintf(stdout,"Completion steady reparse/cache median %.3f/%.3fms max %.3f/%.3fms; cached string bytes%zu; byteguard copied0/styles0; first typed through slices+refresh %.3fms.\n",oldSteady[5],newSteady[5],oldSteady.back(),newSteady.back(),cacheBytes,completed);
            const auto waitTimer=longRequest();const auto paint=finishPaint();
            fprintf(stdout,"Completion550KiB singlecommentline timer %.3fms postedstyle+realpaint+completion %.3fms colourise1; defaultcheck %.3f/%.3fms.\n",waitTimer,paint,passiveTimer,passivePaint);
            // Measure the displayed caret region separately from consecutive timer slices.
            for(const auto* corpus:{&benchmarkText,&longCompletionLine})for(int sample=0;sample<11;++sample)for(bool deferred:{false,true}){
                d->Send(SCI_AUTOCCANCEL);d->Load({*corpus});d->Send(SCI_GOTOPOS,corpus->size());SetFocus(d->hwnd);
                d->Send(SCI_CLEARDOCUMENTSTYLE);d->Send(SCI_STARTSTYLING,0);typed.ch='n';
                trace.paints=0;longestPosted=0;const auto cpuStart=cpu();const auto start=std::chrono::steady_clock::now();
                if(!deferred)d->Send(SCI_COLOURISE,0,corpus->size());
                HandleStyleNotification(*d,typed);const auto callback=milliseconds(start);const auto styledBeforePaint=d->Send(SCI_GETENDSTYLED);
                drainCompletion();InvalidateRect(d->hwnd,nullptr,TRUE);UpdateWindow(d->hwnd);const auto firstPaint=milliseconds(start);
                const auto paintCpu=cpu()-cpuStart;const auto paints=trace.paints;
                require(d->Send(SCI_GETENDSTYLED)>=d->Send(SCI_GETCURRENTPOS));
                const auto deadline=GetTickCount64()+10000;
                while(d->style.completionPending && GetTickCount64()<deadline){
                    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                    if(d->style.completionPending)MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
                }
                require(!d->style.completionPending && d->Send(SCI_AUTOCACTIVE));
                fprintf(stdout,"CompletionPaintPair,%s,%d,%d,%.6f,%.6f,%.6f,%.6f,%zu,%lld,%lld,%.6f\n",corpus==&benchmarkText?"short-lines":"long-line",sample,deferred,callback,firstPaint,milliseconds(start),paintCpu,paints,static_cast<long long>(styledBeforePaint),static_cast<long long>(d->Send(SCI_GETENDSTYLED)),longestPosted);
            }
        }
    }
    d->Send(SCI_SETSAVEPOINT);d->encodingDirty=false;require(frame.Close(frame.tabs.GetActivePage()));
}

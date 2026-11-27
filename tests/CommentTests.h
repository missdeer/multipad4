#pragma once
#include "EditTransformTests.h"
#include "CommentMetrics.h"
#include "LexerMetrics.h"
#include <source_location>
namespace commenttest {
struct Copies {
    size_t wholeCalls = 0, textBytes = 0, rangeCalls = 0, styledCalls = 0, styleBytes = 0;
    size_t colouriseCalls = 0, styleQueries = 0, positionCalls = 0;
};
struct Trace { EditorDocument& document; SciFnDirect original; Copies copies; };
inline thread_local Trace* trace = nullptr;
inline sptr_t Direct(sptr_t pointer, unsigned int message, uptr_t w, sptr_t l) {
    auto& state = *trace; auto& count = state.copies;
    if (message == SCI_GETTEXT) { ++count.wholeCalls; count.textBytes += w ? w - 1 : 0; }
    if (message == SCI_GETTEXTRANGEFULL || message == SCI_GETSTYLEDTEXTFULL) {
        const auto& range = *reinterpret_cast<const Sci_TextRangeFull*>(l);
        const auto bytes = static_cast<size_t>(range.chrg.cpMax - range.chrg.cpMin);
        count.textBytes += bytes;
        if (message == SCI_GETSTYLEDTEXTFULL) { ++count.styledCalls; count.styleBytes += bytes; } else ++count.rangeCalls;
    }
    if (message == SCI_COLOURISE) ++count.colouriseCalls;
    if (message == SCI_GETSTYLEINDEXAT) ++count.styleQueries;
    if (message == SCI_POSITIONAFTER) ++count.positionCalls;
    return state.original ? state.original(pointer, message, w, l) : SendMessageW(state.document.hwnd, message, w, l);
}
struct Case {
    const char* name;
    const wchar_t* language;
    std::string input;
    LRESULT anchor, caret;
    LRESULT warm = 0;
    int edit = 0, eol = SC_EOL_LF;
    UINT command = IDM_EDIT_STREAMCOMMENT;
    bool readOnly = false, rectangle = false;
};
struct Result {
    transformtest::State output, undone;
    Copies commandCopies, totalCopies;
    lexerMetrics::Counters commandLexer, totalLexer;
    double callback = 0, paint = 0, cpu = 0;
    SIZE_T beforePrivate = 0, peakPrivate = 0;
    ULONGLONG readBytes = 0, writeBytes = 0;
};
inline Result Run(MainFrame& frame, EditorDocument& d, const Case& test, bool old, bool sample) {
    using Clock = std::chrono::steady_clock;
    d.Send(SCI_SETREADONLY, FALSE); d.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM); d.Send(SCI_CLEARSELECTIONS);
    if (!SelectDocumentScheme(d, test.language)) throw std::runtime_error("Unknown comment fixture language.");
    d.Load({test.input}); d.Send(SCI_SETEOLMODE, test.eol); d.Send(SCI_SETWRAPMODE, SC_WRAP_NONE);
    d.style.completion = false; d.style.markOccurrences = 0; d.style.matchBraces = false; d.style.autoIndent = false;
    d.Send(SCI_SETUNDOSELECTIONHISTORY, SC_UNDO_SELECTION_HISTORY_ENABLED);
    d.Send(SCI_SETSEL, test.anchor, test.caret); d.Send(SCI_SCROLLCARET); frame.Refresh(); frame.UpdateWindow();
    d.Send(SCI_CLEARDOCUMENTSTYLE); d.Send(SCI_STARTSTYLING, 0);
    if (test.warm) d.Send(SCI_COLOURISE, 0, test.warm);
    if (test.edit == 1) { d.Send(SCI_SETTARGETRANGE, 1, 2); d.Send(SCI_REPLACETARGET, 1, reinterpret_cast<LPARAM>("*")); }
    if (test.edit == 2) { d.Send(SCI_SETTARGETRANGE, 1, 7); d.Send(SCI_REPLACETARGET, 6, reinterpret_cast<LPARAM>("div   ")); }
    if (test.edit == 3) {
        const auto lf = test.input.find('\n'); d.Send(SCI_STARTSTYLING, lf); d.Send(SCI_SETSTYLING, 1, 0); d.Send(SCI_STARTSTYLING, d.Send(SCI_GETLENGTH));
    }
    d.Send(SCI_EMPTYUNDOBUFFER); d.Send(SCI_SETSAVEPOINT); d.Send(SCI_SETSEL, test.anchor, test.caret);
    if (test.rectangle) { d.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE); d.Send(SCI_SETRECTANGULARSELECTIONANCHOR, test.anchor); d.Send(SCI_SETRECTANGULARSELECTIONCARET, test.caret); }
    d.Send(SCI_SETREADONLY, test.readOnly);
    const auto before = transformtest::Capture(d);
    Trace observer{d, d.directFunction}; trace = &observer; d.directFunction = Direct;
    struct Restore { EditorDocument& d; SciFnDirect original; ~Restore() { d.directFunction = original; trace = nullptr; baseline = nullptr; lexerMetrics::enabled = false; } } restore{d, observer.original};
    baseline = old ? BaselineToggleDocumentComment : nullptr; lexerMetrics::Reset();
    Result result; result.beforePrivate = transformtest::PrivateBytes(); std::atomic<SIZE_T> peak{result.beforePrivate};
    std::optional<std::jthread> sampler;
    if (sample) sampler.emplace([&](std::stop_token stop) { while (!stop.stop_requested()) {
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) peak.store((std::max)(peak.load(), memory.PrivateUsage)); Sleep(1);
    } });
    IO_COUNTERS ioBefore{}, ioAfter{}; GetProcessIoCounters(GetCurrentProcess(), &ioBefore);
    const auto cpu = transformtest::CpuMilliseconds(); const auto begin = Clock::now();
    frame.SendMessage(WM_COMMAND, test.command);
    result.callback = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    result.commandCopies = observer.copies; result.commandLexer = lexerMetrics::counters;
    frame.SendMessage(WM_DOCUMENT_CHANGED, 0);
    RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
    RedrawWindow(d.ViewWindow(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    UpdateWindow(frame.GetMDITabCtrl()); UpdateWindow(frame.m_hWndStatusBar);
    result.paint = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    result.cpu = transformtest::CpuMilliseconds() - cpu;
    result.totalCopies = observer.copies; result.totalLexer = lexerMetrics::counters;
    lexerMetrics::enabled = false; baseline = nullptr;
    GetProcessIoCounters(GetCurrentProcess(), &ioAfter); result.readBytes = ioAfter.ReadTransferCount - ioBefore.ReadTransferCount; result.writeBytes = ioAfter.WriteTransferCount - ioBefore.WriteTransferCount;
    if (sampler) { sampler->request_stop(); sampler->join(); } result.peakPrivate = peak.load();
    if ((d.Send(SCI_GETREADONLY) != 0) != test.readOnly) throw std::runtime_error("Comment command changed read-only state.");
    result.output = transformtest::Capture(d); d.Send(SCI_SETREADONLY, FALSE);
    if (sample && result.output.bytes == before.bytes) throw std::runtime_error("Comment benchmark did not transform text.");
    if (d.Send(SCI_CANUNDO)) d.Send(SCI_UNDO);
    result.undone = transformtest::Capture(d);
    if (result.undone.bytes != before.bytes) throw std::runtime_error(std::string("Comment undo mismatch: ") + test.name);
    return result;
}
inline void RequirePair(const Case& test, const Result& old, const Result& current) {
    if (old.output != current.output || old.undone != current.undone) throw std::runtime_error(std::string("Comment bytes/selection/undo mismatch: ") + test.name);
    if (current.commandCopies.wholeCalls) throw std::runtime_error("Comment command retains a whole-document text copy.");
}
}
inline void RunCommentTests(MainFrame& frame, bool benchmark) {
    using namespace commenttest;
    auto* d = frame.NewDocument(); if (!d) throw std::runtime_error("Cannot create comment test editor.");
    frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 0); frame.settings.Set(IDM_VIEW_SHOWEXCERPT, 0); frame.ShowWindow(SW_SHOWNORMAL);
    RunEditScopeTests(*d);
    std::vector<Case> cases{
        {"empty",L"cpp","",0,0}, {"wrap",L"cpp","body",0,4}, {"exact",L"cpp","/*body*/",0,8},
        {"surround",L"cpp","/*before body after*/",9,13}, {"reverse",L"cpp","/*body*/",6,2},
        {"string-exact",L"cpp","\"/*body*/\"",1,9}, {"string-inner",L"cpp","\"/*literal*/\"",3,10},
        {"nested-looking",L"cpp","/* outer /* inner x */ tail",18,19},
        {"unicode",L"cpp","/*中😀\ttext*/",2,9}, {"embedded-nul",L"cpp",std::string("/*a\0b*/",7),2,5},
        {"autoit-case",L"autoit3","#CS\nbody\n#CE\n",4,9}, {"matlab-lines",L"matlab","%{\nvalue = 1;\n%}\n",3,14},
        {"matlab-indent",L"matlab","  %{\nvalue\n  %}\n",5,10}, {"matlab-inline",L"matlab","x %{ body %} y",5,9},
        {"cmake",L"cmake","#[[value]]",3,8}, {"lua",L"lua","--[[\nbody\n--]]",5,9},
        {"latex-case",L"latex","\\begin{comment}\nbody\n\\end{comment}",16,20},
        {"powershell",L"powershell","<# body #>",3,7}, {"html-js",L"html","<script>\n/*body*/\n</script>\n",11,15},
        {"html-css",L"html","<style>\n/*body*/\n</style>\n",10,14},
        {"html-vb-line",L"html","<script type=\"text/vbscript\">\nvalue\n</script>",29,34},
        {"php",L"php","<?php /*body*/ ?>",8,12}, {"line-mode",L"cpp","one\ntwo",0,7,0,0,SC_EOL_LF,IDM_EDIT_LINECOMMENT}
    };
    cases.push_back({"dirty-opener",L"cpp","/ x*/",0,1,-1,1});
    cases.push_back({"dirty-embedded",L"html","<script>\n/*body*/\n</script>",11,15,-1,2});
    cases.push_back({"styled-CRLF-chunk",L"cpp","/*"+std::string(65535,'a')+"\r\nb*/",2,65540,-1,3});
    cases.push_back({"read-only",L"cpp","/*body*/",2,6}); cases.back().readOnly = true;
    cases.push_back({"rectangle",L"cpp","one\ntwo",1,6}); cases.back().rectangle = true;
    for (auto test : cases) for (const auto warm : {LRESULT(0), LRESULT(-1)}) {
        if (!test.edit) test.warm = warm;
        RequirePair(test, Run(frame,*d,test,true,false), Run(frame,*d,test,false,false));
    }
    for (const auto& eol : {std::string("\r\n"),std::string("\r")}) {
        Case test{"autoit-eol",L"autoit3","#CS"+eol+"body"+eol+"#CE"+eol,static_cast<LRESULT>(3+eol.size()),static_cast<LRESULT>(7+eol.size())};
        test.eol = eol == "\r\n" ? SC_EOL_CRLF : SC_EOL_CR;
        RequirePair(test,Run(frame,*d,test,true,false),Run(frame,*d,test,false,false));
    }
    printf("Comment boundary/lexer restart/selection/undo pairs passed: %zu cold/warm cases plus CRLF/CR.\n", cases.size()*2); fflush(stdout);
    if (benchmark) {
        std::string tail; while(tail.size()<4*1024*1024) tail += "int padding = 42;\n";
        std::vector<Case> large{
            {"tiny-wrap-cold",L"cpp","x\n"+tail,0,1}, {"tiny-exact-cold",L"cpp","/*x*/\n"+tail,0,5},
            {"tiny-string-warm",L"cpp","\"/*x*/\"\n"+tail,1,6,-1},
            {"embedded-tiny",L"html","<script>\n/*x*/\n</script>\n"+tail,11,12},
            {"long-line",L"cpp","/*"+std::string(512*1024,' ')+"x*/\n"+tail,512*1024+2,512*1024+3},
            {"far-surround",L"cpp","/*\n"+tail+"x\n"+tail+"*/\n",static_cast<LRESULT>(tail.size()+3),static_cast<LRESULT>(tail.size()+4)}
        };
        printf("variant,case,pair,input_bytes,callback_ms,correct_paint_ms,ui_cpu_ms,private_before,sampled_peak_private,text_bytes,style_bytes,range_calls,style_range_calls,colourise_api,style_queries,position_after,lexer_calls,lexer_requested_bytes,style_submitted_bytes,backtrack_bytes,paint_style_submitted_bytes,read_bytes,write_bytes\n");
        for (const auto& test:large) for(int pair=0;pair<3;++pair){
            Result old,current;
            if(pair%2){current=Run(frame,*d,test,false,true);old=Run(frame,*d,test,true,true);}else{old=Run(frame,*d,test,true,true);current=Run(frame,*d,test,false,true);}
            RequirePair(test,old,current);
            for(const bool wasOld:{true,false}){const auto& result=wasOld?old:current;const auto& c=result.commandCopies;const auto& l=result.commandLexer;
                printf("%s,%s,%d,%zu,%.6f,%.6f,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",wasOld?"baseline":"optimized",test.name,pair,test.input.size(),result.callback,result.paint,result.cpu,result.beforePrivate,result.peakPrivate,c.textBytes,c.styleBytes,c.rangeCalls,c.styledCalls,c.colouriseCalls,c.styleQueries,c.positionCalls,l.calls,l.requestedBytes,l.submittedBytes,l.backtrackBytes,result.totalLexer.submittedBytes,result.readBytes,result.writeBytes);
            } fflush(stdout);
        }
    }
    d->Send(SCI_SETREADONLY,FALSE);d->Send(SCI_SETSAVEPOINT);while(frame.tabs.GetPageCount())frame.Close(frame.tabs.GetPageCount()-1,false);
}

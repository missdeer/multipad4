#pragma once
#include "FileCommands.h"
#include "EditTransformTests.h"
#include <source_location>
namespace savewhitespacetest {
inline void Require(bool value, std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error("Save whitespace regression at line " + std::to_string(at.line()));
}
inline void Baseline(MainFrame& frame, EditorDocument& d) {
    if (d.Send(SCI_GETREADONLY)) return;
    const bool normalize = frame.settings.ReadString(L"File Settings", L"ConsistentEOL", L"0") == L"1";
    const bool strip = frame.settings.ReadString(L"File Settings", L"StripTrailingBlanks", L"0") == L"1";
    if (!strip && !normalize) return;
    d.Send(SCI_BEGINUNDOACTION);
    if (strip) for (auto line = d.Send(SCI_GETLINECOUNT); line-- > 0;) {
        auto end = d.Send(SCI_GETLINEENDPOSITION, line), start = d.Send(SCI_POSITIONFROMLINE, line), position = end;
        while (position > start) { auto c = d.Send(SCI_GETCHARAT, position - 1); if (c != ' ' && c != '\t') break; --position; }
        if (position != end) { d.Send(SCI_SETTARGETRANGE, position, end); d.Send(SCI_REPLACETARGET, 0, reinterpret_cast<LPARAM>("")); }
    }
    if (normalize) d.Send(SCI_CONVERTEOLS, d.Send(SCI_GETEOLMODE));
    d.Send(SCI_ENDUNDOACTION);
}
struct Trace { SciFnDirect original; HWND window; size_t calls = 0, byteQueries = 0, ranges = 0, borrowed = 0; };
inline thread_local Trace* trace;
inline sptr_t Direct(sptr_t pointer, unsigned message, uptr_t w, sptr_t l) {
    auto& t = *trace; ++t.calls;
    if (message == SCI_GETCHARAT) ++t.byteQueries;
    if (message == SCI_GETRANGEPOINTER) { ++t.ranges; t.borrowed += static_cast<size_t>(l); }
    return t.original ? t.original(pointer, message, w, l) : SendMessageW(t.window, message, w, l);
}
struct Result {
    transformtest::State output, undone;
    std::vector<LRESULT> bookmarks;
    size_t calls, byteQueries, ranges, borrowed;
    double elapsed, paint, cpu;
    SIZE_T privateBefore, peakPrivate;
};
inline Result Run(MainFrame& frame, EditorDocument& d, const std::string& input, bool old, int mode, bool sample) {
    d.Send(SCI_SETREADONLY, FALSE); d.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM); d.Send(SCI_CLEARSELECTIONS);
    SelectDocumentScheme(d, L"Text"); d.Load({input}); d.style.completion = false; d.style.markOccurrences = 0;
    d.Send(SCI_SETUNDOSELECTIONHISTORY, SC_UNDO_SELECTION_HISTORY_ENABLED);
    d.Send(SCI_SETSEL, 1, input.size());
    if (mode == 3) { d.Send(SCI_SETMULTIPLESELECTION, TRUE); d.Send(SCI_ADDSELECTION, 2, 0); }
    if (mode == 4) { d.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE); d.Send(SCI_SETRECTANGULARSELECTIONANCHOR, 1); d.Send(SCI_SETRECTANGULARSELECTIONCARET, input.size()); }
    d.Send(SCI_SETEOLMODE, SC_EOL_LF); d.Send(SCI_MARKERDELETEALL, 0);
    for (LRESULT line = 0; line < (std::min)(LRESULT(4), d.Send(SCI_GETLINECOUNT)); ++line) d.Send(SCI_MARKERADD, line, 0);
    d.Send(SCI_SETREADONLY, mode == 2);
    frame.settings.WriteString(L"File Settings", L"ConsistentEOL", mode == 1 ? L"1" : L"0");
    frame.settings.WriteString(L"File Settings", L"StripTrailingBlanks", L"1");
    frame.Refresh(); frame.UpdateWindow();
    Trace t{d.directFunction, d.hwnd}; trace = &t; d.directFunction = Direct;
    struct Restore { EditorDocument& d; SciFnDirect original; ~Restore() { d.directFunction = original; trace = nullptr; } } restore{d, t.original};
    Result result{}; result.privateBefore = transformtest::PrivateBytes(); std::atomic<SIZE_T> peak{result.privateBefore};
    std::optional<std::jthread> sampler;
    if (sample) sampler.emplace([&](std::stop_token stop) { while (!stop.stop_requested()) { peak.store((std::max)(peak.load(), transformtest::PrivateBytes())); Sleep(1); } });
    const auto cpu = transformtest::CpuMilliseconds(), start = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (old) Baseline(frame, d); else PrepareFileSave(frame, d);
    auto elapsed = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count() - start; };
    result.elapsed = elapsed(); result.cpu = transformtest::CpuMilliseconds() - cpu;
    result.calls = t.calls; result.byteQueries = t.byteQueries; result.ranges = t.ranges; result.borrowed = t.borrowed;
    RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN); result.paint = elapsed();
    if (sampler) { sampler->request_stop(); sampler->join(); }
    result.peakPrivate = peak.load(); result.output = transformtest::Capture(d);
    for (LRESULT line = 0; line < (std::min)(LRESULT(4), d.Send(SCI_GETLINECOUNT)); ++line) result.bookmarks.push_back(d.Send(SCI_MARKERGET, line));
    d.Send(SCI_SETREADONLY, FALSE);
    if (d.Send(SCI_CANUNDO)) d.Send(SCI_UNDO);
    result.undone = transformtest::Capture(d); Require(result.undone.bytes == input);
    return result;
}
}
inline void RunSaveWhitespaceTests(MainFrame& frame, bool benchmark) {
    using namespace savewhitespacetest;
    auto* d = frame.NewDocument(); Require(d != nullptr);
    frame.ShowWindow(SW_SHOWNORMAL);
    std::vector<std::pair<std::string, std::string>> cases{
        {"empty", ""}, {"mixed", " a \t\r\nb\t\rc  \n中文 \t\nlast\t "}, {"clean", "a\r\nb\nc\rd"},
        {"blank", " \t \t"}, {"nul", std::string("a\0 \t\r\nb", 8)},
        {"split-crlf", std::string(65532, 'x') + "  \r\nlast \t"},
        {"split-tail", "x" + std::string(131080, ' ') + "\r\nlast\t"},
        {"no-final-eol", std::string(65536, 'x') + " \t"}
    };
    if (benchmark) {
        std::string clean, tails;
        for (int i = 0; i < 32768; ++i) { clean += std::string(254, 'x') + "\r\n"; tails += "x" + std::string(253, ' ') + "\r\n"; }
        cases.emplace_back("8MiB-clean", std::move(clean)); cases.emplace_back("8MiB-tails", std::move(tails));
    }
    std::ofstream csv;
    if (benchmark) { csv.open("save-whitespace-results.csv"); csv << "case,mode,iteration,path,bytes,callback_ms,paint_ms,cpu_ms,private_before,peak_private,editor_calls,char_queries,range_calls,borrowed_bytes\n"; }
    for (const auto& [name, input] : cases) for (int mode = 0; mode < (benchmark ? 1 : 5); ++mode) {
        for (int iteration = 0; iteration < (benchmark ? 7 : 1); ++iteration) {
            auto before = Run(frame, *d, input, true, mode, benchmark);
            auto after = Run(frame, *d, input, false, mode, benchmark);
            Require(before.output == after.output && before.undone == after.undone && before.bookmarks == after.bookmarks);
            Require(after.byteQueries == 0);
            if (benchmark) for (const auto& pair : {std::pair{"before", &before}, std::pair{"after", &after}}) {
                const auto& r = *pair.second;
                csv << name << ',' << mode << ',' << iteration << ',' << pair.first << ',' << input.size() << ',' << r.elapsed << ',' << r.paint << ',' << r.cpu << ','
                    << r.privateBefore << ',' << r.peakPrivate << ',' << r.calls << ',' << r.byteQueries << ',' << r.ranges << ',' << r.borrowed << '\n';
            }
        }
    }
    frame.settings.WriteString(L"File Settings", L"ConsistentEOL", L"0"); frame.settings.WriteString(L"File Settings", L"StripTrailingBlanks", L"0");
    d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; Require(frame.Close(frame.tabs.GetActivePage()));
}

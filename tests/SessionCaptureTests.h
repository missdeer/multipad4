#pragma once
#include "SessionCaptureHooks.h"
#include "SessionFeatureTests.h"
#include "EditTransformTests.h"
#include <source_location>
namespace sessioncapturetest {
inline void Require(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("Session capture regression at line " + std::to_string(at.line()));
}
struct Trace { SciFnDirect original; HWND window; size_t calls = 0, lines = 0, levels = 0, iterators = 0; };
inline thread_local Trace* trace;
inline sptr_t Direct(sptr_t pointer, unsigned message, uptr_t w, sptr_t l) {
    auto& t = *trace; ++t.calls;
    if (message == SCI_GETLINECOUNT) ++t.lines;
    if (message == SCI_GETFOLDLEVEL) ++t.levels;
    if (message == SCI_CONTRACTEDFOLDNEXT) ++t.iterators;
    return t.original ? t.original(pointer, message, w, l) : SendMessageW(t.window, message, w, l);
}
inline std::wstring Read(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key) {
    AppSettings profile; profile.Initialize(true); profile.path = path; profile.persistent = true;
    return profile.ReadString(section, key);
}
inline std::map<std::wstring, std::wstring> Fields(const std::filesystem::path& path) {
    std::map<std::wstring, std::wstring> result;
    std::vector<wchar_t> sections(65536), values(65536);
    Require(GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path.c_str()) < sections.size() - 2);
    for (auto* section = sections.data(); *section; section += wcslen(section) + 1) {
        Require(GetPrivateProfileSectionW(section, values.data(), static_cast<DWORD>(values.size()), path.c_str()) < values.size() - 2);
        for (auto* item = values.data(); *item; item += wcslen(item) + 1) {
            const std::wstring entry(item); const auto equal = entry.find(L'='); Require(equal != std::wstring::npos);
            const auto key = entry.substr(0, equal), value = entry.substr(equal + 1);
            if (!value.empty() && key != L"FindHistoryCount" && key != L"ReplaceHistoryCount") result[std::wstring(section) + L"." + key] = value;
        }
    }
    return result;
}
}
inline void RunSessionCaptureTests(MainFrame& frame, bool benchmark) {
    using namespace sessioncapturetest;
    RunSessionFeatureTests(frame); // Existing selections, bookmarks, clone, snapshot and exit coverage.
    const int initial = frame.tabs.GetPageCount();
    const auto folder = std::filesystem::temp_directory_path() / (L"MultiPad4-session-capture-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    auto close = [&](EditorDocument* d) {
        d->Send(SCI_SETREADONLY, FALSE); d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false;
        int page = -1; for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == d) page = i;
        Require(page >= 0 && frame.Close(page));
    };
    try {
        const auto file = folder / L"nested.cpp", session = folder / L"nested.ini";
        files::WriteNew(file, {"void outer() {\n    if (true) {\n        int x = 0;\n    }\n}\n"});
        Require(frame.Open(file)); auto* d = frame.Active(); Require(SelectDocumentScheme(*d, L"cpp")); d->style.folding = true; d->ConfigureLexer(); d->Send(SCI_COLOURISE, 0, -1);
        Require((d->Send(SCI_GETFOLDLEVEL, 0) & SC_FOLDLEVELHEADERFLAG) && (d->Send(SCI_GETFOLDLEVEL, 1) & SC_FOLDLEVELHEADERFLAG));
        d->Send(SCI_FOLDLINE, 1, SC_FOLDACTION_CONTRACT); d->Send(SCI_FOLDLINE, 0, SC_FOLDACTION_CONTRACT);
        d->Send(SCI_MARKERADD, 2, 0); d->Send(SCI_SETMULTIPLESELECTION, TRUE); d->Send(SCI_SETSELECTION, 12, 4); d->Send(SCI_ADDSELECTION, 40, 36);
        const auto selection = transformtest::Capture(*d);
        d->search.findHistory = {L"first", L"second", L"third"}; d->search.replaceHistory = {L"replacement", L"old"};
        baseline = true; SaveRelaunchSession(frame, session, false, d); baseline = false;
        Require(Read(session, L"Document0", L"CollapsedFolds") == L"0 1 ");
        close(d); RestoreRelaunchSession(frame, session); d = frame.Active();
        Require(!d->Send(SCI_GETFOLDEXPANDED, 0) && !d->Send(SCI_GETFOLDEXPANDED, 1));
        Require((d->Send(SCI_MARKERGET, 2) & 1) && transformtest::Capture(*d) == selection);
        Require(d->search.findHistory == std::vector<std::wstring>({L"first", L"second", L"third"}));
        d->search.findHistory = {L"short 中文"}; d->search.replaceHistory.clear();
        SaveRelaunchSession(frame, session, false, d);
        Require(Read(session, L"Document0", L"FindHistoryCount") == L"1" && Read(session, L"Document0", L"ReplaceHistoryCount") == L"0");
        Require(Read(session, L"Document0", L"FindHistory1").empty() && Read(session, L"Document0", L"FindHistory2").empty());
        Require(Read(session, L"Document0", L"ReplaceHistory0").empty());
        close(d); RestoreRelaunchSession(frame, session); d = frame.Active();
        Require(d->search.findHistory == std::vector<std::wstring>({L"short 中文"}) && d->search.replaceHistory.empty());
        Require(!d->Send(SCI_GETFOLDEXPANDED, 0) && !d->Send(SCI_GETFOLDEXPANDED, 1)); close(d);

        if (benchmark) {
            d = frame.NewDocument(); Require(d != nullptr); Require(SelectDocumentScheme(*d, L"Text"));
            std::string corpus; corpus.reserve(4000000); for (int i = 0; i < 2000000; ++i) corpus += "x\n";
            d->Load({corpus}); d->style.completion = false; d->style.markOccurrences = 0;
            d->search.findHistory = {L"find", L"中文"}; d->search.replaceHistory = {L"replace"};
            std::ofstream csv("session-capture-results.csv");
            csv << "mode,iteration,path,lines,elapsed_ms,cpu_ms,private_before,peak_private,editor_calls,line_queries,fold_level_queries,fold_iterator_queries,history_writes,serialized_bytes\n";
            for (bool all : {false, true}) for (int iteration = 0; iteration < 7; ++iteration) {
                std::map<std::wstring, std::wstring> expected;
                for (bool old : {true, false}) {
                    const auto output = folder / (std::to_wstring(all) + L"-" + std::to_wstring(iteration) + (old ? L"-old.ini" : L"-new.ini"));
                    Trace t{d->directFunction, d->hwnd}; trace = &t; d->directFunction = Direct;
                    struct Restore { EditorDocument& d; SciFnDirect original; ~Restore() { d.directFunction = original; trace = nullptr; baseline = false; } } restore{*d, t.original};
                    baseline = old; historyWrites = 0;
                    const auto privateBefore = transformtest::PrivateBytes(); std::atomic<SIZE_T> peak{privateBefore};
                    std::jthread sampler([&](std::stop_token stop) { while (!stop.stop_requested()) { peak.store((std::max)(peak.load(), transformtest::PrivateBytes())); Sleep(1); } });
                    const auto cpu = transformtest::CpuMilliseconds(); const auto started = std::chrono::steady_clock::now();
                    SaveRelaunchSession(frame, output, all, d);
                    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                    const auto cpuMs = transformtest::CpuMilliseconds() - cpu; sampler.request_stop(); sampler.join();
                    const auto fields = Fields(output); if (old) expected = fields; else Require(fields == expected);
                    csv << (all ? "maintenance-restart" : "clone-relaunch") << ',' << iteration << ',' << (old ? "before" : "after") << ',' << 2000001 << ',' << elapsed << ',' << cpuMs << ',' << privateBefore << ',' << peak.load() << ',' << t.calls << ',' << t.lines << ',' << t.levels << ',' << t.iterators << ',' << historyWrites << ',' << std::filesystem::file_size(output) << '\n'; csv.flush();
                }
            }
            close(d);
        }
        Require(frame.tabs.GetPageCount() == initial); std::filesystem::remove_all(folder);
    } catch (...) {
        baseline = false;
        while (frame.tabs.GetPageCount() > initial) close(static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetPageCount() - 1)));
        throw;
    }
}

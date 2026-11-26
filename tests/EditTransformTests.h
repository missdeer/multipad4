#pragma once
#include "MainFrame.h"
#include "EditDialogTests.h"
#include "EditScopeTests.h"
#include "EditTransformMetrics.h"
#include <chrono>
#include <cstdio>
#include <psapi.h>
#include <thread>
bool BaselineExecuteAdvancedEdit(EditorDocument&, UINT);
namespace transformtest {
struct State {
    std::string bytes;
    std::vector<LRESULT> selection;
    bool operator==(const State&) const = default;
};
inline State Capture(EditorDocument& document) {
    State state{document.Text(), {document.Send(SCI_GETSELECTIONMODE), document.Send(SCI_GETMAINSELECTION)}};
    if (document.Send(SCI_SELECTIONISRECTANGLE)) for (UINT message : {SCI_GETRECTANGULARSELECTIONCARET, SCI_GETRECTANGULARSELECTIONANCHOR,
        SCI_GETRECTANGULARSELECTIONCARETVIRTUALSPACE, SCI_GETRECTANGULARSELECTIONANCHORVIRTUALSPACE}) state.selection.push_back(document.Send(message));
    for (LRESULT i = 0; i < document.Send(SCI_GETSELECTIONS); ++i) {
        state.selection.push_back(document.Send(SCI_GETSELECTIONNCARET, i));
        state.selection.push_back(document.Send(SCI_GETSELECTIONNANCHOR, i));
        state.selection.push_back(document.Send(SCI_GETSELECTIONNCARETVIRTUALSPACE, i));
        state.selection.push_back(document.Send(SCI_GETSELECTIONNANCHORVIRTUALSPACE, i));
    }
    return state;
}
struct Case {
    const char* name;
    UINT command;
    std::string input;
    LRESULT anchor = 0, caret = -1;
    int eol = SC_EOL_LF, align = IDC_ALIGN_RIGHT, column = 7;
    bool rectangle = false, cancel = false;
};
struct Result {
    State output, undone;
    Metrics counters;
    double callbackMs = 0, paintMs = 0, cpuMs = 0;
    SIZE_T privateBefore = 0, privatePeak = 0;
};
inline double CpuMilliseconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
    auto ticks = [](FILETIME value) { return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    return (ticks(kernel) + ticks(user)) / 10000.0;
}
inline SIZE_T PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        throw std::runtime_error("Cannot sample transform private bytes.");
    return memory.PrivateUsage;
}
inline Result Run(MainFrame& frame, EditorDocument& document, const Case& test, bool old, bool sample) {
    using Clock = std::chrono::steady_clock;
    document.Send(SCI_SETREADONLY, FALSE); document.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
    document.Load({test.input}); document.Send(SCI_SETEOLMODE, test.eol); document.Send(SCI_SETTABWIDTH, 4);
    document.Send(SCI_SETWRAPMODE, SC_WRAP_NONE); document.Send(SCI_SETUNDOSELECTIONHISTORY, SC_UNDO_SELECTION_HISTORY_ENABLED);
    const auto caret = test.caret < 0 ? document.Send(SCI_GETLENGTH) : test.caret;
    if (test.rectangle) {
        document.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
        document.Send(SCI_SETRECTANGULARSELECTIONANCHOR, test.anchor); document.Send(SCI_SETRECTANGULARSELECTIONCARET, caret);
    } else document.Send(SCI_SETSEL, test.anchor, caret);
    document.style.completion = false; document.style.markOccurrences = 0;
    frame.Refresh(); frame.UpdateWindow();
    const bool dialog = test.command == IDM_EDIT_ALIGN || test.command == IDM_EDIT_SORTLINES || test.command == IDM_EDIT_COLUMNWRAP;
    UINT_PTR timer = 0;
    if (dialog) {
        EditDialogTest::owner = frame; EditDialogTest::opened = false; EditDialogTest::ticks = 0;
        EditDialogTest::finish = test.cancel ? IDCANCEL : IDOK;
        EditDialogTest::fill = [&](HWND window) {
            if (test.command == IDM_EDIT_ALIGN) CheckRadioButton(window, IDC_ALIGN_LEFT, IDC_ALIGN_JUSTIFY_PAR, test.align);
            if (test.command == IDM_EDIT_COLUMNWRAP) SetDlgItemInt(window, IDC_COLUMNWRAP, test.column, FALSE);
            if (test.command == IDM_EDIT_SORTLINES) {
                CheckRadioButton(window, IDC_SORT_NONE, IDC_SORT_SHUFFLE, IDC_SORT_ASC);
                for (UINT id : {IDC_SORT_MERGE_DUP, IDC_SORT_REMOVE_DUP, IDC_SORT_REMOVE_UNIQUE, IDC_SORT_IGNORE_CASE, IDC_SORT_LOGICAL_NUMBER, IDC_SORT_GROUPBY_FILE_TYPE})
                    CheckDlgButton(window, id, BST_UNCHECKED);
                CheckDlgButton(window, IDC_SORT_COLUMN, test.rectangle ? BST_CHECKED : BST_UNCHECKED);
            }
        };
        timer = SetTimer(nullptr, 0, 10, EditDialogTest::Timer);
        if (!timer) throw std::runtime_error("Cannot create transform dialog timer.");
    }
    struct Restore {
        UINT_PTR timer;
        ~Restore() { if (timer) KillTimer(nullptr, timer); EditDialogTest::owner = nullptr; EditDialogTest::fill = {}; baseline = nullptr; }
    } restore{timer};
    baseline = old ? BaselineExecuteAdvancedEdit : nullptr; metrics = {};
    Result result; result.privateBefore = PrivateBytes(); std::atomic<SIZE_T> peak{result.privateBefore};
    std::optional<std::jthread> sampler;
    if (sample) sampler.emplace([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
                peak.store((std::max)(peak.load(), memory.PrivateUsage));
            Sleep(1);
        }
    });
    const auto cpu = CpuMilliseconds(); const auto started = Clock::now();
    frame.SendMessage(WM_COMMAND, test.command);
    result.callbackMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    frame.SendMessage(WM_DOCUMENT_CHANGED, 0);
    RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    result.paintMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count(); result.cpuMs = CpuMilliseconds() - cpu;
    result.counters = metrics; baseline = nullptr;
    if (sampler) { sampler->request_stop(); sampler->join(); }
    result.privatePeak = peak.load();
    if (dialog && !EditDialogTest::opened) throw std::runtime_error("Transform dialog did not open.");
    result.output = Capture(document);
    if (document.Send(SCI_CANUNDO)) document.Send(SCI_UNDO);
    result.undone = Capture(document);
    if (result.undone.bytes != test.input) throw std::runtime_error("Transform undo changed input bytes.");
    return result;
}
inline void RequirePair(const Case& test, const Result& before, const Result& after) {
    if (before.output != after.output || before.undone != after.undone)
        throw std::runtime_error(std::string("Transform paired bytes/selection/undo mismatch: ") + test.name);
    if (after.counters.widthCalls && test.command != IDM_EDIT_ALIGN)
        throw std::runtime_error("Unused display-width prepass remains.");
    if (after.counters.eagerBytes || after.counters.lineCopyBytes)
        throw std::runtime_error("Transform retains an eager payload copy.");
}
}
inline void RunEditTransformTests(MainFrame& frame, bool benchmark) {
    using namespace transformtest;
    auto* document = frame.NewDocument(); if (!document) throw std::runtime_error("Cannot create transform test document.");
    frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 0); frame.plugins.enabled = false;
    frame.ShowWindow(SW_SHOWNORMAL); frame.UpdateWindow();
    RunEditScopeTests(*document); RunEditDialogTests(*document);
    std::vector<Case> cases;
    for (const auto& text : {std::string("b\na\nb"), std::string("b\r\na\r\nb\r\n"), std::string("b\ra\rb\r"), std::string("x\nx\n")})
        for (UINT command : {IDM_EDIT_SORTLINES, IDM_EDIT_MERGEDUPLICATELINE, IDM_EDIT_REMOVEDUPLICATELINE}) cases.push_back({"line-endings", command, text});
    cases.push_back({"partial-wrap", IDM_EDIT_COLUMNWRAP, "one two three more", 0, 13});
    cases.push_back({"surrogate-wrap", IDM_EDIT_COLUMNWRAP, "😀x 中文\tend", 0, -1, SC_EOL_CRLF, IDC_ALIGN_RIGHT, 1});
    cases.push_back({"rectangle-sort", IDM_EDIT_SORTLINES, "x b\ny a", 2, 7, SC_EOL_LF, IDC_ALIGN_RIGHT, 7, true});
    cases.push_back({"backwards-sort", IDM_EDIT_SORTLINES, "prefix\nb\na\ntail", 11, 7});
    cases.push_back({"line-start-end", IDM_EDIT_SORTLINES, "z\nb\na\ntail", 2, 6});
    cases.push_back({"single-align", IDM_EDIT_ALIGN, "long header\n  x", 14, 14});
    for (int align : {IDC_ALIGN_LEFT, IDC_ALIGN_RIGHT, IDC_ALIGN_CENTER, IDC_ALIGN_JUSTIFY, IDC_ALIGN_JUSTIFY_PAR})
        cases.push_back({"alignment", IDM_EDIT_ALIGN, "\talpha 😀 words\r\n   shorter x\r\n \t\r\n  final words", 0, -1, SC_EOL_CRLF, align});
    for (UINT command : {IDM_EDIT_ALIGN, IDM_EDIT_SORTLINES, IDM_EDIT_COLUMNWRAP})
        cases.push_back({"cancel", command, "b\na\n", 0, -1, SC_EOL_LF, IDC_ALIGN_RIGHT, 7, false, true});
    for (const auto& test : cases) RequirePair(test, Run(frame, *document, test, true, false), Run(frame, *document, test, false, false));
    printf("Edit transform exact bytes, EOL, selection and undo: %zu paired small cases passed.\n", cases.size()); fflush(stdout);
    if (benchmark) {
        std::string corpus; corpus.reserve(20 * 1024 * 1024);
        for (int i = 0; i < 1000000; ++i) corpus += " " + std::to_string((1000000 - i) % 10000) + "\t😀x\n";
        printf("variant,operation,pair,lines,input_bytes,callback_ms,correct_redraw_ms,ui_cpu_ms,private_before,sampled_peak_private,range_calls,range_bytes,split_bytes,eager_bytes,line_copy_bytes,utf16_calls,utf16_bytes,width_calls\n");
        for (const auto& operation : std::initializer_list<std::pair<const char*, UINT>>{{"align", IDM_EDIT_ALIGN}, {"sort", IDM_EDIT_SORTLINES}, {"dedup", IDM_EDIT_MERGEDUPLICATELINE}, {"wrap", IDM_EDIT_COLUMNWRAP}}) {
            Case test{operation.first, operation.second, corpus};
            for (int pair = 0; pair < 3; ++pair) {
                Result before, after;
                for (bool old : {pair % 2 == 0, pair % 2 != 0}) {
                    auto result = Run(frame, *document, test, old, true);
                    const auto& m = result.counters;
                    printf("%s,%s,%d,1000000,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n", old ? "before" : "after", test.name, pair, corpus.size(), result.callbackMs, result.paintMs, result.cpuMs,
                        static_cast<size_t>(result.privateBefore), static_cast<size_t>(result.privatePeak), m.rangeCalls, m.rangeBytes, m.splitBytes, m.eagerBytes, m.lineCopyBytes, m.utf16Calls, m.utf16Bytes, m.widthCalls); fflush(stdout);
                    (old ? before : after) = std::move(result);
                }
                RequirePair(test, before, after);
            }
        }
    }
    document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
    frame.Close(frame.tabs.GetActivePage(), false);
}

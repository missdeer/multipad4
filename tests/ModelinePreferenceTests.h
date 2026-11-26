#pragma once
#include "MainFrame.h"
#include "FileCommands.h"
#include "FrameCommands.h"
#include "SettingsCancelTests.h"
#include "TabPreferenceTests.h"
#include "LongLinePreferenceTests.h"
#include "ModelinePreferenceHooks.h"
#include <chrono>
#include <psapi.h>
#include <thread>

void BaselineApplyFileVariables(EditorDocument&);
void BaselineRememberDocumentPreferences(MainFrame&, EditorDocument&, UINT);
namespace modelinepref {
inline void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Modeline preference regression at " + std::to_string(where.line()));
}
struct Reads {
    size_t textCalls = 0, textBytes = 0, rangeCalls = 0, rangeBytes = 0;
};
struct Probe {
    HWND window; bool enabled = false; Reads reads;
    explicit Probe(EditorDocument& document) : window(document.hwnd) {
        Check(!document.directFunction); // The pinned fork uses the Win32 message path.
        Check(SetWindowSubclass(window, Procedure, 52, reinterpret_cast<DWORD_PTR>(this)));
    }
    ~Probe() { if (IsWindow(window)) RemoveWindowSubclass(window, Procedure, 52); }
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto& probe = *reinterpret_cast<Probe*>(data); const bool record = probe.enabled;
        const auto result = DefSubclassProc(window, message, w, l);
        if (record && message == SCI_GETTEXT && l) { ++probe.reads.textCalls; probe.reads.textBytes += static_cast<size_t>(result); }
        if (record && message == SCI_GETTEXTRANGEFULL && l) { ++probe.reads.rangeCalls; probe.reads.rangeBytes += static_cast<size_t>(result); }
        return result;
    }
};
using Preferences = decltype(settingscanceltest::Preferences(std::declval<DocumentStyleState>()));
struct State {
    std::string text;
    Preferences style, baseline, applied;
    std::array<LRESULT, 13> editor;
    std::vector<LRESULT> selection;
    std::array<LRESULT, 10> extra;
    std::wstring language;
    bool operator==(const State&) const = default;
};
inline State Capture(EditorDocument& d) {
    auto& f = d.fileVariables;
    State s{d.Text(), settingscanceltest::Preferences(d.style), settingscanceltest::Preferences(f.baseline),
        settingscanceltest::Preferences(f.applied), settingscanceltest::EditorSettings(d), {},
        {f.active, f.baselineWrap, f.appliedWrap, f.baselineEdge, f.appliedEdge,
         d.Send(SCI_GETEDGECOLUMN), d.Send(SCI_GETEDGEMODE), d.edgeMode, d.Send(SCI_GETREADONLY), d.Send(SCI_GETMODIFY)}, d.language};
    s.selection = {d.Send(SCI_GETSELECTIONMODE), d.Send(SCI_GETMAINSELECTION)};
    if (d.Send(SCI_SELECTIONISRECTANGLE)) for (UINT message : {SCI_GETRECTANGULARSELECTIONCARET, SCI_GETRECTANGULARSELECTIONANCHOR,
        SCI_GETRECTANGULARSELECTIONCARETVIRTUALSPACE, SCI_GETRECTANGULARSELECTIONANCHORVIRTUALSPACE}) s.selection.push_back(d.Send(message));
    for (LRESULT i = 0; i < d.Send(SCI_GETSELECTIONS); ++i) for (UINT message : {SCI_GETSELECTIONNCARET, SCI_GETSELECTIONNANCHOR,
        SCI_GETSELECTIONNCARETVIRTUALSPACE, SCI_GETSELECTIONNANCHORVIRTUALSPACE}) s.selection.push_back(d.Send(message, i));
    return s;
}
inline void Activate(MainFrame& frame, EditorDocument& document) {
    for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == &document) {
        frame.tabs.SetActivePage(i); frame.Refresh(); return;
    }
    throw std::runtime_error("Preference document disappeared.");
}
inline void Boundaries(MainFrame& frame) {
    auto* d = frame.NewDocument(); Check(d); const auto originalStyle = d->style;
    std::vector<std::string> inputs{"", "tab-width: 7", "enable-local-variables: nil; tab-width: 9", std::string(512, ' ')};
    for (size_t size : {513, 700, 1023, 1024, 1025, 4097, 8193}) {
        std::string text(size, ' '); const size_t tail = size - 512;
        const std::string marker = "fill-column: 91"; text[tail - 1] = 'x'; text.replace(tail, marker.size(), marker); inputs.push_back(std::move(text));
    }
    inputs.push_back("tab-width: 5; mode: text\n" + std::string(1200, ' ') + "fill-column: 94; tab-width: 9");
    inputs.push_back(std::string(510, ' ') + "😀\n" + std::string(700, ' ') + "fill-column: 93");
    Probe probe(*d);
    for (const auto& input : inputs) {
        d->Send(SCI_SETREADONLY, FALSE); d->fileVariables = {}; d->style = originalStyle; d->Load({input});
        probe.reads = {}; probe.enabled = true; const auto region = filevars::Region(*d); probe.enabled = false;
        Check(region == filevars::Region(input)); Check(probe.reads.textCalls == 0);
        const bool tail = input.size() > 512 && !filevars::Recognized(input.substr(0, 512));
        Check(probe.reads.rangeBytes == (input.empty() ? 0 : (std::min)(input.size(), size_t(512))) + (tail ? 512 : 0));
        Check(probe.reads.rangeCalls == (input.empty() ? 0u : 1u) + (tail ? 1u : 0u));
        std::optional<State> expected;
        for (bool old : {true, false}) {
            d->Send(SCI_SETREADONLY, FALSE); d->fileVariables = {}; d->style = originalStyle; d->Load({input});
            d->Send(SCI_SETTABWIDTH, originalStyle.tabWidth); d->Send(SCI_SETINDENT, originalStyle.indentWidth);
            d->Send(SCI_SETUSETABS, !originalStyle.tabsAsSpaces); d->Send(SCI_SETTABINDENTS, originalStyle.tabIndents);
            d->Send(SCI_SETEDGECOLUMN, originalStyle.longLineColumn); d->Send(SCI_SETWRAPMODE, originalStyle.wrapMode);
            d->Send(SCI_SETSEL, 1, (std::min)(size_t(4), input.size()));
            d->Send(SCI_SETREADONLY, TRUE); modelinetest::applyBaseline = old ? BaselineApplyFileVariables : nullptr;
            ApplyFileVariables(*d); modelinetest::applyBaseline = nullptr;
            auto state = Capture(*d); if (old) expected = state; else if (state != *expected) {
                fprintf(stderr, "Apply mismatch length=%zu text=%d style=%d baseline=%d applied=%d editor=%d selection=%d extra=%d language=%d\n", input.size(),
                    state.text == expected->text, state.style == expected->style, state.baseline == expected->baseline, state.applied == expected->applied,
                    state.editor == expected->editor, state.selection == expected->selection, state.extra == expected->extra, state.language == expected->language); fflush(stderr); Check(false);
            }
        }
    }
    // No region is cached across an edit or a modeline restoration.
    d->Send(SCI_SETREADONLY, FALSE); d->fileVariables = {}; d->style = originalStyle;
    d->Load({"tab-width: 6\n"}); ApplyFileVariables(*d); Check(d->Send(SCI_GETTABWIDTH) == 6);
    d->Send(SCI_SETTARGETRANGE, 0, d->Send(SCI_GETLENGTH)); d->Send(SCI_REPLACETARGET, 13, reinterpret_cast<LPARAM>("tab-width: 9\n"));
    ApplyFileVariables(*d); Check(d->Send(SCI_GETTABWIDTH) == 9);
    d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; Check(frame.Close(frame.tabs.GetActivePage(), false));
}
inline uint64_t Cpu() {
    FILETIME a{}, b{}, k{}, u{}; Check(GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u));
    return (uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime);
}
inline SIZE_T Private() {
    PROCESS_MEMORY_COUNTERS_EX m{}; m.cb = sizeof(m);
    Check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m))); return m.PrivateUsage;
}
struct Result {
    std::vector<State> documents;
    Preferences defaults;
    std::vector<std::wstring> profile;
    Reads reads, peerReads;
    double callback = 0, painted = 0, cpu = 0;
    SIZE_T peak = 0;
};
inline Result Command(MainFrame& frame, const std::vector<EditorDocument*>& documents, UINT command, bool old) {
    Activate(frame, *documents.front());
    UINT_PTR timer = 0;
    if (command == IDM_VIEW_TABSETTINGS) {
        using namespace StyleDialogTest;
        owner = frame; activated = false; ticks = 0; callbackError = nullptr; finishCommand = IDOK;
        fill = [](HWND dialog) { CheckDlgButton(dialog, IDC_TAB_INDENT, BST_CHECKED); };
        timer = SetTimer(nullptr, 0, 10, Timer); Check(timer);
    }
    if (command == IDM_VIEW_LONGLINESETTINGS) {
        using namespace longlinepreferencetest;
        owner = frame; activated = false; failure = nullptr; finish = IDOK;
        edit = [](HWND dialog) { SetDlgItemInt(dialog, IDC_LONGLINE_LIMIT, 120, FALSE); };
        timer = SetTimer(nullptr, 0, 10, Timer); Check(timer);
    }
    std::vector<std::unique_ptr<Probe>> probes;
    for (auto* d : documents) { probes.push_back(std::make_unique<Probe>(*d)); probes.back()->enabled = true; }
    struct Reset {
        UINT_PTR timer;
        ~Reset() { if (timer) KillTimer(nullptr, timer); modelinetest::preferencesBaseline = nullptr;
            StyleDialogTest::owner = nullptr; StyleDialogTest::fill = {}; longlinepreferencetest::owner = nullptr; longlinepreferencetest::edit = {}; }
    } reset{timer};
    modelinetest::preferencesBaseline = old ? BaselineRememberDocumentPreferences : nullptr;
    std::atomic<SIZE_T> peak{Private()};
    std::jthread sampler([&](std::stop_token stop) { while (!stop.stop_requested()) { peak.store((std::max)(peak.load(), Private())); Sleep(1); } });
    const auto cpu = Cpu(); const auto start = std::chrono::steady_clock::now();
    frame.SendMessage(WM_COMMAND, command); const auto returned = std::chrono::steady_clock::now();
    frame.SendMessage(WM_DOCUMENT_CHANGED, 0); frame.UpdateWindow();
    if (auto* d = frame.Active()) RedrawWindow(d->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    const auto painted = std::chrono::steady_clock::now(); const auto cpuEnd = Cpu();
    sampler.request_stop(); sampler.join(); modelinetest::preferencesBaseline = nullptr;
    Result result; result.callback = std::chrono::duration<double, std::milli>(returned - start).count();
    result.painted = std::chrono::duration<double, std::milli>(painted - start).count(); result.cpu = (cpuEnd - cpu) / 10000.0; result.peak = peak.load();
    for (size_t i = 0; i < probes.size(); ++i) {
        auto& probe = probes[i]; probe->enabled = false;
        result.reads.textCalls += probe->reads.textCalls; result.reads.textBytes += probe->reads.textBytes;
        result.reads.rangeCalls += probe->reads.rangeCalls; result.reads.rangeBytes += probe->reads.rangeBytes;
        if (i) { result.peerReads.textCalls += probe->reads.textCalls; result.peerReads.textBytes += probe->reads.textBytes;
            result.peerReads.rangeCalls += probe->reads.rangeCalls; result.peerReads.rangeBytes += probe->reads.rangeBytes; }
    }
    if (command == IDM_VIEW_TABSETTINGS) { Check(StyleDialogTest::activated); if (StyleDialogTest::callbackError) std::rethrow_exception(StyleDialogTest::callbackError); }
    if (command == IDM_VIEW_LONGLINESETTINGS) { Check(longlinepreferencetest::activated); if (longlinepreferencetest::failure) std::rethrow_exception(longlinepreferencetest::failure); }
    for (auto* d : documents) result.documents.push_back(Capture(*d));
    result.defaults = settingscanceltest::Preferences(frame.defaultStyle);
    for (const auto* key : {L"EdgeColumn", L"EdgeMode", L"EdgeVisible", L"TabIndents", L"BackspaceMask", L"DetectIndentation"})
        result.profile.push_back(frame.settings.ReadString(L"Editor Defaults", key, L"<unset>"));
    return result;
}
}

inline void RunModelinePreferenceTests(MainFrame& frame, bool benchmark) {
    using namespace modelinepref;
    frame.settings.persistent = false; frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 0);
    frame.settings.Set(IDM_VIEW_SHOWEXCERPT, 0); frame.plugins.enabled = false;
    RunTabPreferenceTests(frame); RunLongLinePreferenceTests(frame); RunSettingsCancelTests(frame); Boundaries(frame);
    const auto directory = std::filesystem::temp_directory_path() / (L"multipad-modeline-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory); const auto profile = directory / L"style.ini"; SaveStylePreferences(profile);
    frame.defaultStyle.longLineColumn = 80; frame.defaultStyle.longLineMode = EDGE_LINE; frame.defaultStyle.longLineEnabled = false;
    const auto defaults = frame.defaultStyle; const auto settings = frame.settings;
    std::vector<EditorDocument*> documents; std::vector<DocumentStyleState> styles;
    for (int i = 0; i < 4; ++i) { auto* d = frame.NewDocument(); Check(d); documents.push_back(d); styles.push_back(d->style); }
    frame.ShowWindow(SW_SHOWNORMAL);
    auto inputs = [&](size_t bytes) {
        const std::string block = std::string(255, 'x') + '\n'; std::string body; body.reserve(bytes);
        while (body.size() < bytes) body += block;
        return std::vector<std::string>{"fill-column: 97; tab-always-indent: nil\nactive\n",
            "fill-column: 91; tab-always-indent: nil\n" + body,
            body + "\nfill-column: 92; tab-always-indent: nil\n",
            "mode: text\n" + body + "\nfill-column: 93; tab-always-indent: nil\n"};
    };
    auto prepare = [&](const std::vector<std::string>& corpus) {
        LoadStylePreferences(profile); frame.defaultStyle = defaults; frame.settings = settings;
        for (size_t i = 0; i < documents.size(); ++i) {
            auto& d = *documents[i]; d.Send(SCI_SETREADONLY, FALSE); d.fileVariables = {}; d.style = styles[i]; d.Load({corpus[i]});
            d.Send(SCI_SETTABWIDTH, styles[i].tabWidth); d.Send(SCI_SETINDENT, styles[i].indentWidth);
            d.Send(SCI_SETUSETABS, !styles[i].tabsAsSpaces); d.Send(SCI_SETTABINDENTS, styles[i].tabIndents);
            d.Send(SCI_SETBACKSPACEUNINDENTS, styles[i].backspaceMask & 3); d.Send(SCI_SETWRAPMODE, styles[i].wrapMode);
            d.Send(SCI_SETEDGECOLUMN, 80); d.Send(SCI_SETEDGEMODE, EDGE_NONE); d.edgeMode = EDGE_LINE;
            d.style.completion = false; d.style.markOccurrences = 0; ApplyFileVariables(d);
            d.Send(SCI_SETSEL, (std::min)(size_t(3), corpus[i].size()), 1);
            if (i == 1) d.Send(SCI_SETREADONLY, TRUE);
        }
    };
    auto check = [&](UINT command, const Result& before, const Result& after, size_t bytes) {
        if (before.documents != after.documents || before.defaults != after.defaults || before.profile != after.profile) {
            fprintf(stderr, "Preference mismatch command=%u documents=%d defaults=%d profile=%d\n", command, before.documents == after.documents,
                before.defaults == after.defaults, before.profile == after.profile);
            for (size_t i = 0; i < before.documents.size(); ++i) {
                const auto& a = before.documents[i]; const auto& b = after.documents[i];
                if (a != b) { fprintf(stderr, "doc%zu text%d style%d baseline%d applied%d editor%d selection%d extra%d language%d\n", i,
                    a.text == b.text, a.style == b.style, a.baseline == b.baseline, a.applied == b.applied, a.editor == b.editor,
                    a.selection == b.selection, a.extra == b.extra, a.language == b.language); }
            }
            fflush(stderr); Check(false);
        }
        Check(after.reads.textBytes < 4096);
        if (bytes >= 8 * 1024 * 1024) Check(before.reads.textBytes >= bytes * 3);
        if (command == IDM_VIEW_LONGLINEMARKER) Check(after.peerReads.rangeCalls == 0 && after.peerReads.textCalls == 0);
    };
    const std::vector<UINT> commands{IDM_VIEW_TABSETTINGS, IDM_VIEW_LONGLINESETTINGS, IDM_VIEW_LONGLINEMARKER};
    const auto smallCorpus = inputs(1024);
    for (UINT command : commands) {
        prepare(smallCorpus); const auto before = Command(frame, documents, command, true);
        prepare(smallCorpus); const auto after = Command(frame, documents, command, false); check(command, before, after, 1024);
    }
    auto editPeer = [&] {
        auto& peer = *documents[1]; const std::string replacement = "fill-column: 91\n";
        peer.Send(SCI_SETREADONLY, FALSE); peer.Send(SCI_SETTARGETRANGE, 0, smallCorpus[1].find('\n') + 1);
        peer.Send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data())); peer.Send(SCI_SETREADONLY, TRUE);
    };
    prepare(smallCorpus); editPeer(); const auto editedBefore = Command(frame, documents, IDM_VIEW_TABSETTINGS, true);
    prepare(smallCorpus); editPeer(); const auto editedAfter = Command(frame, documents, IDM_VIEW_TABSETTINGS, false);
    check(IDM_VIEW_TABSETTINGS, editedBefore, editedAfter, 1024); Check(editedAfter.documents[1].editor[3] != 0);
    fprintf(stdout, "Modeline boundaries, edited declarations, peer preference bytes/settings/selection and existing preference regressions passed.\n"); fflush(stdout);
    if (benchmark) {
        const auto corpus = inputs(8 * 1024 * 1024);
        fprintf(stdout, "operation,variant,pair,peer_bytes,callback_ms,correct_paint_ms,ui_cpu_ms,sampled_peak_private,text_calls,text_bytes,range_calls,range_bytes\n"); fflush(stdout);
        for (UINT command : commands) for (int pair = 0; pair < 7; ++pair) {
            Result before, after;
            for (bool old : {pair % 2 == 0, pair % 2 != 0}) {
                prepare(corpus); auto result = Command(frame, documents, command, old);
                fprintf(stdout, "%u,%s,%d,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%zu\n", command, old ? "before" : "after", pair,
                    corpus[1].size() + corpus[2].size() + corpus[3].size(), result.callback, result.painted, result.cpu, result.peak,
                    result.reads.textCalls, result.reads.textBytes, result.reads.rangeCalls, result.reads.rangeBytes); fflush(stdout);
                (old ? before : after) = std::move(result);
            }
            check(command, before, after, 8 * 1024 * 1024);
        }
    }
    for (auto* d : documents) { d->Send(SCI_SETREADONLY, FALSE); d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; }
    while (frame.tabs.GetPageCount()) Check(frame.Close(frame.tabs.GetPageCount() - 1, false));
    std::filesystem::remove_all(directory);
}

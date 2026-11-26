#pragma once
#include "StatusPresentationBaseline.h"
#include "../plugins/PluginApi.h"
#include "../src/StyleManager.h"
#include <chrono>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <source_location>
#include <psapi.h>

struct StatusPresentationSnapshot {
    std::array<std::wstring, 13> text;
    std::array<int, 13> edges{};
    std::array<UINT, 13> types{};
    LRESULT margin = 0;
    bool operator==(const StatusPresentationSnapshot&) const = default;
};
inline StatusPresentationSnapshot ReadStatusPresentation(MainFrame& frame) {
    StatusPresentationSnapshot result;
    for (size_t i = 0; i < result.text.size(); ++i) {
        const auto size = SendMessageW(frame.m_hWndStatusBar, SB_GETTEXTLENGTHW, i, 0);
        result.types[i] = HIWORD(size); result.text[i].resize(LOWORD(size) + 1);
        const auto length = SendMessageW(frame.m_hWndStatusBar, SB_GETTEXTW, i, reinterpret_cast<LPARAM>(result.text[i].data()));
        result.text[i].resize(LOWORD(length));
    }
    SendMessageW(frame.m_hWndStatusBar, SB_GETPARTS, result.edges.size(), reinterpret_cast<LPARAM>(result.edges.data()));
    if (const auto* d = frame.Active()) result.margin = d->Send(SCI_GETMARGINWIDTHN, 0);
    return result;
}
struct StatusPublishCounts { size_t texts = 0, parts = 0, simpleTexts = 0; };
inline LRESULT CALLBACK StatusPublishCounter(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto& count = *reinterpret_cast<StatusPublishCounts*>(data);
    if (message == SB_SETTEXTW || message == SB_SETTEXTA) { if ((w & 255) == 255) ++count.simpleTexts; else ++count.texts; }
    if (message == SB_SETPARTS) ++count.parts;
    return DefSubclassProc(window, message, w, l);
}
inline void ResetStatusPresentation(MainFrame& frame) {
    frame.statusValuesValid = 0; frame.statusMeasureInvalid = frame.statusPartsInvalid = true;
    frame.statusInvalid = (1u << 13) - 1;
}
inline void RunStatusPresentationBenchmark(MainFrame& frame, EditorDocument& document, StatusPublishCounts& messages);
inline void RunStatusPresentationTests(MainFrame& frame, bool benchmark = false) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Status presentation regression at " + std::to_string(where.line()));
    };
    auto* d = frame.NewDocument(); require(d != nullptr && frame.statusObserved);
    d->style.completion = false; d->style.markOccurrences = 0; d->style.matchBraces = false;
    d->Load({"one two three\nsecond line\n"}); d->Send(SCI_GOTOPOS, 2);
    frame.ShowWindow(SW_SHOWNORMAL); SetFocus(d->hwnd); frame.UpdateStatus();
    StatusPublishCounts messages;
    require(SetWindowSubclass(frame.m_hWndStatusBar, StatusPublishCounter, 0x460, reinterpret_cast<DWORD_PTR>(&messages)) != FALSE);
    struct RemoveCounter { HWND window; ~RemoveCounter() { RemoveWindowSubclass(window, StatusPublishCounter, 0x460); } } removeCounter{frame.m_hWndStatusBar};
    auto compareBaseline = [&](const wchar_t* format = nullptr) {
        frame.UpdateStatus(); const auto changed = ReadStatusPresentation(frame);
        frame.statusWidth = -1; // Force the reference to recompute native extents/parts, not reuse the implementation's cache.
        BaselineStatusPresentation(frame, format); require(changed == ReadStatusPresentation(frame));
        ResetStatusPresentation(frame); frame.UpdateStatus();
    };
    compareBaseline(); statusMeasurementCounters = {}; messages = {};
    frame.UpdateStatus(); frame.UpdateStatus();
    require(statusMeasurementCounters.extents == 0 && statusMeasurementCounters.margins == 0 && messages.texts == 0 && messages.parts == 0);
    statusMeasurementCounters = {}; messages = {};
    SendMessageW(d->hwnd, WM_KEYDOWN, VK_RIGHT, 1); frame.UpdateStatus();
    require(statusMeasurementCounters.extents == 2 && statusMeasurementCounters.margins == 0 && messages.texts == 2);
    compareBaseline();
    for (UINT part = 0; part < 6; ++part) {
        require(frame.plugins.Message(NPPM_SETSTATUSBAR, part, reinterpret_cast<LPARAM>(L"plugin text")) != FALSE);
        require(ReadStatusPresentation(frame).text[part] == L"plugin text");
        statusMeasurementCounters = {}; messages = {}; frame.UpdateStatus();
        require(ReadStatusPresentation(frame).text == frame.statusText && messages.texts == 1 && messages.parts == 0 && statusMeasurementCounters.extents == 0);
    }
    SendMessageA(frame.m_hWndStatusBar, SB_SETTEXTA, 3, reinterpret_cast<LPARAM>("native ANSI"));
    frame.UpdateStatus(); require(ReadStatusPresentation(frame).text == frame.statusText);
    SendMessageW(frame.m_hWndStatusBar, SB_SETTEXTW, 2 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(frame.statusText[2].c_str()));
    frame.UpdateStatus(); require(ReadStatusPresentation(frame).types[2] == 0);
    d->search.findText = L"not present anywhere"; d->search.wrap = false; frame.FindNext();
    require(ReadStatusPresentation(frame).text[0] == L"No further match found.");
    frame.UpdateStatus(); require(ReadStatusPresentation(frame).text == frame.statusText);
    SendMessageW(frame.m_hWndStatusBar, SB_SIMPLE, TRUE, 0);
    SendMessageW(frame.m_hWndStatusBar, SB_SETTEXTW, 255 | SBT_NOBORDERS, reinterpret_cast<LPARAM>(L"menu help"));
    statusMeasurementCounters = {}; messages = {}; frame.UpdateStatus();
    wchar_t help[64]{}; SendMessageW(frame.m_hWndStatusBar, SB_GETTEXTW, 0, reinterpret_cast<LPARAM>(help));
    require(std::wstring_view(help) == L"menu help" && SendMessageW(frame.m_hWndStatusBar, SB_ISSIMPLE, 0, 0) && messages.texts == 0 && messages.simpleTexts == 0);
    SendMessageW(frame.m_hWndStatusBar, SB_SIMPLE, FALSE, 0);
    auto wrongEdges = frame.statusEdges; wrongEdges[0] += 17;
    SendMessageW(frame.m_hWndStatusBar, SB_SETPARTS, wrongEdges.size(), reinterpret_cast<LPARAM>(wrongEdges.data()));
    messages = {}; frame.UpdateStatus(); require(messages.parts == 1 && ReadStatusPresentation(frame).edges == frame.statusEdges);
    for (const wchar_t* format : {L"Custom %s / %s", L"Custom %s\n", L"", L"%2000s"}) {
        frame.statusFormat = format; frame.statusFormatValid = true; frame.statusLanguage = GetThreadUILanguage(); ResetStatusPresentation(frame);
        compareBaseline(format);
    }
    frame.statusFormatValid = false; ResetStatusPresentation(frame); compareBaseline();
    const auto language = GetThreadUILanguage();
    {struct RestoreLanguage { LANGID value; ~RestoreLanguage() { SetThreadUILanguage(value); } } restore{language};
        SetThreadUILanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)); compareBaseline();
    }
    compareBaseline();
    statusMeasurementCounters = {}; frame.SendMessage(WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"intl")); frame.UpdateStatus();
    require(statusMeasurementCounters.localeQueries == 1 && statusMeasurementCounters.formatLoads == 1);
    frame.UpdateStatus(); require(statusMeasurementCounters.localeQueries == 1 && statusMeasurementCounters.formatLoads == 1);
    for (int count : {111, 888, 1000, 999}) {
        d->Load({std::string(count - 1, '\n') + "01234567890123456789"});
        SendMessageW(d->hwnd, SCI_STYLESETFONT, STYLE_LINENUMBER, reinterpret_cast<LPARAM>("Georgia"));
        statusMeasurementCounters = {}; frame.UpdateStatus(); require(statusMeasurementCounters.margins == 1);
        const auto number = "__" + std::to_string(count);
        require(d->Send(SCI_GETMARGINWIDTHN, 0) == SendMessageW(d->hwnd, SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<LPARAM>(number.c_str())));
        compareBaseline();
    }
    auto verifyFontChange = [&](UINT command, WPARAM parameter, LPARAM value) {
        SendMessageW(d->hwnd, command, parameter, value); statusMeasurementCounters = {}; frame.UpdateStatus();
        require(statusMeasurementCounters.margins == 1); compareBaseline();
    };
    verifyFontChange(SCI_SETZOOM, 125, 0);
    verifyFontChange(SCI_STYLESETSIZEFRACTIONAL, STYLE_LINENUMBER, 1333);
    verifyFontChange(SCI_STYLESETWEIGHT, STYLE_LINENUMBER, 600);
    verifyFontChange(SCI_STYLESETITALIC, STYLE_LINENUMBER, TRUE);
    verifyFontChange(SCI_STYLESETSTRETCH, STYLE_LINENUMBER, SC_STRETCH_EXPANDED);
    verifyFontChange(SCI_STYLESETCHARACTERSET, STYLE_LINENUMBER,
        d->Send(SCI_STYLEGETCHARACTERSET, STYLE_LINENUMBER) == SC_CHARSET_ANSI ? SC_CHARSET_DEFAULT : SC_CHARSET_ANSI);
    verifyFontChange(SCI_SETFONTQUALITY, SC_EFF_QUALITY_ANTIALIASED, 0);
    verifyFontChange(SCI_SETFONTLOCALE, 0, reinterpret_cast<LPARAM>("ja-JP"));
    const auto wantedMargin = d->Send(SCI_GETMARGINWIDTHN, 0);
    d->Send(SCI_SETMARGINWIDTHN, 0, wantedMargin + 7); statusMeasurementCounters = {}; frame.UpdateStatus();
    require(d->Send(SCI_GETMARGINWIDTHN, 0) == wantedMargin && statusMeasurementCounters.margins == 0);
    d->Send(SCI_SETMARGINWIDTHN, 0, 0); frame.UpdateStatus(); require(d->Send(SCI_GETMARGINWIDTHN, 0) == 0);
    d->Send(SCI_SETMARGINWIDTHN, 0, wantedMargin); frame.UpdateStatus();
    auto theme = std::make_shared<ThemeDefinition>(); theme->legacy["global.marginlinenumber"] = L"font:Arial; size:15; bold";
    d->style.theme = theme; ConfigureDocumentStyle(*d); statusMeasurementCounters = {}; frame.UpdateStatus();
    require(statusMeasurementCounters.margins == 1); compareBaseline(); d->style.theme.reset(); ConfigureDocumentStyle(*d);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(frame.m_hWndStatusBar, WM_GETFONT, 0, 0));
    LOGFONTW description{}; GetObjectW(font ? font : GetStockObject(DEFAULT_GUI_FONT), sizeof(description), &description); description.lfHeight -= 3;
    const auto replacement = CreateFontIndirectW(&description); require(replacement != nullptr);
    {struct RestoreFont { HWND window; HFONT temporary; ~RestoreFont() { SendMessageW(window, WM_SETFONT, 0, TRUE); DeleteObject(temporary); } } restoreFont{frame.m_hWndStatusBar, replacement};
        SendMessageW(frame.m_hWndStatusBar, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
        statusMeasurementCounters = {}; frame.UpdateStatus(); require(statusMeasurementCounters.extents > 0); compareBaseline();
    }
    compareBaseline();
    RECT bounds{}; frame.GetWindowRect(&bounds); statusMeasurementCounters = {};
    frame.SetWindowPos(nullptr, 0, 0, bounds.right - bounds.left + 80, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    frame.UpdateStatus(); require(statusMeasurementCounters.extents == 0); compareBaseline();
    frame.GetWindowRect(&bounds); const auto dpi = GetDpiForWindow(frame); statusMeasurementCounters = {};
    frame.SendMessage(WM_DPICHANGED, MAKELPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&bounds)); frame.UpdateStatus();
    require(statusMeasurementCounters.margins > 0 && statusMeasurementCounters.extents > 0); compareBaseline();
    d->Load({std::string(1024 * 1024, 'x')}); d->Send(SCI_GOTOPOS, d->Send(SCI_GETLENGTH)); frame.UpdateStatus();
    const auto deadline = GetTickCount64() + 10000;
    while (d->statusStatistics.Update(*d)) require(GetTickCount64() < deadline);
    compareBaseline(); d->Send(SCI_SETMULTIPLESELECTION, TRUE); d->Send(SCI_SETSEL, 1, 9); d->Send(SCI_ADDSELECTION, 32, 23); compareBaseline();
    d->Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE); compareBaseline(); d->Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM); d->Send(SCI_CLEARSELECTIONS);
    auto* peer = frame.NewDocument(); require(peer != nullptr); peer->Load({"peer\n"}); compareBaseline();
    require(frame.Close(frame.tabs.GetActivePage(), false)); compareBaseline();
    const auto mappedPath = std::filesystem::temp_directory_path() / (L"multipad-status-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    std::string mappedText; for (int i = 0; i < 20000; ++i) mappedText += "mapped line\n";
    files::WriteAtomic(mappedPath, {mappedText}); auto* mapped = frame.NewDocument(); require(mapped != nullptr); mapped->path = mappedPath;
    mapped->OpenFastFile(mappedPath, {}, files::Encoding::Utf8);
    auto* child = frame.tabs.Child(frame.tabs.GetActivePage()); child->m_hWndClient = mapped->ViewWindow();
    SetParent(mapped->ViewWindow(), child->m_hWnd); ShowWindow(mapped->ViewWindow(), SW_SHOW); child->UpdateLayout(); compareBaseline();
    mapped->fastView->Seek(mapped->fastView->Size(), true); compareBaseline();
    require(frame.Close(frame.tabs.GetActivePage(), false)); std::filesystem::remove(mappedPath); compareBaseline();
    if (benchmark) RunStatusPresentationBenchmark(frame, *d, messages);
    d->Send(SCI_SETSAVEPOINT); while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1, false));
}
inline void RunStatusPresentationBenchmark(MainFrame& frame, EditorDocument& document, StatusPublishCounts& messages) {
    using Clock = std::chrono::steady_clock;
    auto require = [](bool value) { if (!value) throw std::runtime_error("Status presentation paired state mismatch."); };
    std::string corpus(200, 'a'); corpus += "\t中😀\n";
    for (int i = 0; i < 1000; ++i) corpus += "row\n";
    document.style.completion = false; document.style.markOccurrences = 0; document.style.matchBraces = false;
    document.Send(SCI_SETZOOM, 100); frame.settings.Set(IDM_VIEW_SHOWEXCERPT, 0);
    struct RestoreBaseline { ~RestoreBaseline() { statusPresentationBaseline = false; } } restore;
    auto milliseconds = [](auto start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
    auto cpu = [] {
        FILETIME created{}, exited{}, kernel{}, user{}; GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
        return ((static_cast<unsigned long long>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) / 10000.0
            + ((static_cast<unsigned long long>(user.dwHighDateTime) << 32) | user.dwLowDateTime) / 10000.0;
    };
    struct Sample { double callback, paint, completion; size_t extents, margins, texts, parts; unsigned long long state; };
    RECT client{}; frame.GetClientRect(&client); LOGFONTW statusFont{};
    HDC statusDC = GetDC(frame.m_hWndStatusBar); const auto font = reinterpret_cast<HFONT>(SendMessageW(frame.m_hWndStatusBar, WM_GETFONT, 0, 0));
    require(statusDC != nullptr);
    const auto fontBytes = GetObjectW(font ? font : GetCurrentObject(statusDC, OBJ_FONT), sizeof(statusFont), &statusFont);
    ReleaseDC(frame.m_hWndStatusBar, statusDC); require(fontBytes == sizeof(statusFont) && statusFont.lfHeight != 0);
    const auto fontName = files::ToUtf8(statusFont.lfFaceName);
    fprintf(stdout, "StatusSettings tabs%d bytes%zu modeordinary encodingUTF8 lineShape200ASCII+tab+CJK+emoji_then1000rows ISA_SSE4_2 zoom100 marginFontGeorgia plugins%d excerpt0 warm1 temporaryIO0 language%u dpi%u client%ldx%ld statusFont%s height%ld weight%ld separator%u\n",
        frame.tabs.GetPageCount(), corpus.size(), frame.plugins.enabled, GetThreadUILanguage(), GetDpiForWindow(frame), client.right, client.bottom, fontName.c_str(), statusFont.lfHeight, statusFont.lfWeight, static_cast<unsigned>(frame.statusSeparator));
    for (const char* workload : {"caret", "typing", "idle"}) for (int run = 0; run < 5; ++run) {
        std::vector<StatusPresentationSnapshot> previous;
        for (int side = 0; side < 2; ++side) {
            const bool baseline = (run % 2 == 0) == (side == 0);
            statusPresentationBaseline = false; document.Load({corpus}); document.Send(SCI_GOTOPOS, 0);
            document.Send(SCI_STYLESETFONT, STYLE_LINENUMBER, reinterpret_cast<LPARAM>("Georgia"));
            SetFocus(document.hwnd); ResetStatusPresentation(frame); statusPresentationBaseline = baseline;
            if (baseline) frame.statusWidth = -1;
            frame.Refresh(); frame.UpdateWindow();
            MSG message{}; while (PeekMessageW(&message, frame, WM_DOCUMENT_CHANGED, WM_DOCUMENT_CHANGED, PM_REMOVE)) {}
            statusPresentationBaseline = baseline; statusMeasurementCounters = {}; messages = {};
            std::vector<Sample> samples; double cpuTotal = 0;
            IO_COUNTERS ioBefore{}, ioAfter{}; GetProcessIoCounters(GetCurrentProcess(), &ioBefore);
            size_t oldExtents = 0, oldMargins = 0, oldTexts = 0, oldParts = 0;
            for (int operation = 0; operation < 100; ++operation) {
                const auto cpuStart = cpu(); const auto start = Clock::now();
                if (strcmp(workload, "caret") == 0) SendMessageW(document.hwnd, WM_KEYDOWN, VK_RIGHT, 1);
                else if (strcmp(workload, "typing") == 0) SendMessageW(document.hwnd, WM_CHAR, 'x', 1);
                else SendMessageW(document.hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
                frame.SendMessage(WM_DOCUMENT_CHANGED, 1); const auto callback = milliseconds(start);
                const auto deadline = GetTickCount64() + 10000;
                while (!document.statusStatistics.Ready(0) || !document.statusStatistics.Ready(1) || !document.statusStatistics.SelectionReady()) {
                    require(GetTickCount64() < deadline); frame.SendMessage(WM_TIMER, 2);
                }
                const auto completed = milliseconds(start);
                RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
                RedrawWindow(document.ViewWindow(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                UpdateWindow(frame.GetMDITabCtrl()); UpdateWindow(frame.m_hWndStatusBar);
                const auto paint = milliseconds(start); cpuTotal += cpu() - cpuStart;
                const auto snapshot = ReadStatusPresentation(frame);
                if (!side) previous.push_back(snapshot); else require(previous[operation] == snapshot);
                unsigned long long state = 1469598103934665603ULL;
                for (size_t i = 0; i < snapshot.text.size(); ++i) {
                    for (const auto ch : snapshot.text[i]) state = (state ^ static_cast<unsigned>(ch)) * 1099511628211ULL;
                    state = (state ^ static_cast<unsigned>(snapshot.edges[i])) * 1099511628211ULL;
                }
                samples.push_back({callback, paint, completed, statusMeasurementCounters.extents - oldExtents,
                    statusMeasurementCounters.margins - oldMargins, messages.texts - oldTexts, messages.parts - oldParts, state});
                oldExtents = statusMeasurementCounters.extents; oldMargins = statusMeasurementCounters.margins; oldTexts = messages.texts; oldParts = messages.parts;
            }
            GetProcessIoCounters(GetCurrentProcess(), &ioAfter);
            if (strcmp(workload, "caret") == 0) require(document.Send(SCI_GETCURRENTPOS) == 100);
            if (strcmp(workload, "typing") == 0) require(document.Text() == std::string(100, 'x') + corpus);
            std::vector<double> callbacks, paints;
            for (size_t i = 0; i < samples.size(); ++i) {
                const auto& sample = samples[i]; callbacks.push_back(sample.callback); paints.push_back(sample.paint);
                fprintf(stdout, "StatusPair,%s,%d,%d,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%llu\n", workload, run, baseline, i,
                    sample.callback, sample.paint, sample.completion, sample.extents, sample.margins, sample.texts, sample.parts, sample.state);
            }
            std::sort(callbacks.begin(), callbacks.end()); std::sort(paints.begin(), paints.end());
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            fprintf(stdout, "StatusSummary,%s,%d,%d,callback50/95/99/max=%.6f/%.6f/%.6f/%.6f,paint50/95/99/max=%.6f/%.6f/%.6f/%.6f,uiCPU=%.3f,GDI=%zu,SciTextWidth=%zu,SBtext=%zu,SBparts=%zu,locale=%zu,formatLoads=%zu,private=%zu,peakCommit=%zu,readBytes=%llu,writeBytes=%llu\n",
                workload, run, baseline, callbacks[49], callbacks[94], callbacks[98], callbacks[99], paints[49], paints[94], paints[98], paints[99], cpuTotal,
                statusMeasurementCounters.extents, statusMeasurementCounters.margins, messages.texts, messages.parts, statusMeasurementCounters.localeQueries, statusMeasurementCounters.formatLoads,
                memory.PrivateUsage, memory.PeakPagefileUsage, ioAfter.ReadTransferCount - ioBefore.ReadTransferCount, ioAfter.WriteTransferCount - ioBefore.WriteTransferCount);
        }
    }
}

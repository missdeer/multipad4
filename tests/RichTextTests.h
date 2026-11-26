#pragma once
#include "MainFrame.h"
#include "EditClipboard.h"
#include "EditRichText.h"
#include "EditorFeatureTests.h"
#include "EditTransformTests.h"
#include "RichTextMetrics.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <psapi.h>
#include <thread>
#include <source_location>
#include <cctype>
void BaselineCopyRichText(EditorDocument&);
namespace richtexttest {
inline void Require(bool ok, std::source_location where = std::source_location::current()) {
    if (!ok) throw std::runtime_error("Rich text paired regression at line " + std::to_string(where.line()));
}
struct Clipboard {
    std::vector<UINT> formats;
    std::string unicode, rtf;
    bool operator==(const Clipboard&) const = default;
};
inline Clipboard ReadClipboard(HWND owner, size_t unicodeUnits) {
    Require(OpenEditClipboard(owner));
    struct Close { ~Close() { CloseClipboard(); } } close;
    Clipboard result;
    for (UINT format = 0; (format = EnumClipboardFormats(format)) != 0;) result.formats.push_back(format);
    const auto read = [&](UINT format, size_t length) {
        const auto memory = GetClipboardData(format); Require(memory != nullptr);
        const auto size = GlobalSize(memory); const auto* data = static_cast<const char*>(GlobalLock(memory)); Require(data != nullptr);
        struct Unlock { HGLOBAL memory; ~Unlock() { GlobalUnlock(memory); } } unlock{memory};
        std::string bytes;
        if (length) { Require(size >= length); bytes.assign(data, length); }
        else { const auto* end = static_cast<const char*>(memchr(data, 0, size)); Require(end != nullptr); bytes.assign(data, end); }
        return bytes;
    };
    result.unicode = read(CF_UNICODETEXT, (unicodeUnits + 1) * sizeof(wchar_t));
    const auto format = RegisterClipboardFormatW(L"Rich Text Format");
    if (IsClipboardFormatAvailable(format)) result.rtf = read(format, 0);
    return result;
}
struct Calls {
    size_t styleQueries = 0, attributes = 0, positions = 0, styledReads = 0, styledBytes = 0, rangeBytes = 0;
};
struct Trace { SciFnDirect original; HWND window; Calls calls; bool failRead = false; };
inline thread_local Trace* trace = nullptr;
inline sptr_t Direct(sptr_t pointer, unsigned message, uptr_t w, sptr_t l) {
    auto& t = *trace;
    if (message == SCI_GETSTYLEINDEXAT) ++t.calls.styleQueries;
    if (message == SCI_POSITIONAFTER || message == SCI_POSITIONBEFORE) ++t.calls.positions;
    if (message == SCI_STYLEGETFONT || message == SCI_STYLEGETCHARACTERSET || message == SCI_STYLEGETFORE || message == SCI_STYLEGETBACK ||
        message == SCI_STYLEGETSIZEFRACTIONAL || message == SCI_STYLEGETWEIGHT || message == SCI_STYLEGETITALIC || message == SCI_STYLEGETUNDERLINE ||
        message == SCI_STYLEGETSTRIKE || message == SCI_STYLEGETEOLFILLED) ++t.calls.attributes;
    if (message == SCI_GETSTYLEDTEXTFULL) {
        if (t.failRead) { t.failRead = false; return 0; }
        const auto& range = *reinterpret_cast<const Sci_TextRangeFull*>(l);
        ++t.calls.styledReads; t.calls.styledBytes += 2 * static_cast<size_t>(range.chrg.cpMax - range.chrg.cpMin) + 2;
    }
    if (message == SCI_GETTEXTRANGEFULL) {
        const auto& range = *reinterpret_cast<const Sci_TextRangeFull*>(l); t.calls.rangeBytes += static_cast<size_t>(range.chrg.cpMax - range.chrg.cpMin);
    }
    if (message == SCI_GETTEXT && w) t.calls.rangeBytes += static_cast<size_t>(w - 1);
    return t.original ? t.original(pointer, message, w, l) : SendMessageW(t.window, message, w, l);
}
struct Case {
    const char* name;
    std::string text;
    bool dense = false, tabs = false, copyAll = false, rectangle = false, multiple = false;
    LRESULT anchor = 0, caret = -1;
    bool plain = false;
};
struct Result {
    Clipboard clipboard;
    transformtest::State state;
    Calls calls;
    Metrics conversions;
    double callback = 0, paint = 0, cpu = 0;
    SIZE_T privateBefore = 0, peakPrivate = 0;
};
inline Result Run(MainFrame& frame, EditorDocument& d, const Case& test, bool old, bool sample) {
    d.Send(SCI_SETREADONLY, FALSE); d.Send(SCI_SETSELECTIONMODE, SC_SEL_STREAM); d.Send(SCI_CLEARSELECTIONS);
    Require(SelectDocumentScheme(d, test.plain ? L"Text" : L"cpp")); d.Load({test.text}); d.Send(SCI_SETTABWIDTH, 4); d.Send(SCI_SETUSETABS, test.tabs);
    d.Send(SCI_SETWRAPMODE, SC_WRAP_NONE); d.style.completion = false; d.style.markOccurrences = 0;
    const auto end = test.caret < 0 ? d.Send(SCI_GETLENGTH) : test.caret;
    d.Send(SCI_SETSEL, test.anchor, end);
    if (test.rectangle) { d.Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE); d.Send(SCI_SETRECTANGULARSELECTIONANCHOR, 1); d.Send(SCI_SETRECTANGULARSELECTIONCARET, (std::min)(LRESULT(10), end)); }
    if (test.multiple) { d.Send(SCI_SETMULTIPLESELECTION, TRUE); d.Send(SCI_ADDSELECTION, (std::min)(LRESULT(11), end), (std::min)(LRESULT(9), end)); }
    for (int style : {0, 1, 2, 3, 4, 5, 6, 7, STYLE_DEFAULT, 200}) {
        d.Send(SCI_STYLESETFONT, style, reinterpret_cast<LPARAM>(style % 2 ? "Arial" : "Consolas"));
        d.Send(SCI_STYLESETSIZEFRACTIONAL, style, 1000 + style * 13);
        d.Send(SCI_STYLESETFORE, style, RGB(style * 11, style * 17, style * 23));
        d.Send(SCI_STYLESETBACK, style, RGB(230 - style, 240 - style, 250 - style));
        d.Send(SCI_STYLESETWEIGHT, style, style % 2 ? FW_BOLD : FW_NORMAL); d.Send(SCI_STYLESETITALIC, style, style % 3 == 1);
        d.Send(SCI_STYLESETUNDERLINE, style, style % 3 == 2); d.Send(SCI_STYLESETSTRIKE, style, style % 4 == 3);
        d.Send(SCI_STYLESETCHARACTERSET, style, style == 2 ? SHIFTJIS_CHARSET : ANSI_CHARSET); d.Send(SCI_STYLESETEOLFILLED, style, style == 3);
    }
    std::vector<unsigned char> styles(test.text.size());
    for (size_t i = 0; i < styles.size(); ++i) {
        const auto byte = static_cast<unsigned char>(test.text[i]);
        styles[i] = test.text[i] == '\r' ? 2 : test.text[i] == '\n' ? 3 : (byte & 0xC0) == 0x80 ? 200 : test.dense ? static_cast<unsigned char>(i % 8) : 0;
    }
    d.Send(SCI_STARTSTYLING, 0); d.Send(SCI_SETSTYLINGEX, styles.size(), reinterpret_cast<LPARAM>(styles.data()));
    const auto before = transformtest::Capture(d);
    auto selected = test.copyAll ? test.text : d.Range(d.Send(SCI_GETSELECTIONSTART), d.Send(SCI_GETSELECTIONEND) - d.Send(SCI_GETSELECTIONSTART));
    if (!test.copyAll && selected.empty()) selected = test.text;
    // Both native CopyText and CopyRange use SelectionText::FixSelectionForClipboard.
    if (test.copyAll) std::replace(selected.begin(), selected.end(), '\0', ' ');
    const std::string seed = "private clipboard sentinel";
    d.Send(SCI_COPYTEXT, seed.size(), reinterpret_cast<LPARAM>(seed.data()));
    const auto command = test.copyAll ? IDM_EDIT_COPYALL : IDM_EDIT_COPYRTF;
    const bool enabled = frame.State(command).enabled;
    Require(enabled == (test.copyAll ? !test.text.empty() : !d.Send(SCI_GETSELECTIONEMPTY)));
    const auto expectedWide = files::FromUtf8(enabled ? selected : seed);
    const auto deadline = GetTickCount64() + 10000;
    while (d.statusStatistics.Update(d)) Require(GetTickCount64() < deadline);
    frame.Refresh(); frame.UpdateWindow();
    Trace counter{d.directFunction, d.hwnd}; trace = &counter; d.directFunction = Direct;
    struct Restore { EditorDocument& d; SciFnDirect original; ~Restore() { d.directFunction = original; trace = nullptr; baseline = nullptr; } } restore{d, counter.original};
    baseline = old ? BaselineCopyRichText : nullptr; metrics = {};
    Result result; result.privateBefore = transformtest::PrivateBytes(); std::atomic<SIZE_T> peak{result.privateBefore};
    std::optional<std::jthread> sampler;
    if (sample) sampler.emplace([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) peak.store((std::max)(peak.load(), memory.PrivateUsage));
            Sleep(1);
        }
    });
    const auto cpu = transformtest::CpuMilliseconds(); const auto started = std::chrono::steady_clock::now();
    frame.SendMessage(WM_COMMAND, command);
    result.callback = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    frame.SendMessage(WM_DOCUMENT_CHANGED, 0); RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    result.paint = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); result.cpu = transformtest::CpuMilliseconds() - cpu;
    result.calls = counter.calls; result.conversions = metrics;
    if (sampler) { sampler->request_stop(); sampler->join(); } result.peakPrivate = peak.load();
    result.clipboard = ReadClipboard(d.hwnd, expectedWide.size()); result.state = transformtest::Capture(d);
    Require(result.state == before);
    if (enabled && !test.copyAll && selected.find('\0') != std::string::npos) Require(result.clipboard.rtf.find("\\u0?") != std::string::npos);
    if (result.clipboard.unicode != std::string(reinterpret_cast<const char*>(expectedWide.c_str()), (expectedWide.size() + 1) * sizeof(wchar_t))) {
        fprintf(stderr, "Unicode actual/expected (%zu units):", expectedWide.size());
        for (size_t i = 0; i < result.clipboard.unicode.size(); ++i) fprintf(stderr, " %02x", static_cast<unsigned char>(result.clipboard.unicode[i]));
        fprintf(stderr, " /");
        for (size_t i = 0; i < (expectedWide.size() + 1) * sizeof(wchar_t); ++i) fprintf(stderr, " %02x", reinterpret_cast<const unsigned char*>(expectedWide.c_str())[i]);
        fprintf(stderr, "\n");
        throw std::runtime_error(std::string("Rich text expected complete Unicode payload mismatch: ") + test.name + (old ? " before" : " after"));
    }
    return result;
}
// Independent body oracle for the newline-only regression; reject unexpected text/control words.
inline std::string BreakBody(const std::string& rtf) {
    auto position = rtf.find("\\pard"); Require(position != std::string::npos);
    std::string body;
    while (position < rtf.size()) {
        const char c = rtf[position++];
        if (c == '\r' || c == '\n' || c == '}') continue;
        if (c != '\\') { body += c; continue; }
        const auto first = position;
        while (position < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[position]))) ++position;
        const auto word = rtf.substr(first, position - first); Require(!word.empty());
        if (position < rtf.size() && rtf[position] == '-') ++position;
        while (position < rtf.size() && std::isdigit(static_cast<unsigned char>(rtf[position]))) ++position;
        if (position < rtf.size() && rtf[position] == ' ') ++position;
        if (word == "par" || word == "line") body += '\n';
        else Require(word == "pard" || word == "box" || word == "cbpat" || word == "f" || word == "cf" || word == "highlight" ||
            word == "fs" || word == "b" || word == "i" || word == "ul" || word == "ulnone" || word == "strike");
    }
    return body;
}
inline void Pair(const Case& test, const Result& before, const Result& after) {
    if (test.text == "\r\n\n\r" && !test.copyAll) {
        // The old renderer skipped the LF after CRLF. Preserve all three source breaks.
        Require(before.clipboard.unicode == after.clipboard.unicode && before.clipboard.formats == after.clipboard.formats && before.state == after.state);
        const auto oldBody = before.clipboard.rtf.find("\\pard"), newBody = after.clipboard.rtf.find("\\pard");
        Require(oldBody != std::string::npos && newBody != std::string::npos && before.clipboard.rtf.substr(0, oldBody) == after.clipboard.rtf.substr(0, newBody));
        Require(BreakBody(before.clipboard.rtf) == "\n\n\n" && BreakBody(after.clipboard.rtf) == "\n\n\n\n");
        return;
    }
    if (before.clipboard != after.clipboard || before.state != after.state) {
        if (std::string_view(test.name) == "EOL-only")
            fprintf(stderr, "Independent EOL-only body breaks before%zu after%zu; input CRLF+LF+CR expects 3 plus terminal RTF paragraph1.\n", BreakBody(before.clipboard.rtf).size(), BreakBody(after.clipboard.rtf).size());
        const auto limit = (std::min)(before.clipboard.rtf.size(), after.clipboard.rtf.size()); size_t difference = 0;
        while (difference < limit && before.clipboard.rtf[difference] == after.clipboard.rtf[difference]) ++difference;
        fprintf(stderr, "RTF lengths %zu/%zu first difference %zu: before[%s] after[%s]; UnicodeEqual%d formatsEqual%d stateEqual%d\n",
            before.clipboard.rtf.size(), after.clipboard.rtf.size(), difference, before.clipboard.rtf.substr(difference, 100).c_str(), after.clipboard.rtf.substr(difference, 100).c_str(),
            before.clipboard.unicode == after.clipboard.unicode, before.clipboard.formats == after.clipboard.formats, before.state == after.state);
        throw std::runtime_error(std::string("Rich text exact bytes/formats/selection mismatch: ") + test.name);
    }
}
}
inline void RunRichTextTests(MainFrame& frame, bool benchmark) {
    using namespace richtexttest;
    wchar_t station[256]{}; DWORD needed = 0;
    Require(GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &needed));
    Require(_wcsicmp(station, L"WinSta0") != 0);
    auto* d = frame.NewDocument(); Require(d != nullptr); frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 0);
    frame.ShowWindow(SW_SHOWNORMAL); RunEditorFeatureTests(*d);
    {
        const std::string input = std::string("a\0", 2) + "é😀\r\n"; Require(SelectDocumentScheme(*d, L"cpp")); d->Load({input});
        std::vector<unsigned char> styles(input.size());
        for (size_t i = 0; i < styles.size(); ++i) styles[i] = static_cast<unsigned char>(i % 5);
        d->Send(SCI_STARTSTYLING, 0); d->Send(SCI_SETSTYLINGEX, styles.size(), reinterpret_cast<LPARAM>(styles.data()));
        std::vector<char> data(2 * input.size() + 2, 'x'); Sci_TextRangeFull range{{0, static_cast<LRESULT>(input.size())}, data.data()};
        Require(d->Send(SCI_GETSTYLEDTEXTFULL, 0, reinterpret_cast<LPARAM>(&range)) == static_cast<LRESULT>(input.size()));
        if (memcmp(data.data(), styles.data(), input.size()) != 0) {
            fprintf(stderr, "Planar fixture lexer%lld styled%lld bytes", d->Send(SCI_GETLEXER), d->Send(SCI_GETENDSTYLED));
            for (size_t i = 0; i < input.size(); ++i) fprintf(stderr, " %u/%u", static_cast<unsigned char>(data[i]), styles[i]);
            fprintf(stderr, "\n");
        }
        Require(memcmp(data.data(), styles.data(), input.size()) == 0 && data[input.size()] == 0 &&
            memcmp(data.data() + input.size() + 1, input.data(), input.size()) == 0 && data.back() == 0);
        const std::string seed = "read failure sentinel"; d->Send(SCI_COPYTEXT, seed.size(), reinterpret_cast<LPARAM>(seed.data()));
        const auto before = ReadClipboard(d->hwnd, seed.size());
        Trace counter{d->directFunction, d->hwnd}; counter.failRead = true; trace = &counter; d->directFunction = Direct;
        bool failed = false; try { CopyRichText(*d); } catch (const std::runtime_error&) { failed = true; }
        d->directFunction = counter.original; trace = nullptr;
        Require(failed && !counter.failRead && ReadClipboard(d->hwnd, seed.size()) == before);
    }
    std::vector<Case> cases;
    for (bool dense : {false, true}) for (bool tabs : {false, true}) {
        cases.push_back({"mixed", "{text} \\ é 中文 😀\tend\r\nnext\nlast\rfinal", dense, tabs});
        cases.push_back({"embedded-NUL", std::string("a\0b\r\n", 5) + "😀z", dense, tabs});
        cases.push_back({"cross-CRLF", std::string(65534, 'a') + "\r\nlast\té😀", dense, tabs});
        cases.push_back({"cross-UTF8", std::string(65534, 'a') + "😀z\r\n", dense, tabs});
        cases.push_back({"EOL-only", "\r\n\n\r", dense, tabs});
    }
    cases.push_back({"empty-document", ""});
    Case plain{"plain-no-styles", "plain é😀\tend\r\n"}; plain.plain = true; cases.push_back(std::move(plain));
    cases.push_back({"partial-backwards", "HEAD é😀{body}\tTAIL", true, false, false, false, false, 16, 5});
    cases.push_back({"empty-selection", "one é😀 two", true, false, false, false, false, 2, 2});
    cases.push_back({"partial-CR", "head\r\nnext", true, false, false, false, false, 4, 5});
    cases.push_back({"partial-LF", "head\r\nnext", true, false, false, false, false, 5, 6});
    cases.push_back({"copy-all-NUL", std::string("a\0b", 3) + "é😀\n", false, false, true});
    for (bool copyAll : {false, true}) {
        cases.push_back({"rectangle", "one two\nthree four", true, true, copyAll, true});
        cases.push_back({"multiple", "one two three four", true, false, copyAll, false, true});
    }
    for (const auto& test : cases) {
        const auto before = Run(frame, *d, test, true, false);
        const auto after = Run(frame, *d, test, false, false);
        Pair(test, before, after);
    }
    printf("Rich text exact RTF/Unicode bytes, formats and selection: %zu paired cases passed in isolated station.\n", cases.size()); fflush(stdout);
    if (benchmark) {
        std::string dense, mixed;
        for (int i = 0; i < 32768; ++i) { dense += "a{b}\\c\t"; mixed += "aé中😀{}\r\n\t"; }
        std::vector<Case> workloads{{"long-run", std::string(1024 * 1024, 'x') + "\r\n"}, {"dense-style", dense, true}, {"multibyte", mixed, true}, {"copy-all", std::string(8 * 1024 * 1024, 'x') + "é😀", false, false, true}};
        printf("variant,operation,pair,input_bytes,callback_ms,correct_redraw_ms,ui_cpu_ms,private_before,sampled_peak_private,style_queries,style_attributes,position_queries,styled_reads,styled_bytes,app_range_bytes,utf16_calls,utf16_bytes,rtf_bytes\n");
        for (const auto& test : workloads) for (int pair = 0; pair < 3; ++pair) {
            Result before, after;
            for (bool old : {pair % 2 == 0, pair % 2 != 0}) {
                auto result = Run(frame, *d, test, old, true); const auto& c = result.calls; const auto& m = result.conversions;
                printf("%s,%s,%d,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n", old ? "before" : "after", test.name, pair, test.text.size(), result.callback, result.paint, result.cpu,
                    static_cast<size_t>(result.privateBefore), static_cast<size_t>(result.peakPrivate), c.styleQueries, c.attributes, c.positions, c.styledReads, c.styledBytes, c.rangeBytes, m.utf16Calls, m.utf16Bytes, result.clipboard.rtf.size()); fflush(stdout);
                (old ? before : after) = std::move(result);
            }
            Pair(test, before, after);
        }
    }
    d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; frame.Close(frame.tabs.GetActivePage(), false);
}

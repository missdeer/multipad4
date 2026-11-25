#include "FileReplace.h"
#include "ThreadPool.h"
#include <windows.h>
#include <psapi.h>
#include "Scintilla.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <array>
#include <source_location>
#include <cmath>
// Search and replacement do not invoke the host case-conversion command.
char* EditMapTextCase(int, const char*, size_t&, UINT) noexcept { std::terminate(); }
using Clock = std::chrono::steady_clock;
static void Require(bool value, std::source_location where = std::source_location::current()) { if (!value) throw std::runtime_error("Replacement regression line " + std::to_string(where.line())); }
struct Editor {
    HWND window = CreateWindowExW(0, L"Scintilla", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Editor() { Require(window != nullptr); Send(SCI_SETCODEPAGE, SC_CP_UTF8); }
    ~Editor() { DestroyWindow(window); }
    LRESULT Send(UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(window, m, w, l); }
    std::pair<std::string, size_t> Replace(const std::string& input, const files::ReplacePattern& pattern) {
        Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(input.c_str()));
        Send(SCI_SETSEARCHFLAGS, pattern.flags);
        size_t count = 0;
        auto search = [&](LRESULT start) { Send(SCI_SETTARGETRANGE, start, Send(SCI_GETLENGTH)); return Send(SCI_SEARCHINTARGET, pattern.needle.size(), reinterpret_cast<LPARAM>(pattern.needle.data())); };
        // Baseline includes the original Finder pass and its retained text.
        auto retained = input;
        for (LRESULT start = 0; start <= Send(SCI_GETLENGTH);) {
            auto found = search(start); if (found < 0) break;
            auto end = Send(SCI_GETTARGETEND);
            if (found == end) { if (end == Send(SCI_GETLENGTH)) break; start = Send(SCI_POSITIONAFTER, end); }
            else start = end;
        }
        for (LRESULT start = 0; start <= Send(SCI_GETLENGTH);) {
            auto found = search(start); if (found < 0) break;
            auto length = Send(SCI_GETTARGETEND) - found;
            auto size = Send(pattern.captures ? SCI_REPLACETARGETRE : SCI_REPLACETARGET, pattern.replacement.size(), reinterpret_cast<LPARAM>(pattern.replacement.data()));
            ++count; start = found + size;
            if (!length) { if (start >= Send(SCI_GETLENGTH)) break; start = Send(SCI_POSITIONAFTER, start); }
        }
        std::string output(Send(SCI_GETLENGTH) + 1, '\0'); Send(SCI_GETTEXT, output.size(), reinterpret_cast<LPARAM>(output.data())); output.pop_back(); return {output, count};
    }
};
int main(int argc, char** argv) {
    try {
        Require(Scintilla_RegisterClasses(GetModuleHandleW(nullptr)));
        Editor editor;
        std::atomic<bool> cancel{false};
        if (argc > 1) {
            const bool worker = std::string(argv[1]).starts_with("worker");
            const bool cancellation = std::string(argv[1]) == "worker-cancel";
            const bool dense = argc > 2 && std::string(argv[2]) == "dense";
            std::string input(dense ? 2*1024*1024 : 320*1024*1024, dense ? 'a' : 'x');
            if (!dense) for (size_t at = 0; at < input.size(); at += 1024*1024) input.replace(at, 6, "needle");
            files::ReplacePattern pattern{dense ? "a" : "needle", dense ? "abcdefgh" : "replacement", SCFIND_MATCHCASE, false};
            auto corpus = std::filesystem::temp_directory_path() / (L"multipad-replace-benchmark-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
            files::WriteAtomic(corpus, {input}); input.clear(); input.shrink_to_fit();
            FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
            GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelBefore, &userBefore);
            auto begin = Clock::now(); double longest = 0, cancelUi = 0, cancelAt = 0; size_t count = 0; std::string output;
            if (worker) {
                auto task = work::Submit([&] { auto version = files::ReplacementVersion(corpus); auto result = files::ReplaceContents(files::ReadReplacement(corpus, cancel), pattern, cancel); files::CommitReplacement(corpus, result.text, version, cancel); return result; });
                while (!task.Ready()) { if (cancellation && !cancel && Clock::now() - begin > std::chrono::milliseconds(10)) { auto requested = Clock::now(); cancel = true; SendMessageW(editor.window, WM_NULL, 0, 0); cancelUi = std::chrono::duration<double,std::milli>(Clock::now()-requested).count(); cancelAt = std::chrono::duration<double,std::milli>(requested-begin).count(); } auto start = Clock::now(); MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } longest = (std::max)(longest, std::chrono::duration<double,std::milli>(Clock::now()-start).count()); Sleep(1); }
                try { auto result = task.Get(); output = std::move(result.text.utf8); count = result.replacements; }
                catch (...) { if (!cancellation) throw; Require(cancel); }
            } else {
                auto start = Clock::now(); auto result = editor.Replace(files::Read(corpus).utf8, pattern); files::WriteAtomic(corpus, {result.first}); output = std::move(result.first); count = result.second;
                longest = std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            }
            GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelAfter, &userAfter);
            auto ticks = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
            auto uiCpu = (ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) - ticks(userBefore)) / 10000.0;
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            const auto elapsed = std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
            if (cancellation) { output = files::Read(corpus).utf8; Require(output.size() == (dense ? 2*1024*1024 : 320*1024*1024)); }
            else Require(files::Read(corpus).utf8 == output);
            std::filesystem::remove(corpus);
            unsigned long long hash = 1469598103934665603ULL; for (unsigned char c : output) hash = (hash ^ c) * 1099511628211ULL;
            printf("%s,%s,elapsed_ms=%.3f,longest_ui_ms=%.3f,ui_cpu_ms=%.3f,peak_working_set=%zu,peak_private_bytes=%zu,cancel_ui_ms=%.3f,cancel_retire_ms=%.3f,count=%zu,output_bytes=%zu,hash=%llu\n",argv[1],dense?"dense":"sparse",elapsed,longest,uiCpu,memory.PeakWorkingSetSize,memory.PeakPagefileUsage,cancelUi,cancelAt ? elapsed-cancelAt : 0,count,output.size(),hash);
            return 0;
        }
        const int regex = SCFIND_REGEXP | SCFIND_POSIX | SCFIND_CXX11REGEX;
        for (auto [input, pattern] : std::vector<std::pair<std::string, files::ReplacePattern>>{
            {"one ONE stone one_ one", {"one", "two", SCFIND_WHOLEWORD, false}},
            {"é É 中文", {"é", "e", 0, false}},
            {"abc abc", {"(a)(bc)", "$2-$1", regex, true}},
            {"a\r\nb\n", {"^|$", "_", regex, true}},
            {"aaa", {"(?<=a)a", "b", regex, true}},
            {"aaa", {"(?=a)", "x", regex, true}},
            {"", {"^$", "empty", regex, true}},
            {"😀a😀", {"(?=.)", "_", regex, true}},
            {"a\nb", {"a.*b", "X", regex | SCFIND_REGEX_DOT_ALL, true}},
            {"a\r\nb", {"a*", "_", regex | SCFIND_REGEX_DOT_ALL, true}},
            {"a\r\nb", {"(?=.)", "_", regex | SCFIND_REGEX_DOT_ALL, true}},
            {"miss", {"absent", "longer", 0, false}},
            {"a\r\na\na\ra", {"a", "x\nxx", 0, false}},
            {"a\r\na\n", {"\n", "x", 0, false}},
            {"a\r\na\n", {"\r", "", 0, false}}
        }) {
            editor.Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(input.c_str()));
            editor.Send(SCI_SETSEARCHFLAGS, pattern.flags);
            std::vector<std::array<LRESULT, 4>> positions;
            for (LRESULT start = 0; start <= editor.Send(SCI_GETLENGTH);) {
                editor.Send(SCI_SETTARGETRANGE, start, editor.Send(SCI_GETLENGTH));
                auto found = editor.Send(SCI_SEARCHINTARGET, pattern.needle.size(), reinterpret_cast<LPARAM>(pattern.needle.data()));
                if (found < 0) break;
                auto end = editor.Send(SCI_GETTARGETEND), line = editor.Send(SCI_LINEFROMPOSITION, found);
                positions.push_back({found, end, line, editor.Send(SCI_POSITIONFROMLINE, line)});
                if (end == found) { if (end == editor.Send(SCI_GETLENGTH)) break; start = editor.Send(SCI_POSITIONAFTER, end); }
                else start = end;
            }
            auto expected = editor.Replace(input, pattern);
            auto actual = files::ReplaceContents({input}, pattern, cancel);
            Require(actual.hits.archive && actual.hits.archive->Count() == positions.size());
            auto hits = actual.hits.archive->Page(0, positions.size());
            for (size_t i = 0; i < hits.size(); ++i) Require(hits[i].start == positions[i][0] && hits[i].end == positions[i][1] && hits[i].line == positions[i][2] && hits[i].lineStart == positions[i][3]);
            if (actual.text.utf8 != expected.first || actual.replacements != expected.second) { fprintf(stderr, "Parity failure %s: expected %s got %s\n", pattern.needle.c_str(), expected.first.c_str(), actual.text.utf8.c_str()); return 2; }
        }
        {
            auto result = files::ReplaceContents({std::string("a\0a", 3)}, {"a", "growing\n", 0, false}, cancel);
            Require(result.replacements == 2 && result.text.utf8 == std::string("growing\n\0growing\n", 17));
            auto targets = result.hits.replacementTargets->Page(0, 2);
            Require(targets.size() == 2 && targets[0].utf8Start == 0 && targets[0].utf8End == 8 && targets[1].utf8Start == 9 && targets[1].utf8End == 17);
            auto adjacent = files::ReplaceContents({"aa"}, {"a", "XYZ", 0, false}, cancel);
            auto adjacentTargets = adjacent.hits.replacementTargets->Page(0, 2);
            Require(adjacentTargets[0].utf8End == 3 && adjacentTargets[1].utf8Start == 3 && adjacentTargets[1].utf8End == 6);
            auto growing = files::ReplaceContents({"one one", files::Encoding::Utf16LE}, {"one", "growing\nreplacement", 0, false}, cancel);
            auto spans = growing.hits.replacementTargets->Page(0, 2);
            Require(spans[0].nativeEnd == spans[0].utf8End * 2 && spans[1].nativeStart == spans[1].utf8Start * 2);
            auto zero = files::ReplaceContents({"ab"}, {"(?=.)", "_", regex, true}, cancel);
            auto zeroTargets = zero.hits.replacementTargets->Page(0, 2);
            Require(zeroTargets[0].utf8Start == 0 && zeroTargets[0].utf8End == 1 && zeroTargets[1].utf8Start == 2 && zeroTargets[1].utf8End == 3);
            auto adjacentZero = files::ReplaceContents({"foo"}, {"foo|$", "bar", regex, true}, cancel);
            auto adjacentZeroTargets = adjacentZero.hits.replacementTargets->Page(0, 2);
            Require(adjacentZeroTargets.size() == 2 && adjacentZeroTargets[0].utf8Start == 0 && adjacentZeroTargets[0].utf8End == 3 && adjacentZeroTargets[1].utf8Start == 3 && adjacentZeroTargets[1].utf8End == 6);
            auto unreplaced = files::ReplaceContents({"ab"}, {"a|(?<=x)(?=b)|b", "x", regex, true}, cancel);
            auto unreplacedTargets = unreplaced.hits.replacementTargets->Page(0, 2);
            auto expectedUnreplaced = editor.Replace("ab", {"a|(?<=x)(?=b)|b", "x", regex, true});
            Require(unreplaced.text.utf8 == expectedUnreplaced.first && unreplaced.replacements == expectedUnreplaced.second);
            Require(unreplacedTargets.size() == 2 && unreplacedTargets[1].utf8Start == 1 && unreplacedTargets[1].utf8End == 2);
            auto evolving = files::ReplaceContents({"aaa"}, {"(?<!b)a", "b", regex, true}, cancel);
            Require(evolving.hits.archive->Count() == 3 && evolving.hits.replacementTargets->Count() == 3);
            auto expectedEvolving = editor.Replace("aaa", {"(?<!b)a", "b", regex, true});
            Require(evolving.text.utf8 == expectedEvolving.first && evolving.replacements == expectedEvolving.second);
        }
        bool regexFailed = false;
        try { files::ReplaceContents({"x\n" + std::string(100, 'a') + "!"}, {"x|(?:a+)+$", "_", regex, true}, cancel); }
        catch (const std::runtime_error&) { regexFailed = true; }
        Require(regexFailed);
        auto dir = std::filesystem::temp_directory_path() / (L"multipad-replace-" + std::to_wstring(GetCurrentProcessId())); std::filesystem::create_directories(dir);
        auto detectionPath = dir / L"detection.txt";
        for (auto bytes : std::vector<std::string>{
            std::string(17*1024*1024, char(0x80)),
            "\xef\xbb\xbf" + std::string(17*1024*1024, char(0x80)),
            "plain ascii", "coding: utf-7\n+Ti1lhw- alpha", "\xef\xbb\xbf" + std::string(2*1024*1024, 'a') + "\xff",
            std::string(2*1024*1024, 'a') + "\xff", "coding: utf-8\n" + std::string(2*1024*1024, 'a') + "\xff",
            "coding: windows-1252\n" + std::string(2*1024*1024, 'a') + "\x80",
            std::string(2*1024*1024, 'a') + "\ncoding: windows-1252\n\x80",
            files::Encode({"中文 alpha", files::Encoding::Utf16LE}),
            files::Encode({"中文 alpha", files::Encoding::Utf16BEPlain}),
            files::Encode({"alpha", files::Encoding::Utf16LE}) + "a"
        }) {
            files::WriteAtomicChunks(detectionPath, [&](const files::ChunkConsumer& consume) { consume(bytes); });
            files::Text expected, actual; bool expectedError = false, actualError = false;
            try { expected = files::Read(detectionPath); } catch (...) { expectedError = true; }
            try { actual = files::ReadReplacement(detectionPath, cancel); } catch (...) { actualError = true; }
            if (expectedError != actualError || (!expectedError && (expected.encoding != actual.encoding || expected.utf8 != actual.utf8))) {
                fprintf(stderr, "Read parity failed sample bytes=%zu first=%.24s expectedEncoding=%u actualEncoding=%u expectedError=%d actualError=%d\n", bytes.size(), bytes.c_str(), unsigned(expected.encoding), unsigned(actual.encoding), expectedError, actualError); return 3;
            }
        }
        {
            std::atomic<bool> stopped{true}; files::ReadOptions options; options.cancel = &stopped;
            bool cancelled = false;
            try { files::DetectEncoding(std::string(1024, 'a'), options); }
            catch (const std::exception& error) { cancelled = std::string_view(error.what()) == "File read cancelled."; }
            Require(cancelled);
        }
        auto path = dir / L"test.txt"; files::WriteAtomic(path, {"before"}); auto version = files::ReplacementVersion(path);
        files::CommitReplacement(path, {"after"}, version, cancel); Require(files::Read(path).utf8 == "after");
        bool conflict = false; try { files::CommitReplacement(path, {"lost"}, version, cancel); } catch (...) { conflict = true; } Require(conflict && files::Read(path).utf8 == "after");
        int validations = 0; conflict = false;
        try { files::WriteAtomicChunks(path, [](const files::ChunkConsumer& consume) { consume("lost"); }, [&] { if (++validations == 2) throw std::runtime_error("Injected precommit failure"); }); } catch (...) { conflict = true; }
        Require(conflict && validations == 2 && files::Read(path).utf8 == "after");
        auto alias = dir / L"alias.txt"; Require(CreateHardLinkW(alias.c_str(), path.c_str(), nullptr)); version = files::ReplacementVersion(path);
        files::CommitReplacement(path, {"linked"}, version, cancel); Require(files::Read(alias).utf8 == "linked" && files::ReplacementVersion(path).indexLow == version.indexLow);
        auto stream = std::filesystem::path(path.native() + L":test"); files::WriteAtomic(stream, {"stream"}); version = files::ReplacementVersion(stream);
        files::CommitReplacement(stream, {"updated"}, version, cancel); Require(files::Read(stream).utf8 == "updated");
        cancel = true; conflict = false; version = files::ReplacementVersion(path); try { files::CommitReplacement(path, {"lost"}, version, cancel); } catch (...) { conflict = true; } Require(conflict && files::Read(path).utf8 == "linked");
        std::filesystem::remove_all(dir); puts("Replacement parity and conditional writes passed.");
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 1; }
}

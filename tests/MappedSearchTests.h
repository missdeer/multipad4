#pragma once
#include "MainFrame.h"
#include "SearchDialog.h"
#include "SearchResultsPane.h"
#include "FileCommands.h"
#include <source_location>
#include <fstream>
#include <chrono>
#include <psapi.h>
#include "MappedPreviewBaseline.h"
inline void RunMappedSearchTests(MainFrame& frame, const std::filesystem::path& directory) {
    auto require = [](bool value, std::source_location where = std::source_location::current()) { if (!value) throw std::runtime_error("Mapped search regression at line " + std::to_string(where.line())); };
    auto* document = frame.NewDocument(); require(document != nullptr);
    auto path = directory / L"mapped-search.txt";
    auto load = [&](const std::string& text, files::Encoding encoding = files::Encoding::Utf8) {
        files::WriteAtomic(path, {text, encoding}); document->path = path;
        document->OpenFastFile(path, {}, encoding);
        auto* child = frame.tabs.Child(frame.tabs.GetActivePage()); child->m_hWndClient = document->ViewWindow();
        SetParent(document->ViewWindow(), child->m_hWnd); ShowWindow(document->ViewWindow(), SW_SHOW); child->UpdateLayout();
        document->search = {};
    };
    auto wait = [&] {
        auto deadline = GetTickCount64() + 15000;
        while (document->fastView->Busy() && GetTickCount64() < deadline) {
            MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (document->fastView->Busy()) MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        require(!document->fastView->Busy());
    };
    auto content = [&] { large::Search reader(document->fastView->Model().Capture(), document->encoding, {L""}); return reader.Utf8(0, document->fastView->Size()); };
    // Consecutive Previous jobs retain only a matcher for the same immutable root/query.
    std::string repeated; for (int i = 0; i < 140; ++i) repeated += "aaa Alpha 12\r\n";
    for (bool regex : {false, true}) {
        load(repeated); document->search.findText = regex ? L"Alpha" : L"aa"; document->search.regex = regex;
        document->search.flags = regex ? SCFIND_MATCHCASE : 0; document->search.wrap = false;
        document->fastView->ShowSelection(document->fastView->Size(), 0);
        auto position = document->fastView->Size();
        for (int i = 0; i < 100; ++i) {
            large::Search fresh(document->fastView->Model().Capture(), document->encoding, document->MappedSearchOptions());
            auto expected = fresh.Next(position, 0, true); require(expected.has_value());
            require(document->FindNext(true)); wait();
            auto selected = document->fastView->GlobalSelection();
            require((std::min)(selected.anchor, selected.caret) == expected->start && (std::max)(selected.anchor, selected.caret) == expected->end);
            position = expected->start;
        }
        document->fastView->StartFind(L"missing", true, false, false, false); document->fastView->StopWork();
        document->fastView->ShowSelection(document->fastView->Size(), 0); require(document->FindNext(true)); wait();
        document->Send(SCI_INSERTTEXT, 0, reinterpret_cast<LPARAM>("edit"));
        document->fastView->ShowSelection(document->fastView->Size(), 0); require(document->FindNext(true)); wait();
        document->Send(SCI_UNDO);
    }
    {
        std::ofstream metrics("mapped-preview-benchmark.csv"); metrics << "encoding,variant,run,elapsed_ms,source_bytes_read\n";
        for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LE, files::Encoding::Utf16BE, files::FromCodePage(936), files::FromCodePage(54936)}) {
            std::string sample; for (int i = 0; i < 128; ++i) sample += encoding == files::FromCodePage(936) ? "中 a " : "中😀 a "; sample += "\r\nlast\n";
            load(sample, encoding); document->search.findText = L"a";
            const auto snapshot = document->fastView->Model().Capture();
            auto hits = document->fastView->Matches(document->MappedSearchOptions(), false); hits.resize(64);
            const auto expected = BaselineMappedMatches(*document, snapshot, hits);
            const auto actual = document->MappedMatches(snapshot, hits);
            require(expected.size() == actual.size());
            for (size_t i = 0; i < actual.size(); ++i) {
                require(actual[i].nativeStart == expected[i].nativeStart && actual[i].nativeEnd == expected[i].nativeEnd && actual[i].nativeLineStart == expected[i].nativeLineStart && actual[i].line == expected[i].line);
                require(actual[i].preview.substr(actual[i].start, actual[i].end - actual[i].start) == "a");
            }
            if (GetEnvironmentVariableW(L"MULTIPAD4_PREVIEW_BENCHMARK", nullptr, 0)) for (bool baseline : {true, false}) for (int run = 0; run < 5; ++run) {
                const auto before = document->fastView->Model().Statistics(); const auto start = std::chrono::steady_clock::now();
                for (int repeat = 0; repeat < 20; ++repeat) {
                    auto matches = baseline ? BaselineMappedMatches(*document, snapshot, hits) : document->MappedMatches(snapshot, hits); require(matches.size() == 64);
                }
                const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                metrics << static_cast<unsigned>(encoding) << ',' << (baseline ? "before" : "after") << ',' << run << ',' << elapsed << ',' << document->fastView->Model().Statistics().sourceBytesRead - before.sourceBytesRead << '\n';
            }
        }
    }
    load("first\nsecond\tvalue\r\nlast");
    document->search.findText = L"second\tvalue\r\n"; document->search.transformBackslashes = true;
    require(document->FindNext()); wait();
    require(document->Range(document->Send(SCI_GETSELECTIONSTART), document->Send(SCI_GETSELECTIONEND) - document->Send(SCI_GETSELECTIONSTART)) == "second\tvalue\r\n");
    document->search.replaceText = L"new\nline"; require(document->ReplaceOne()); require(content() == "first\nnew\nlinelast");
    document->Send(SCI_UNDO); require(!document->Modified());
    document->search.findText = L"\n"; document->search.replaceText = L"\r\n";
    require(document->ReplaceAll() == 2); require(content() == "first\r\nsecond\tvalue\r\r\nlast");
    document->Send(SCI_UNDO); require(content() == "first\nsecond\tvalue\r\nlast");
    load("Alpha 12\r\nalpha 34\nlast");
    document->search.regex = true; document->search.useBoostRegex = true; document->search.findText = LR"((alpha) ([0-9]+))";
    document->search.replaceText = LR"($2-\1)";
    require(document->FindNext()); wait(); require(document->ReplaceOne()); require(content().starts_with("12-Alpha"));
    document->Send(SCI_UNDO); require(!document->Modified());
    require(document->ReplaceAll() == 2); require(content() == "12-Alpha\r\n34-alpha\nlast");
    document->Send(SCI_UNDO); require(content() == "Alpha 12\r\nalpha 34\nlast");
    document->search.findText = L"^alpha"; document->fastView->ShowSelection(1, 0);
    require(document->FindNext()); wait(); require(document->fastView->GlobalSelection().anchor == 10);
    document->fastView->ShowSelection(document->fastView->Size(), 0); require(document->FindNext(true)); wait(); require(document->fastView->GlobalSelection().anchor == 10);
    document->search.findText = LR"(Alpha.*last)"; document->search.dotMatchesNewline = true; require(document->ReplaceAll() == 1);
    document->Send(SCI_UNDO);
    // Validate before changing any document, and make the entire batch undoable.
    document->search.findText = L"["; auto before = content(); bool rejected = false;
    try { document->ReplaceAll(); } catch (const std::runtime_error&) { rejected = true; } require(rejected && content() == before);
    load("ab"); document->search.regex = true; document->search.findText = L"(?=.)"; document->search.replaceText = L"_";
    require(document->ReplaceAll() == 2 && content() == "_a_b"); document->Send(SCI_UNDO); require(content() == "ab");
    document->search.findText = L"$"; document->search.wrap = false; document->fastView->ShowSelection(0, 0);
    require(document->FindNext()); wait(); require(document->fastView->Caret() == 2);
    require(document->FindNext()); wait(); require(document->fastView->Status() == "No further match found.");
    require(document->FindNext()); wait(); require(document->fastView->Status() == "No further match found.");
    load("word WORD sword word"); document->search.findText = L"word"; document->search.flags = SCFIND_WHOLEWORD; document->search.replaceText = L"X";
    require(document->ReplaceAll() == 3 && content() == "X X sword X"); document->Send(SCI_UNDO);
    document->fastView->ShowSelection(5, 4); require(document->ReplaceAll(true) == 1 && content() == "word X sword word"); document->Send(SCI_UNDO);
    auto collected = document->CollectMatches(); require(collected.size() == 3 && collected[0].mappedSource && !collected[0].source);
    frame.searchResults.Begin(L"word"); frame.searchResults.Add(*document, collected); frame.searchResults.Finish(collected.size(), 1);
    auto resultLine = frame.searchResults.targets.size() - 1;
    require(frame.searchResults.Navigate(resultLine));
    require(document->FindAll(false, true) == 3);
    // UTF-16 native byte positions, cross-I/O-block regex and edited pieces.
    for (auto encoding : {files::Encoding::Utf8Bom, files::Encoding::Utf16LE, files::Encoding::Utf16BE}) {
        load("中文 one\r\n中文 two", encoding); document->search.findText = L"中文 (one|two)"; document->search.regex = true; document->search.replaceText = L"$1";
        require(document->ReplaceAll() == 2 && content() == "one\r\ntwo"); document->Send(SCI_UNDO); require(content() == "中文 one\r\n中文 two");
    }
    load(std::string(65534, 'x') + "BEGIN\r\nEND last"); document->search.regex = true; document->search.findText = LR"(BEGIN\r\nEND)"; document->search.replaceText = L"done";
    require(document->ReplaceAll() == 1 && content() == std::string(65534, 'x') + "done last"); document->Send(SCI_UNDO);
    document->fastView->ShowSelection(65536, 0); document->Send(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("NEW"));
    document->search.findText = L"BENEWGIN"; document->search.regex = false; document->search.replaceText = L"joined";
    require(document->ReplaceAll() == 1 && content() == std::string(65534, 'x') + "joined\r\nEND last");
    document->Send(SCI_UNDO); document->Send(SCI_UNDO); require(!document->Modified());
    // Clipboard replacement is literal, even when the query is regex.
    load("one one"); require(OpenClipboard(frame.m_hWnd) != FALSE); EmptyClipboard();
    auto clipboard = GlobalAlloc(GMEM_MOVEABLE, 16 * sizeof(wchar_t)); require(clipboard != nullptr);
    auto* value = static_cast<wchar_t*>(GlobalLock(clipboard)); wcscpy_s(value, 16, L"$1\n"); GlobalUnlock(clipboard);
    require(SetClipboardData(CF_UNICODETEXT, clipboard) != nullptr); CloseClipboard();
    document->search.findText = L"one"; document->search.regex = true; document->search.replaceText = L"^c";
    require(document->ReplaceAll() == 2 && content() == "$1\n $1\n"); document->Send(SCI_UNDO);
    // Cancel a worker before commit; its branch must never modify live text.
    document->search.findText = L"one"; document->search.regex = false; document->search.replaceText = L"changed";
    document->fastView->StartReplaceEvery(document->MappedSearchOptions(), document->SearchReplacement(), false, false);
    document->fastView->StopWork(); require(content() == "one one");
    document->fastView->StartReplaceEvery(document->MappedSearchOptions(), document->SearchReplacement(), false, false); wait();
    require(content() == "changed changed"); document->Send(SCI_UNDO); require(!document->Modified());
    document->fastView->SetReadOnly(true); require(document->ReplaceAll() == 0 && content() == "one one"); document->fastView->SetReadOnly(false);
    // Compare mapped matches against the existing Scintilla implementation.
    {
        const auto sample = std::string("xa aab\r\nb\ra\nb ") + "中😀";
        load(sample);
        EditorDocument ordinary; require(ordinary.Create(frame.tabs)); ordinary.Load({sample});
        for (auto pattern : {L"a", L"a+", L".", L"(?=.)", L"^", L"$", L"\\b", L"(?=a)", L"(?<=x)a", L"(a)?b", L"a.*b", L"a\\r?\\nb", L"中|😀"}) {
            for (bool dot : {false, true}) {
                document->search = {}; document->search.findText = pattern; document->search.regex = true; document->search.useBoostRegex = true; document->search.dotMatchesNewline = dot;
                ordinary.search = document->search;
                auto expected = ordinary.CollectMatches(); auto actual = document->fastView->Matches(document->MappedSearchOptions(), false);
                if (expected.size() != actual.size()) {
                    std::string detail = "Mapped regex differs from Scintilla: " + files::ToUtf8(pattern) + " dotAll=" + std::to_string(dot) + " expected=";
                    for (const auto& hit : expected) detail += std::to_string(hit.start) + ":" + std::to_string(hit.end) + ",";
                    detail += " actual="; for (const auto& hit : actual) detail += std::to_string(hit.start) + ":" + std::to_string(hit.end) + ",";
                    throw std::runtime_error(detail);
                }
                for (size_t i = 0; i < actual.size(); ++i) {
                    require(actual[i].start == static_cast<large::Position>(expected[i].start));
                    require(actual[i].end == static_cast<large::Position>(expected[i].end));
                }
            }
        }
    }
    // Match and replacement semantics agree with the ordinary editor.
    for (const auto& item : std::vector<std::pair<std::wstring, std::wstring>>{
        {L"(a)?b", LR"($1-\0-$$-$&)"}, {L"(?=a)", L"_"}, {L"a+", LR"(\t$0\x41\u0042)"}, {L"\\b", L"_"}}) {
        const std::string sample = "ab b aaa\r\nend";
        load(sample); document->search.regex = true; document->search.useBoostRegex = true;
        document->search.findText = item.first; document->search.replaceText = item.second;
        EditorDocument ordinary; require(ordinary.Create(frame.tabs)); ordinary.Load({sample}); ordinary.search = document->search;
        const auto expected = ordinary.ReplaceAll(); const auto actual = document->ReplaceAll();
        require(actual == expected && content() == ordinary.Text());
    }
    load("AbAbA"); document->search.findText = L"aba"; document->search.wrap = false;
    document->fastView->ShowSelection(5, 0); require(document->FindNext(true)); wait(); require(document->fastView->GlobalSelection().anchor == 2);
    load(std::string("a\0b\0c", 5)); document->search.findText = LR"(\0)"; document->search.transformBackslashes = true; document->search.replaceText = L"_";
    require(document->ReplaceAll() == 2 && content() == "a_b_c"); document->Send(SCI_UNDO);
    // DBCS trail bytes must never be interpreted as standalone ASCII matches.
    if (IsValidCodePage(932)) {
        load("ソ\\ ソ\\", files::FromCodePage(932));
        document->search.findText = L"\\"; document->search.replaceText = L"!"; document->search.flags = SCFIND_MATCHCASE;
        auto hits = document->fastView->Matches(document->MappedSearchOptions(), false);
        require(hits.size() == 2 && hits[0].start == 2 && hits[1].start == 6);
        require(document->ReplaceAll() == 2 && content() == "ソ! ソ!"); document->Send(SCI_UNDO);
        document->search.regex = true; document->search.findText = L"ソ(.)"; document->search.replaceText = L"$1";
        require(document->ReplaceAll() == 2 && content() == "\\ \\"); document->Send(SCI_UNDO);
    }
    for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain, files::FromCodePage(54936)}) {
        if (!IsValidCodePage(files::CodePage(encoding))) continue;
        load("中😀 one 中😀 two", encoding);
        document->search.regex = true; document->search.findText = L"(中😀) (one|two)"; document->search.replaceText = L"$2:$1";
        require(document->ReplaceAll() == 2 && content() == "one:中😀 two:中😀"); document->Send(SCI_UNDO);
    }
    // Viewport coordinates survive supplementary characters, edits and retained mark groups.
    std::vector<files::Encoding> coordinateEncodings{files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain};
    for (auto page : {932u, 54936u}) if (IsValidCodePage(page)) coordinateEncodings.push_back(files::FromCodePage(page));
    for (auto encoding : coordinateEncodings) {
        const std::string character = files::CodePage(encoding) == 932 ? "ソ" : "中😀";
        const auto nativeCharacter = files::Encode({character, encoding}).size();
        load(character + " one\n" + character + " two", encoding);
        auto& view = *document->fastView;
        view.ShowSelection(0, nativeCharacter);
        require(document->Range(0, document->Send(SCI_GETSELECTIONEND)) == character);
        require(view.GlobalSelection().anchor == 0 && view.GlobalSelection().caret == nativeCharacter);
        document->Send(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("X"));
        require(content() == "X one\n" + character + " two");
        document->Send(SCI_UNDO); require(content() == character + " one\n" + character + " two");
        require(view.GlobalSelection().anchor == 0 && view.GlobalSelection().caret == nativeCharacter);
        document->Send(SCI_REDO); require(content() == "X one\n" + character + " two");
        document->Send(SCI_UNDO);
        auto snapshot = view.Model().Capture();
        const auto second = files::Encode({character + " one\n", encoding}).size();
        view.SetMarks(snapshot, std::vector<large::SearchHit>{{second, view.Size(), {}}}, true);
        auto earlier = std::make_shared<large::HitArchive>(); earlier->Append({0, nativeCharacter, {}});
        view.SetMarks(snapshot, earlier, true, false);
        require(document->Send(SCI_INDICATORVALUEAT, 12, 0) != 0);
        require(document->Send(SCI_INDICATORVALUEAT, 12, character.size() + 5) != 0);
        require(document->Send(SCI_MARKERGET, 0) & 1); require(document->Send(SCI_MARKERGET, 1) & 1);
        // Both viewport edges clip separately in each retained group after seeking.
        std::string manyLines; for (int i = 0; i < 120000; ++i) manyLines += character + "\n";
        load(manyLines, encoding); auto& largeView = *document->fastView;
        snapshot = largeView.Model().Capture(); largeView.Seek(largeView.Size() / 2);
        require(largeView.Begin() > 0 && largeView.End() < largeView.Size());
        largeView.SetMarks(snapshot, std::vector<large::SearchHit>{{0, largeView.Begin() + nativeCharacter, {}}}, true);
        auto repeated = std::make_shared<large::HitArchive>(); repeated->Append({largeView.End() - files::Encode({"\n", encoding}).size(), largeView.Size(), {}});
        const auto readBeforePaint = largeView.BytesRead();
        largeView.SetMarks(snapshot, repeated, true, false);
        require(largeView.BytesRead() == readBeforePaint);
        require(document->Send(SCI_INDICATORVALUEAT, 12, 0) != 0);
        require(document->Send(SCI_INDICATORVALUEAT, 12, document->Send(SCI_GETLENGTH) / 2) == 0);
        require(document->Send(SCI_INDICATORVALUEAT, 12, document->Send(SCI_GETLENGTH) - 1) != 0);
        require(document->Send(SCI_MARKERGET, 0) & 1);
        // The global selection extends beyond the visible text; replacement stays atomic.
        largeView.Seek(0);
        const auto selected = largeView.End() + files::Encode({character + "\n", encoding}).size();
        const auto selectedUtf8 = largeView.ReadUtf8(0, static_cast<size_t>(selected)).size();
        largeView.ShowSelection(0, selected);
        require(largeView.GlobalSelection().anchor == 0 && largeView.GlobalSelection().caret == selected);
        document->Send(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("X"));
        require(content() == "X" + manyLines.substr(selectedUtf8));
        document->Send(SCI_UNDO); require(content() == manyLines);
        require(largeView.GlobalSelection().anchor == 0 && largeView.GlobalSelection().caret == selected);
        document->Send(SCI_REDO); require(content() == "X" + manyLines.substr(selectedUtf8));
        document->Send(SCI_UNDO); require(content() == manyLines);
    }
    load("中 word", files::Encoding::Utf16LEPlain);
    {
        auto& view = *document->fastView;
        SCNotification invalid{}; invalid.modificationType = SC_MOD_INSERTTEXT; invalid.position = 1; invalid.length = 1; invalid.text = "X";
        bool rejected = false; try { view.OnModified(invalid); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && content() == "中 word");
    }
    // Less common installed multibyte encodings keep their original conversion path.
    for (auto page : {20932u, 51949u}) {
        if (!IsValidCodePage(page)) continue;
        const auto encoding = files::FromCodePage(page);
        const std::string sample = page == 20932 ? "日本 word" : "한국 word";
        load(sample, encoding); auto& view = *document->fastView;
        const auto native = files::Encode({sample, encoding}).size();
        view.ShowSelection(0, native); require(view.GlobalSelection().caret == native);
        require(document->Range(0, document->Send(SCI_GETLENGTH)) == sample);
        document->Send(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("X")); require(content() == "X");
        document->Send(SCI_UNDO); require(content() == sample);
    }
    // Paired warm-cache viewport paint workload; baseline is the previous prefix decoder.
    if (GetEnvironmentVariableW(L"MULTIPAD4_VIEWPORT_BENCHMARK", nullptr, 0)) {
        std::string text; for (int i = 0; i < 4096; ++i) text += "中😀 hit\n";
        load(text, files::Encoding::Utf16LEPlain);
        auto& view = *document->fastView;
        std::vector<large::SearchHit> hits;
        for (size_t i = 0; i < 4096; ++i) hits.push_back({i * 16 + 8, i * 16 + 14, {}});
        auto archive = std::make_shared<large::HitArchive>(); for (const auto& hit : hits) archive->Append(hit);
        const auto snapshot = view.Model().Capture();
        std::vector<double> before, after;
        auto measure = [&](bool baseline) {
            const auto read = view.BytesRead();
            const auto started = std::chrono::steady_clock::now();
            if (baseline) {
                document->Send(SCI_SETINDICATORCURRENT, 12); document->Send(SCI_INDICATORCLEARRANGE, 0, text.size());
                for (const auto& hit : hits) {
                    const auto first = files::Decode(view.Model().Read(0, static_cast<size_t>(hit.start)), document->encoding, false).utf8.size();
                    const auto last = files::Decode(view.Model().Read(0, static_cast<size_t>(hit.end)), document->encoding, false).utf8.size();
                    document->Send(SCI_INDICATORFILLRANGE, first, last - first);
                }
            } else view.SetMarks(snapshot, archive, false);
            document->Send(SCI_GETLENGTH); UpdateWindow(document->hwnd);
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (!baseline) require(view.BytesRead() == read);
            require(document->Send(SCI_INDICATORVALUEAT, 12, 8) != 0);
            return ms;
        };
        for (int i = 0; i < 21; ++i) { before.push_back(measure(true)); after.push_back(measure(false)); }
        std::sort(before.begin(), before.end()); std::sort(after.begin(), after.end());
        fprintf(stdout, "Viewport UTF16LE 64KiB/4096 hits warm: prefix vs checkpoints ms median %.3f/%.3f p95 %.3f/%.3f p99/max %.3f/%.3f; decoded bytes per repaint 268460032/0.\n",
            before[10], after[10], before[19], after[19], before[20], after[20]);
        std::vector<double> typing, seeking;
        for (int i = 0; i < 21; ++i) {
            view.ShowSelection(0, 0);
            auto started = std::chrono::steady_clock::now();
            document->Send(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("X"));
            require(view.GlobalSelection().caret == 2); UpdateWindow(document->hwnd);
            typing.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
            document->Send(SCI_UNDO);
            const auto read = view.BytesRead(); started = std::chrono::steady_clock::now();
            view.Seek(0); UpdateWindow(document->hwnd);
            seeking.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
            require(view.BytesRead() - read == 65536);
        }
        std::sort(typing.begin(), typing.end()); std::sort(seeking.begin(), seeking.end());
        fprintf(stdout, "Viewport new typing ms median %.3f p95 %.3f p99/max %.3f; seek/decode ms median %.3f p95 %.3f p99/max %.3f; native bytes decoded per seek 65536, typed prefix 0.\n",
            typing[10], typing[19], typing[20], seeking[10], seeking[19], seeking[20]);
    }
    load(std::string(1024 * 1024 - 5, 'A') + "nEeDlE_mArKeR" + std::string(1024 * 1024, 'B') + "NEEDLE_MARKER");
    document->search.findText = L"needle_marker"; document->search.replaceText = L"found";
    require(document->ReplaceAll() == 2 && content() == std::string(1024 * 1024 - 5, 'A') + "found" + std::string(1024 * 1024, 'B') + "found"); document->Send(SCI_UNDO);
    // Cancellation after a worker has already written edits must discard its branch.
    load(std::string(50000, 'a')); document->search.findText = L"a";
    const auto beforeCancel = document->fastView->Model().Capture();
    document->fastView->StartReplaceEvery(document->MappedSearchOptions(), "b", false, false);
    auto cancelDeadline = GetTickCount64() + 5000;
    while (document->fastView->Model().Statistics().addedBytes < 100 && GetTickCount64() < cancelDeadline) SwitchToThread();
    require(document->fastView->Model().Statistics().addedBytes >= 100);
    document->fastView->StopWork(); require(document->fastView->Model().Capture().root == beforeCancel.root && !document->Modified());
    // A match spanning more than the view must be selected/replaced globally.
    load("BEGIN" + std::string(300 * 1024, 'x') + "END");
    document->search.regex = true; document->search.findText = LR"(BEGIN.*END)"; document->search.flags = SCFIND_MATCHCASE; document->search.replaceText = L"done";
    require(document->FindNext()); wait();
    require(document->fastView->GlobalSelection().anchor == 0 && document->fastView->GlobalSelection().caret == document->fastView->Size());
    require(document->ReplaceOne() && content() == "done"); document->Send(SCI_UNDO); require(!document->Modified());
    // All-result offsets spool to disk, and the visible result pane is paged.
    std::string many; for (int i = 0; i < 2205; ++i) many += "hit\n";
    load(many); document->search.findText = L"hit"; document->search.flags = SCFIND_MATCHCASE;
    auto archive = document->fastView->ArchiveMatches(document->MappedSearchOptions(), false); require(archive->Count() == 2205);
    require(archive->Page(2000, 2)[0].start == 8000 && archive->FirstEndingAtOrAfter(8001) == 2000);
    frame.searchResults.Clear(); frame.searchResults.Begin(L"hit");
    frame.searchResults.AddMapped(*document, document->fastView->Model().Capture(), archive); frame.searchResults.Finish(archive->Count(), 1);
    require(frame.searchResults.targets.size() < 70); require(frame.searchResults.Navigate(frame.searchResults.targets.size() - 1));
    require(frame.searchResults.targets.size() > 128 && frame.searchResults.targets.size() < 140);
    require(frame.searchResults.Navigate(frame.searchResults.targets.size() - 1));
    require(document->FindAll(false, true) == 2205);
    load("one two");
    auto snap = document->fastView->Model().Capture();
    document->fastView->SetMarks(snap, std::vector<large::SearchHit>{{0, 3, {}}}, false);
    auto additional = std::make_shared<large::HitArchive>(); additional->Append({4, 7, {}});
    document->fastView->SetMarks(snap, additional, false, false);
    require(document->Send(SCI_INDICATORVALUEAT, 12, 0) != 0 && document->Send(SCI_INDICATORVALUEAT, 12, 4) != 0);
    document->fastView->SetMarks(snap, additional, false, true);
    require(document->Send(SCI_INDICATORVALUEAT, 12, 0) == 0 && document->Send(SCI_INDICATORVALUEAT, 12, 4) != 0);
    // Actual large-document UI entry points run against edited snapshots.
    load(std::string(66 * 1024 * 1024, 'x'));
    const auto boundary = large::Position(64 * 1024 * 1024 + 256 * 1024);
    document->fastView->Model().Replace(boundary - 4, 12, "NeedleMarker");
    document->fastView->Model().Replace(2 * 1024 * 1024, 12, "NeedleMarker");
    document->fastView->Seek(0); document->search.findText = L"needlemarker"; document->search.wrap = false;
    require(document->FindNext()); wait(); require(document->fastView->GlobalSelection().anchor == 2 * 1024 * 1024);
    require(document->FindNext()); wait(); require(document->fastView->GlobalSelection().anchor == boundary - 4);
    document->fastView->ShowSelection(document->fastView->Size(), 0);
    require(document->FindNext(true)); wait(); require(document->fastView->GlobalSelection().anchor == boundary - 4);
    const auto beforeParallelReplace = document->fastView->Model().Capture();
    document->search.replaceText = L"done";
    document->fastView->StartReplaceEvery(document->MappedSearchOptions(), "done", false, false); wait();
    require(document->search.matches == 2); document->Send(SCI_UNDO);
    require(document->fastView->Model().Capture().root == beforeParallelReplace.root);
    auto previousRead = document->fastView->BytesRead();
    document->search.findText = L"not-in-file"; require(document->FindNext());
    auto deadline = GetTickCount64() + 5000;
    while (document->fastView->BytesRead() == previousRead && GetTickCount64() < deadline) SwitchToThread();
    document->fastView->StopWork(); require(document->fastView->Model().Capture().root == beforeParallelReplace.root);
    // Exercise the actual modeless dialog commands.
    load("one one"); frame.searchDialog.Open(frame, *document, true);
    SetDlgItemTextW(frame.searchDialog.hwnd, IDC_FINDTEXT, L"one"); SetDlgItemTextW(frame.searchDialog.hwnd, IDC_REPLACETEXT, L"two");
    CheckRadioButton(frame.searchDialog.hwnd, IDC_SEARCH_NORMAL, IDC_SEARCH_REGEX, IDC_SEARCH_NORMAL);
    CheckDlgButton(frame.searchDialog.hwnd, IDC_FINDCASE, BST_CHECKED); CheckDlgButton(frame.searchDialog.hwnd, IDC_SEARCH_SELECTION, BST_UNCHECKED);
    frame.searchDialog.Execute(IDC_REPLACEALL); wait(); require(content() == "two two"); document->Send(SCI_UNDO); require(!document->Modified());
    frame.searchDialog.Execute(IDC_SEARCH_COUNT); wait(); require(document->search.matches == 2);
    frame.searchDialog.Execute(IDC_FINDALL); wait(); require(document->search.matches == 2);
    frame.searchDialog.Execute(IDC_SEARCH_MARK); wait(); require(document->search.matches == 2);
    frame.searchDialog.Execute(IDC_SEARCH_CLEAR_MARK);
    // Cross-document commands preserve ownership and each document's undo.
    auto* other = frame.NewDocument(); require(other != nullptr); other->Load({"one other one"});
    frame.tabs.SetActivePage(frame.tabs.GetPageCount() - 2); frame.searchDialog.SyncDocument();
    frame.searchDialog.Execute(IDC_SEARCH_ALL_OPEN); require(document->search.matches == 2 || !document->Modified());
    document->search.findText = L"one"; document->search.replaceText = L"two";
    for (auto* item : {document, other}) { item->search = document->search; item->ValidateSearch(true); }
    require(document->ReplaceAll() == 2 && other->ReplaceAll() == 2);
    require(content() == "two two" && other->Text() == "two other two");
    document->Send(SCI_UNDO); other->Send(SCI_UNDO); require(!document->Modified() && !other->Modified());
    frame.searchDialog.Close(); require(frame.Close(frame.tabs.GetPageCount() - 1));
    frame.searchResults.Clear();
    document->search = {}; document->Send(SCI_SETSAVEPOINT); require(frame.Close(frame.tabs.GetActivePage()));
}

// Opt-in real-file benchmark: all scans include the 64-bit native offset tail.
inline void RunMappedSearchPerformance(MainFrame& frame, const std::filesystem::path& path) {
    auto check = [](bool value) { if (!value) throw std::runtime_error("Mapped search benchmark verification failed."); };
    frame.ShowWindow(SW_SHOWNORMAL); frame.UpdateWindow(); check(frame.Open(path));
    auto* document = frame.Active(); check(document && document->FastView() && document->encoding == files::Encoding::Utf8);
    auto& view = *document->fastView; const auto sourceSize = view.Size();
    const std::string marker = "\nMultiPad4_Unique_Search_731A9C42\n";
    view.Model().Replace(sourceSize, 0, marker); const auto original = view.Model().Capture(); view.Seek(0);
    large::SearchOptions exact{L"MultiPad4_Unique_Search_731A9C42", false, true};
    large::SearchOptions insensitive{L"multipad4_unique_search_731a9c42", false, false};
    auto wait = [&] {
        auto deadline = GetTickCount64() + 120000;
        while (view.Busy() && GetTickCount64() < deadline) {
            MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (view.Busy()) MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        check(!view.Busy());
    };
    auto measure = [&](int run, const char* name, const auto& action) {
        const auto before = view.Model().Statistics(); const auto start = std::chrono::steady_clock::now();
        action(); const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto after = view.Model().Statistics(); PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE);
        if (run >= 0) printf("mapped-search,%d,%s,%.3f,%llu,%llu,%zu,%zu\n", run, name, ms, after.sourceBytesRead - before.sourceBytesRead, after.addedBytes - before.addedBytes, memory.PrivateUsage / 1024, memory.PeakWorkingSetSize / 1024);
        check(document->Send(SCI_GETLENGTH) < 512 * 1024); fflush(stdout);
    };
    printf("sourceBytes=%llu; warmup=1; measured=3; cached filesystem; inserted unique tail marker; columns=run,operation,ms,sourceReadBytes,addedBytes,privateKiB,peakWorkingKiB\n", sourceSize);
    wchar_t compareFlag[8]{};
    const bool compare = GetEnvironmentVariableW(L"MULTIPAD4_LARGE_TEST_SEARCH_COMPARE", compareFlag, 8) != 0;
    for (int run = -1; run < 3; ++run) {
      for (unsigned config = 0; config < (compare ? 2u : 1u); ++config) {
        const bool serial = compare && config == (run % 2 == 0 ? 0u : 1u);
        exact.workers = insensitive.workers = serial ? 1u : 0u;
        const std::string suffix = serial ? "-serial" : "-parallel";
        view.ShowSelection(0, 0);
        measure(run, ("find-exact-to-tail" + suffix).c_str(), [&] { view.StartSearch(exact, false, false); wait(); check(view.GlobalSelection().anchor == sourceSize + 1); });
        measure(run, ("count-ignore-case" + suffix).c_str(), [&] {
            size_t count = 0; view.StartEnumerate(insensitive, false, [&](const auto&, const auto& hits) { count = hits->Count(); }, false); wait(); check(count == 1);
        });
        measure(run, ("replace-all-exact" + suffix).c_str(), [&] {
            view.StartReplaceEvery(exact, "replaced", false, false); wait();
            check(document->search.matches == 1 && view.ReadUtf8(sourceSize, 10) == "\nreplaced\n");
        });
        view.Undo(); check(view.Model().Capture().root == original.root);
      }
    }
    // A nearby match must not turn into a whole-range parallel scan.
    view.Model().Replace(0, 0, marker);
    for (int run = 0; run < 3; ++run) {
        view.ShowSelection(0, 0); exact.workers = 0;
        measure(run, "find-near-head-parallel", [&] { view.StartSearch(exact, false, false); wait(); check(view.GlobalSelection().anchor == 1); });
    }
    view.Undo(); check(view.Model().Capture().root == original.root);
    large::Search regex(view.Model().Capture(), document->encoding, {L"(MultiPad4)_Unique_Search_([0-9A-F]+)", true, true});
    auto hit = regex.Next(sourceSize > 65536 ? sourceSize - 65536 : 0, view.Size());
    check(hit && hit->start == sourceSize + 1 && regex.Substitute(*hit, "$2:$1", true) == "731A9C42:MultiPad4");
    // Cancellation and undo must preserve the original file and complete model.
    view.StartReplaceEvery(exact, "discarded", false, false); view.StopWork(); check(view.Model().Capture().root == original.root);
    view.Undo(); check(!document->Modified() && view.Size() == sourceSize); check(frame.Close(frame.tabs.GetActivePage()));
}

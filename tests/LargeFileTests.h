#pragma once
#include "MainFrame.h"
#include "AutoSave.h"
#include "QAArtifacts.h"
#include <fstream>
#include <cstdio>
#include <source_location>
#include <psapi.h>
inline void RunLargeFileTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("Large file integration check failed at line " + std::to_string(where.line())); };
    wchar_t benchmarkPath[32768]{};
    if (GetEnvironmentVariableW(L"MULTIPAD4_LARGE_TEST_SOURCE", benchmarkPath, 32768)) {
        frame.ShowWindow(SW_SHOWNORMAL); frame.UpdateWindow();
        auto start = GetTickCount64(); require(frame.Open(benchmarkPath));
        RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        auto elapsed = GetTickCount64() - start;
        auto* document = frame.Active();
        require(document && document->FastView());
        auto& view = *document->fastView;
        const auto readAtOpen = view.BytesRead();
        require(readAtOpen < 512 * 1024 && document->Send(SCI_GETLENGTH) <= 512 * 1024);
        require(view.Size() == std::filesystem::file_size(benchmarkPath));
        PROCESS_MEMORY_COUNTERS memory{sizeof(memory)}; GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
        fprintf(stdout, "%llu bytes: Open plus first paint %llu ms; source bytes requested %llu; peak working set %zu MiB.\n", view.Size(), elapsed, readAtOpen, memory.PeakWorkingSetSize / (1024 * 1024));
        for (auto offset : {view.Size() / 4, view.Size() / 2, view.Size() - 1, view.Size()}) {
            view.Seek(offset, offset == view.Size());
            require(view.Begin() <= offset && view.End() <= view.Size() && view.End() > view.Begin());
            require(document->Send(SCI_GETLENGTH) <= 512 * 1024);
        }
        require(view.End() == view.Size());
        require(document->Send(SCI_GETCURRENTPOS) == document->Send(SCI_GETLENGTH));
        require(view.BytesRead() < 2 * 1024 * 1024);
        fprintf(stdout, "Quarter/middle/end navigation passed; total bytes read %llu.\n", view.BytesRead());
        wchar_t captureDirectory[32768]{};
        if (GetEnvironmentVariableW(L"MULTIPAD4_LARGE_TEST_QA", captureDirectory, 32768)) {
            qaArtifactDirectory = captureDirectory;
            frame.ShowWindow(SW_SHOWNORMAL); frame.UpdateWindow(); frame.Refresh();
            CaptureQAWindow(frame, L"large-file");
        }
        wchar_t profile[8]{};
        if (GetEnvironmentVariableW(L"MULTIPAD4_LARGE_TEST_PROFILE", profile, 8)) {
            view.LoadForEditing();
            const auto& timing = document->loadMetrics;
            fprintf(stdout, "Full load: allocation %llu ms, read/validate/decode %llu ms, EOL/line reserve %llu ms, Scintilla insertion %llu ms, configure %llu ms.\n", timing.allocation, timing.readDecode, timing.lineScan, timing.insertion, timing.configure);
        }
        require(frame.Close(frame.tabs.GetActivePage())); return;
    }
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-large-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    try {
        auto source = directory / L"large.cpp", copy = directory / L"copy.txt";
        size_t mib = 320;
        wchar_t configured[32]{};
        if (GetEnvironmentVariableW(L"MULTIPAD4_LARGE_TEST_MIB", configured, 32)) mib = (std::max)(mib, static_cast<size_t>(_wtoi64(configured)));
        const std::string line = std::string(4092, 'a') + "\r\n";
        const size_t repeats = (mib * 1024 * 1024) / line.size();
        { std::ofstream out(source, std::ios::binary); for (size_t i = 0; i < repeats; ++i) out.write(line.data(), line.size()); out << "last"; require(bool(out)); }
        const auto start = GetTickCount64();
        require(frame.Open(source));
        auto* document = frame.Active();
        const auto elapsed = GetTickCount64() - start;
        require(document && document->FastView());
        require(document->fastView->Size() == repeats * line.size() + 4);
        require(document->fastView->BytesRead() < 512 * 1024);
        require(document->Send(SCI_GETREADONLY) && !document->Modified());
        require(document->Send(SCI_STYLEGETFORE, 0) == document->Send(SCI_STYLEGETFORE, STYLE_DEFAULT));
        require(!frame.State(ID_SAVE).enabled && !frame.State(ID_FIND).enabled && frame.State(ID_LOAD_FOR_EDITING).enabled);
        document->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!")); require(!document->Modified());
        require(frame.SaveTo(*document, copy, true));
        require(std::filesystem::file_size(copy) == std::filesystem::file_size(source));
        {
            files::MappedFile original(source); unsigned long long position = 0;
            files::ReadChunks(copy, [&](std::string_view bytes) { require(original.Read(position, bytes.size()) == bytes); position += bytes.size(); });
            require(position == original.Size());
        }
        auto firstPageEnd = document->fastView->End();
        document->Send(SCI_SETFIRSTVISIBLELINE, document->Send(SCI_GETLINECOUNT) - document->Send(SCI_LINESONSCREEN));
        SendMessageW(document->hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        require(document->fastView->Begin() == firstPageEnd);
        SendMessageW(document->ViewWindow(), WM_VSCROLL, SB_TOP, 0); require(document->fastView->Begin() == 0);
        for (int page = 0; page < 3; ++page) {
            auto previousEnd = document->fastView->End(); document->fastView->Seek(previousEnd);
            require(document->fastView->Begin() == previousEnd && document->fastView->End() > previousEnd);
        }
        SendMessageW(document->ViewWindow(), WM_VSCROLL, SB_BOTTOM, 0);
        require(document->Range(static_cast<size_t>(document->Send(SCI_GETLENGTH)) - 4, 4) == "last");
        require(frame.Reload(*document) && document->FastView());
        {
            auto broken = directory / L"bad-preview.txt";
            { std::ofstream out(broken, std::ios::binary); out << "\xEF\xBB\xBF\xFF"; }
            RECT before{}, after{}; GetClientRect(document->hwnd, &before);
            auto viewWindow = document->ViewWindow(); auto text = document->Range(0, 64); bool rejected = false;
            try { document->OpenFastFile(broken, {}); } catch (const std::exception&) { rejected = true; }
            GetClientRect(document->hwnd, &after);
            require(rejected && document->ViewWindow() == viewWindow && document->Range(0, 64) == text);
            require(EqualRect(&before, &after));
        }
        document->fastView->LoadForEditing();
        require(!document->FastView() && !document->Send(SCI_GETREADONLY));
        require((document->Send(SCI_GETDOCUMENTOPTIONS) & (SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE)) == (SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE));
        require(document->Send(SCI_GETLENGTH) == static_cast<LRESULT>(repeats * line.size() + 4));
        require(document->Send(SCI_GETLINECOUNT) == static_cast<LRESULT>(repeats + 1));
        require(document->Send(SCI_GETEOLMODE) == SC_EOL_CRLF && document->Send(SCI_GETWRAPMODE) == SC_WRAP_NONE);
        auto last = document->Send(SCI_POSITIONFROMLINE, repeats);
        require(last == static_cast<LRESULT>(repeats * line.size()));
        document->Send(SCI_GOTOPOS, last); require(document->Range(last, 4) == "last");
        require(document->Send(SCI_GETLINEENDPOSITION, repeats) == document->Send(SCI_GETLENGTH));
        require(frame.SaveTo(*document, copy, true));
        size_t offset = 0;
        files::ReadChunks(copy, [&](std::string_view bytes) {
            require(document->Range(offset, bytes.size()) == bytes); offset += bytes.size();
        });
        require(offset == repeats * line.size() + 4);
        document->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!"));
        require(document->Modified() && !SaveDocumentRecovery(frame, *document, true));
        auto previousLength = document->Send(SCI_GETLENGTH);
        { std::ofstream out(directory / L"bad.txt", std::ios::binary); out << "\xEF\xBB\xBF\xFF"; }
        bool failed = false;
        try { document->LoadFile(directory / L"bad.txt"); } catch (const std::exception&) { failed = true; }
        require(failed && document->Send(SCI_GETLENGTH) == previousLength);
        auto* other = frame.NewDocument(); require(other && !other->LargeFile());
        other->Load({"small\nfile"}); require(!other->LargeFile() && other->Text() == "small\nfile");
        for (auto encoding : {files::Encoding::Utf8Bom, files::Encoding::Utf16LE, files::Encoding::Utf16BE}) {
            const auto expected = std::string(16 * 1024 * 1024 - 1, 'x') + "\xF0\x9F\x98\x80\xEF\xBB\xBF\nend";
            other->Load({expected, encoding}); other->SaveStreaming(directory / L"unicode.txt");
            auto saved = files::Read(directory / L"unicode.txt");
            require(saved.encoding == encoding && saved.utf8 == expected);
            EditorDocument preview; preview.path = directory / L"unicode.txt"; require(preview.Create(frame.tabs));
            preview.OpenFastFile(preview.path, {}, encoding);
            require(preview.FastView() && preview.fastView->BytesRead() < 512 * 1024);
            preview.fastView->Seek(preview.fastView->Size(), true);
            require(preview.Range(static_cast<size_t>(preview.Send(SCI_GETLENGTH)) - 3, 3) == "end");
            preview.fastView->LoadForEditing(); require(!preview.FastView() && preview.Text() == expected);
        }
        // A late invalid sequence must not replace the fast viewport on upgrade.
        {
            auto broken = directory / L"late-invalid.txt";
            { std::ofstream out(broken, std::ios::binary); out << std::string(1024 * 1024, 'x') << "\xFF"; }
            EditorDocument preview; preview.path = broken; require(preview.Create(frame.tabs));
            preview.OpenFastFile(broken, {}, files::Encoding::Utf8);
            auto before = preview.Range(0, 64); bool rejected = false;
            try { preview.fastView->LoadForEditing(); } catch (const std::exception&) { rejected = true; }
            require(rejected && preview.FastView() && preview.Send(SCI_GETREADONLY) && preview.Range(0, 64) == before);
        }
        document->encodingDirty = false; document->Send(SCI_SETSAVEPOINT);
        while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1));
        PROCESS_MEMORY_COUNTERS memory{sizeof(memory)}; GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
        fprintf(stdout, "Peak working set: %zu MiB.\n", memory.PeakWorkingSetSize / (1024 * 1024));
        fprintf(stdout, "%zu MiB opened in %llu ms; length, lines, 64-bit end position, streaming copy and failure retention passed.\n", mib, elapsed);
        std::filesystem::remove_all(directory);
    } catch (...) { std::error_code error; std::filesystem::remove_all(directory, error); throw; }
}

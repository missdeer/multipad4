#pragma once
#include "../src/AutoSave.h"
#include "../src/MainFrame.h"
#include <chrono>
#include <fstream>
#include <source_location>
inline void RunPerformanceFeatureTests(MainFrame& frame) {
    auto require = [](bool value, std::source_location where = std::source_location::current()) {
        if (!value) throw std::runtime_error("Performance regression at line " + std::to_string(where.line()));
    };
    using Clock = std::chrono::steady_clock;
    auto settings = frame.settings;
    auto folder = std::filesystem::temp_directory_path() / (L"MultiPad4-performance-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    auto* d = frame.NewDocument(); require(d != nullptr);
    frame.settings = AppSettings{}; frame.settings.Initialize(true); frame.settings.path = folder / L"settings.ini";
    frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1);
    frame.settings.WriteString(L"AutoSave", L"PeriodMilliseconds", L"1");
    try {
        std::ofstream metrics("performance-checks.txt");
        d->Load({"a\t中文😀\nsecond\tline\n"}); d->Send(SCI_SETTABWIDTH, 4); d->Send(SCI_SETSEL, 1, 12);
        auto checkStats = [&] {
            auto deadline = Clock::now() + std::chrono::seconds(5);
            double longest = 0;
            bool pending;
            do {
                auto start = Clock::now(); pending = d->statusStatistics.Update(*d);
                longest = (std::max)(longest, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
                require(Clock::now() < deadline);
            } while (pending);
            auto pos = d->Send(SCI_GETCURRENTPOS), line = d->Send(SCI_LINEFROMPOSITION, pos);
            auto first = d->Send(SCI_POSITIONFROMLINE, line), last = d->Send(SCI_GETLINEENDPOSITION, line);
            require(d->statusStatistics.Column(0) == d->Send(SCI_GETCOLUMN, pos));
            require(d->statusStatistics.Column(1) == d->Send(SCI_GETCOLUMN, last));
            require(d->statusStatistics.Characters(0) == d->Send(SCI_COUNTCHARACTERS, first, pos));
            require(d->statusStatistics.Characters(1) == d->Send(SCI_COUNTCHARACTERS, first, last));
            LRESULT selected = 0;
            for (LRESULT i = 0; i < d->Send(SCI_GETSELECTIONS); ++i) selected += d->Send(SCI_COUNTCHARACTERS, d->Send(SCI_GETSELECTIONNSTART, i), d->Send(SCI_GETSELECTIONNEND, i));
            require(d->statusStatistics.SelectedCharacters() == selected);
            metrics << "longest statistics slice ms: " << longest << '\n';
        };
        checkStats();
        d->Send(SCI_SETTABWIDTH, 8); checkStats();
        d->Load({std::string(8 * 1024 * 1024, 'a') + "\t中文😀"});
        d->Send(SCI_SELECTALL); checkStats();
        d->Send(SCI_SETSEL, 0, 4); d->Send(SCI_SETMULTIPLESELECTION, TRUE); d->Send(SCI_ADDSELECTION, 16, 8); checkStats();
        auto start = Clock::now();
        for (int i = 0; i < 1000; ++i) require(d->Range(10, 4) == "aaaa");
        metrics << "1000 four-byte ranges from 8 MiB document ms: " << std::chrono::duration<double, std::milli>(Clock::now()-start).count() << '\n';
        start = Clock::now();
        for (int i = 0; i < 20; ++i) require(d->Text().substr(10, 4) == "aaaa");
        metrics << "20 legacy full-copy four-byte reads from 8 MiB document ms: " << std::chrono::duration<double, std::milli>(Clock::now()-start).count() << '\n';
        start = Clock::now();
        for (int i = 0; i < 20000; ++i) require(d->Send(SCI_GETLENGTH) > 0);
        metrics << "20000 direct length queries ms: " << std::chrono::duration<double, std::milli>(Clock::now()-start).count() << '\n';
        start = Clock::now();
        for (int i = 0; i < 20000; ++i) require(SendMessageW(d->hwnd, SCI_GETLENGTH, 0, 0) > 0);
        metrics << "20000 window-message length queries ms: " << std::chrono::duration<double, std::milli>(Clock::now()-start).count() << '\n';
        d->Send(SCI_CLEARSELECTIONS); d->Send(SCI_SETSEL, 0, 0);
        d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x"));
        QueueDocumentBackups(frame); require(frame.runtime.autoSaveTask != nullptr);
        PumpDocumentBackup(frame); // Capture one bounded chunk, then invalidate it.
        d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("y"));
        PumpDocumentBackup(frame); require(frame.runtime.autoSaveTask == nullptr);
        QueueDocumentBackups(frame); require(frame.runtime.autoSaveTask != nullptr);
        auto revision = d->style.revision;
        auto deadline = Clock::now() + std::chrono::seconds(10);
        while (frame.runtime.autoSaveTask) {
            PumpDocumentBackup(frame); require(Clock::now() < deadline); std::this_thread::yield();
        }
        require(d->runtime.lastRecoveryRevision == revision && d->Modified());
        require(files::Read(d->runtime.recoveryPath).utf8 == d->Text());
        // A foreground save drains/cancels pending work before touching the same destination.
        d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("z")); d->runtime.lastAutoSave = 0;
        QueueDocumentBackups(frame); require(frame.runtime.autoSaveTask != nullptr);
        require(frame.SaveTo(*d, folder / L"saved.txt"));
        require(!frame.runtime.autoSaveTask && !d->Modified() && files::Read(d->path).utf8 == d->Text());
        require(frame.Close(frame.tabs.GetActivePage())); d = nullptr;
        frame.settings = settings; std::filesystem::remove_all(folder);
    } catch (...) {
        if (d) { CancelDocumentBackup(frame, *d); d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; frame.Close(frame.tabs.GetActivePage()); }
        frame.settings = settings; std::error_code error; std::filesystem::remove_all(folder, error); throw;
    }
}

#pragma once
#include "AutoSaveCaptureHooks.h"
#include "AutoSaveFeatureTests.h"
#include "EditTransformTests.h"
#include <source_location>
namespace autosavecapturetest {
inline void Require(bool ok, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error("AutoSave capture regression at line " + std::to_string(at.line()));
}
inline HANDLE entered = nullptr, release = nullptr;
inline void Gate(const std::filesystem::path&, bool) {
    SetEvent(entered);
    if (WaitForSingleObject(release, 10000) != WAIT_OBJECT_0) throw std::runtime_error("AutoSave write gate timeout");
}
inline bool Pending(MainFrame& frame) { return frame.runtime.autoSaveTask || !frame.runtime.autoSaveWrites.empty(); }
inline void Drain(MainFrame& frame) {
    const auto deadline = GetTickCount64() + 10000;
    while (Pending(frame)) { PumpDocumentBackup(frame); Require(GetTickCount64() < deadline); Sleep(1); }
}
inline void StartWrite(MainFrame& frame, EditorDocument& d) {
    d.runtime.lastAutoSave = 0; QueueDocumentBackups(frame);
    const auto deadline = GetTickCount64() + 5000;
    while (frame.runtime.autoSaveWrites.empty()) { PumpDocumentBackup(frame); Require(GetTickCount64() < deadline); }
}
inline double Milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}
inline void RunAutoSaveCaptureTests(MainFrame& frame, bool benchmark) {
    using namespace autosavecapturetest;
    using namespace std::string_view_literals;
    RunAutoSaveFeatureTests(frame); // Forced suspend, fallback, write failure and retry.
    const auto saved = frame.settings;
    const auto folder = std::filesystem::temp_directory_path() / (L"MultiPad4-capture-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    frame.settings = AppSettings{}; frame.settings.Initialize(true); frame.settings.path = folder / L"settings.ini";
    frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1); frame.settings.WriteString(L"AutoSave", L"PeriodMilliseconds", L"1");
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr); release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(entered && release);
    auto close = [&](EditorDocument* d) { d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; int page = -1; for (int i = 0; i < frame.tabs.GetPageCount(); ++i) if (frame.tabs.GetPageData(i) == d) page = i; Require(page >= 0 && frame.Close(page)); };
    try {
        // Validation occurs before creation and CREATE_NEW never replaces existing bytes.
        const auto invalid = folder / L"invalid.txt";
        bool rejected = false;
        try { files::WriteNewUtf8(invalid, std::string_view("\xFF", 1), "header\n"); } catch (const std::exception&) { rejected = true; }
        Require(rejected && !std::filesystem::exists(invalid));
        files::WriteNewUtf8(invalid, "中文\0tail"sv, "header\n");
        Require(files::Read(invalid).utf8 == "header\n中文\0tail"sv);
        rejected = false; try { files::WriteNewUtf8(invalid, "replacement"); } catch (const std::exception&) { rejected = true; }
        Require(rejected && files::Read(invalid).utf8 == "header\n中文\0tail"sv);

        // A document revised every capture tick cannot starve another due tab.
        auto* hot = frame.NewDocument(); auto* stable = frame.NewDocument(); Require(hot && stable);
        hot->Load({std::string(16 * 1024 * 1024, 'h')}); stable->Load({"stable 中文"});
        hot->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x")); stable->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x"));
        hot->runtime.lastAutoSave = stable->runtime.lastAutoSave = 0;
        const auto deadline = GetTickCount64() + 5000;
        while (stable->runtime.lastRecoveryRevision != stable->style.revision) {
            QueueDocumentBackups(frame); PumpDocumentBackup(frame);
            hot->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x"));
            Require(GetTickCount64() < deadline); Sleep(1);
        }
        Require(files::Read(stable->runtime.recoveryPath).utf8 == stable->Text());
        CancelDocumentBackup(frame, *hot); close(hot); close(stable); Drain(frame);

        // An independent immutable write stays owned after close, without blocking it.
        auto* d = frame.NewDocument(); d->path = folder / L"independent.txt"; d->Load({"independent 中文"});
        d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x"));
        autosavetest::beforeWrite = Gate; StartWrite(frame, *d); Require(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
        auto started = std::chrono::steady_clock::now(); close(d);
        Require(Milliseconds(started) < 500 && !frame.runtime.autoSaveWrites.empty());
        SetEvent(release); autosavetest::beforeWrite = nullptr; Drain(frame);
        bool kept = false;
        for (const auto& file : std::filesystem::directory_iterator(folder))
            if (file.path().filename().wstring().starts_with(L"independent.txt ") && file.path().extension() != L".ini")
                kept = files::Read(file.path()).utf8 == "independent 中文x";
        Require(kept);

        // Canceled independent snapshots are cleaned after the worker finishes.
        d = frame.NewDocument(); d->path = folder / L"retired-delete.txt"; d->Load({"obsolete"});
        d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x")); ResetEvent(entered); ResetEvent(release);
        autosavetest::beforeWrite = Gate; StartWrite(frame, *d); Require(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
        CancelDocumentBackup(frame, *d); Require(!frame.runtime.autoSaveWrites.empty()); d->Send(SCI_SETSAVEPOINT);
        SetEvent(release); autosavetest::beforeWrite = nullptr; Drain(frame); close(d);
        for (const auto& file : std::filesystem::directory_iterator(folder)) Require(!file.path().filename().wstring().starts_with(L"retired-delete.txt "));

        // A foreground save of the same path waits for an overwrite, then wins.
        d = frame.NewDocument(); d->path = folder / L"overwrite.txt"; d->Load({"captured"});
        files::WriteNew(d->path, {"original"}); d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x"));
        frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1 | 16); ResetEvent(entered); ResetEvent(release);
        autosavetest::beforeWrite = Gate; StartWrite(frame, *d); Require(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
        d->Send(SCI_APPENDTEXT, 7, reinterpret_cast<LPARAM>(" newest"));
        std::jthread unblock([] { Sleep(60); SetEvent(release); }); started = std::chrono::steady_clock::now();
        Require(frame.SaveTo(*d, d->path)); Require(Milliseconds(started) >= 20);
        autosavetest::beforeWrite = nullptr; Require(files::Read(d->path).utf8 == "capturedx newest"); close(d); Drain(frame);

        // Close also serializes an overwrite of its path.
        d = frame.NewDocument(); d->path = folder / L"close-overwrite.txt"; d->Load({"closing"});
        d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x")); ResetEvent(entered); ResetEvent(release);
        autosavetest::beforeWrite = Gate; StartWrite(frame, *d); Require(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
        { std::jthread finish([] { Sleep(60); SetEvent(release); }); started = std::chrono::steady_clock::now(); close(d); Require(Milliseconds(started) >= 20); }
        autosavetest::beforeWrite = nullptr; Require(files::Read(folder / L"close-overwrite.txt").utf8 == "closingx"); Drain(frame);

        if (benchmark) {
            std::ofstream csv("autosave-capture-results.csv"), pumps("autosave-pump-samples.csv");
            pumps << "iteration,path,pump_ms,queued_input_probe_ms\n";
            csv << "iteration,path,tab,bytes,complete_ms,cpu_ms,max_pump_ms,private_before,peak_private,range_copy_bytes,capture_copy_bytes,slices,recoverable_age_ms\n";
            for (int iteration = 0; iteration < 7; ++iteration) for (bool baseline : {true, false}) {
                frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1);
                d = frame.NewDocument(); d->Load({std::string(16 * 1024 * 1024, 'x') + "中文"});
                d->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x")); d->runtime.lastAutoSave = 0;
                auto* peer = frame.NewDocument(); peer->Load({std::string(64 * 1024, 'p')}); peer->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x")); peer->runtime.lastAutoSave = 0;
                autosavetest::baselineCapture = baseline; autosavetest::rangeCopies = autosavetest::captureCopies = autosavetest::captureSlices = 0;
                const auto privateBefore = transformtest::PrivateBytes(); std::atomic<SIZE_T> peak{privateBefore};
                std::jthread sampler([&](std::stop_token stop) { while (!stop.stop_requested()) { peak.store((std::max)(peak.load(), transformtest::PrivateBytes())); Sleep(1); } });
                const auto cpu = transformtest::CpuMilliseconds(); started = std::chrono::steady_clock::now();
                QueueDocumentBackups(frame); double maxPump = 0, age = 0, peerAge = 0;
                while (d->runtime.lastRecoveryRevision != d->style.revision || peer->runtime.lastRecoveryRevision != peer->style.revision) {
                    const auto pump = std::chrono::steady_clock::now(); frame.PostMessage(WM_NULL); PumpDocumentBackup(frame); const auto pumpMs = Milliseconds(pump); maxPump = (std::max)(maxPump, pumpMs);
                    MSG input{}; if (PeekMessageW(&input, frame, WM_NULL, WM_NULL, PM_REMOVE)) DispatchMessageW(&input);
                    pumps << iteration << "," << (baseline ? "before" : "after") << "," << pumpMs << "," << Milliseconds(pump) << "\n";
                    if (!age && d->runtime.lastRecoveryRevision == d->style.revision) age = Milliseconds(started);
                    if (!peerAge && peer->runtime.lastRecoveryRevision == peer->style.revision) peerAge = Milliseconds(started);
                    Require(Milliseconds(started) < 10000); Sleep(30);
                }
                const auto elapsed = Milliseconds(started), cpuElapsed = transformtest::CpuMilliseconds() - cpu; sampler.request_stop(); sampler.join();
                Require(files::Read(d->runtime.recoveryPath).utf8 == d->Text() && files::Read(peer->runtime.recoveryPath).utf8 == peer->Text());
                csv << iteration << ',' << (baseline ? "before" : "after") << ",large," << d->Send(SCI_GETLENGTH) << ',' << elapsed << ',' << cpuElapsed << ',' << maxPump << ',' << privateBefore << ',' << peak.load() << ',' << autosavetest::rangeCopies << ',' << autosavetest::captureCopies << ',' << autosavetest::captureSlices << ',' << age << '\n';
                csv << iteration << ',' << (baseline ? "before" : "after") << ",peer," << peer->Send(SCI_GETLENGTH) << ',' << elapsed << ',' << cpuElapsed << ',' << maxPump << ',' << privateBefore << ',' << peak.load() << ',' << autosavetest::rangeCopies << ',' << autosavetest::captureCopies << ',' << autosavetest::captureSlices << ',' << peerAge << '\n'; csv.flush();
                close(d); close(peer); Drain(frame);
            }
        }
        autosavetest::baselineCapture = false; frame.settings = saved;
        CloseHandle(entered); CloseHandle(release); entered = release = nullptr; std::filesystem::remove_all(folder);
    } catch (...) {
        autosavetest::beforeWrite = nullptr; autosavetest::baselineCapture = false; SetEvent(release);
        Drain(frame); frame.settings = saved; CloseHandle(entered); CloseHandle(release); throw;
    }
}

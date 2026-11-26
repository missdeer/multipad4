#include "SearchWork.h"
#include "LazySearch.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <source_location>
#include "SmbShutdownFixture.h"
void Check(bool condition, std::source_location where = std::source_location::current()) { if (!condition) throw std::runtime_error("Search retirement regression at " + std::to_string(where.line())); }
template<class F> void Until(F predicate, std::source_location where = std::source_location::current()) {
    const auto deadline = GetTickCount64() + 5000;
    while (!predicate()) { Check(GetTickCount64() < deadline, where); Sleep(1); }
}

int main() {
    try {
        auto edited = large::Document::FromUtf8(std::make_shared<const std::string>(""));
        const auto disk = std::string(65535, 'x') + "\r";
        edited->Replace(0, 0, disk); edited->Replace(disk.size(), 0, "\ntail");
        const auto snapshot = edited->Capture();
        large::Document::ReadSession session(snapshot);
        Check(session.Read(65534, 7) == "x\r\ntail");
        Check(large::Document::LineCount(snapshot) == 2 && large::Document::PositionFromLine(snapshot, 1) == 65537);
        Check(large::Document::LineFromPosition(snapshot, 65536) == 0);
        std::atomic<bool> firstToken{false}, secondToken{false};
        large::Search matcher(snapshot, files::Encoding::Utf8, {L"tail"}, nullptr, true);
        matcher.SetCancellation(&firstToken); Check(matcher.Next(0, matcher.Size()).has_value()); matcher.SetCancellation(nullptr);
        firstToken = true; matcher.SetCancellation(&secondToken);
        Check(matcher.Next(0, matcher.Size()).has_value()); matcher.SetCancellation(nullptr);
        HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::vector<work::SearchTask<void>> jobs;
        std::atomic<size_t> entered{0}, exited{0};
        auto payload = std::make_shared<int>(42); auto weak = std::weak_ptr<int>(payload);
        for (int i = 0; i < 8; ++i) jobs.emplace_back(work::TrySearchPermit(), [&, payload](const auto&) { ++entered; WaitForSingleObject(release, INFINITE); Check(*payload == 42); ++exited; });
        payload.reset(); Until([&] { return entered == 8; }); Check(!work::TrySearchPermit());
        const auto start = std::chrono::steady_clock::now();
        for (auto& job : jobs) job.Reset();
        const auto cancelMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Check(cancelMs < 100 && !weak.expired() && !work::TrySearchPermit());
        SetEvent(release); Until([&] { return exited == 8 && weak.expired(); });
        Until([&] { return work::TrySearchPermit() != nullptr; }); CloseHandle(release);
        release = CreateEventW(nullptr, TRUE, FALSE, nullptr); entered = exited = 0;
        auto group = work::TrySearchPermit();
        for (int i = 0; i < 9; ++i) jobs.emplace_back(group, [&](const auto&) { ++entered; WaitForSingleObject(release, INFINITE); ++exited; });
        Until([&] { return entered == 9; });
        work::SearchTask<void> excess(group, [](const auto&) {}); Check(!excess.Valid() && !group->HasCapacity());
        for (auto& job : jobs) job.Reset(); Check(!group->HasCapacity());
        SetEvent(release); Until([&] { return group->callbacks == 0; }); group.reset(); CloseHandle(release);
        HANDLE reader, writer; Check(CreatePipe(&reader, &writer, nullptr, 0) != FALSE);
        std::atomic<bool> reading{false};
        work::SearchTask<DWORD> io(work::TrySearchPermit(), [&](const auto&) {
            reading = true; char byte; DWORD count = 0;
            return ReadFile(reader, &byte, 1, &count, nullptr) ? ERROR_SUCCESS : GetLastError();
        });
        Until([&] { return reading.load(); }); Sleep(20);
        io.Cancel(); Until([&] { return io.Ready(); }); Check(io.Get() == ERROR_OPERATION_ABORTED);
        CloseHandle(reader); CloseHandle(writer);
        HANDLE lateReader, lateWriter; Check(CreatePipe(&lateReader, &lateWriter, nullptr, 0) != FALSE);
        HANDLE lateGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        reading = false;
        work::SearchTask<DWORD> lateIo(work::TrySearchPermit(), [&](const auto&) {
            reading = true; WaitForSingleObject(lateGate, INFINITE);
            char byte; DWORD count = 0;
            return ReadFile(lateReader, &byte, 1, &count, nullptr) ? ERROR_SUCCESS : GetLastError();
        });
        Until([&] { return reading.load(); }); lateIo.Cancel(); SetEvent(lateGate);
        Until([&] { return lateIo.Ready(); }); Check(lateIo.Get() == ERROR_OPERATION_ABORTED);
        CloseHandle(lateReader); CloseHandle(lateWriter); CloseHandle(lateGate);
        HANDLE nestedReader, nestedWriter; Check(CreatePipe(&nestedReader, &nestedWriter, nullptr, 0) != FALSE);
        std::atomic<unsigned> childReaders{0}, aborted{0};
        const auto childCount = (std::min)(2u, work::Concurrency());
        work::SearchTask<void> nested(work::TrySearchPermit(), [&](const auto& stop) {
            work::ParallelFor(childCount, [&](size_t) {
                work::ReadCancellation scope(&stop); ++childReaders;
                char byte; DWORD count; if (!ReadFile(nestedReader, &byte, 1, &count, nullptr) && GetLastError() == ERROR_OPERATION_ABORTED) ++aborted;
            });
        });
        Until([&] { return childReaders == childCount; }); Sleep(20); nested.Cancel();
        Until([&] { return nested.Ready(); }); nested.Get(); Check(aborted == childCount);
        CloseHandle(nestedReader); CloseHandle(nestedWriter);
        const auto drained = [] { const auto registry = work::searchDetail::RegistryOwner(); std::lock_guard guard(registry->mutex); return registry->groups == 0; };
        Until(drained);
        HANDLE disposing = CreateEventW(nullptr, TRUE, FALSE, nullptr), disposeRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        struct Resource {
            HANDLE disposing, release;
            Resource(HANDLE a, HANDLE b) : disposing(a), release(b) {}
            ~Resource() { SetEvent(disposing); WaitForSingleObject(release, INFINITE); }
        };
        std::vector<std::shared_ptr<work::SearchPermit>> reserved;
        for (int i = 0; i < 7; ++i) { reserved.push_back(work::TrySearchPermit()); Check(reserved.back() != nullptr); }
        auto resource = std::make_shared<Resource>(disposing, disposeRelease);
        work::SearchTask<std::shared_ptr<Resource>> completed(work::TrySearchPermit(), [resource](const auto&) { return resource; });
        resource.reset(); Until([&] { return completed.Ready(); });
        const auto disposeStart = std::chrono::steady_clock::now(); completed.Reset();
        Check(std::chrono::steady_clock::now() - disposeStart < std::chrono::milliseconds(100));
        Check(WaitForSingleObject(disposing, 5000) == WAIT_OBJECT_0 && !work::TrySearchPermit());
        SetEvent(disposeRelease); reserved.clear(); Until(drained); CloseHandle(disposing); CloseHandle(disposeRelease);
        std::atomic<bool> oldToken{false};
        { work::ReadCancellation read(&oldToken); }
        {
            const auto readers = work::searchDetail::Readers(); std::lock_guard guard(readers->mutex);
            for (const auto& item : readers->states) if (auto reader = item.lock()) Check(reader->token != &oldToken || !reader->thread);
        }
        HANDLE queuedGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        work::searchDetail::gate = queuedGate;
        const auto beforeQueued = work::searchDetail::entered.load();
        std::atomic<bool> ran{false};
        work::SearchTask<void> queued(work::TrySearchPermit(), [&](const auto&) { ran = true; });
        Until([&] { return work::searchDetail::entered > beforeQueued; });
        queued.Cancel(); work::searchDetail::gate = nullptr; SetEvent(queuedGate);
        Until([&] { return queued.Ready(); });
        bool cancelled = false; try { queued.Get(); } catch (const std::runtime_error&) { cancelled = true; }
        Check(cancelled && !ran); Until(drained); CloseHandle(queuedGate);
        if (RunSmbShutdownFixture([](auto& blocked) { work::StopSearchAdmission(); blocked.Reset(); })) return 0;
        // Shutdown revokes admission without waiting for an uncancellable callback.
        release = CreateEventW(nullptr, TRUE, FALSE, nullptr); entered = exited = 0;
        work::SearchTask<void> shutdown(work::TrySearchPermit(), [&](const auto&) { ++entered; WaitForSingleObject(release, INFINITE); ++exited; });
        Until([&] { return entered == 1; });
        const auto stop = std::chrono::steady_clock::now();
        auto previouslyAdmitted = work::TrySearchPermit(); work::StopSearchAdmission();
        work::SearchTask<void> afterClose(previouslyAdmitted, [&](const auto&) { ran = true; }); Check(!afterClose.Valid());
        previouslyAdmitted.reset();
        Check(std::chrono::steady_clock::now() - stop < std::chrono::milliseconds(100));
        Check(!work::TrySearchPermit() && work::SearchClosing()); shutdown.Reset(); SetEvent(release);
        Until([&] { return exited == 1; }); CloseHandle(release);
        std::cout << "Retired eight blocked workers in " << cancelMs << " ms; payload lifetime, cap, synchronous pipe cancellation and shutdown passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

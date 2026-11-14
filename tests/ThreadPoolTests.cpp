#include "ThreadPool.h"
#include <iostream>
#include <source_location>
#include <string>
#include <string_view>

namespace {
void Check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Thread pool regression at line " + std::to_string(where.line()));
}
struct Event {
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Event() { Check(handle != nullptr); }
    ~Event() { CloseHandle(handle); }
    void Signal() const { SetEvent(handle); }
    bool Wait() const { return WaitForSingleObject(handle, 10000) == WAIT_OBJECT_0; }
};
}

int main() {
    try {
        work::Task<void> empty;
        Check(!empty.Valid() && !empty.Ready()); empty.Wait(); empty.Reset();
        auto result = work::Submit([] { return std::make_unique<int>(42); });
        auto moved = std::move(result);
        Check(!result.Valid() && moved.Valid());
        Check(*moved.Get() == 42 && !moved.Valid());
        bool emptyRejected = false;
        try { moved.Get(); } catch (const std::logic_error&) { emptyRejected = true; }
        Check(emptyRejected);

        auto failure = work::Submit([]() -> int { throw std::runtime_error("worker failure"); });
        bool caught = false;
        try { failure.Get(); } catch (const std::runtime_error& error) { caught = std::string_view(error.what()) == "worker failure"; }
        Check(caught && !failure.Valid());
        auto nonStandard = work::Submit([] { throw 17; });
        caught = false;
        try { nonStandard.Get(); } catch (int value) { caught = value == 17; }
        Check(caught);

        // Ready polls do not wait; Reset and move assignment drain callbacks
        // before releasing their borrowed context, including void tasks.
        Event release;
        std::atomic<bool> finished{false};
        auto pending = work::Submit([&] { Check(release.Wait()); finished = true; });
        Check(!pending.Ready());
        release.Signal(); pending.Wait();
        Check(pending.Ready() && finished.load());
        pending.Reset(); Check(!pending.Valid());
        unsigned drained = 0;
        {
            auto owned = work::Submit([&] { ++drained; });
            owned = work::Submit([] {});
            Check(drained == 1);
        }
        {
            auto owned = work::Submit([&] { ++drained; });
        }
        Check(drained == 2);

        // Leaf callbacks finish before a failing batch unwinds borrowed data.
        std::atomic<unsigned> completed{0};
        caught = false;
        try {
            work::ParallelFor(16, [&](size_t index) {
                if (!index) throw std::runtime_error("batch failure");
                ++completed;
            });
        } catch (const std::runtime_error&) { caught = true; }
        Check(caught && completed == 15);

        // Fill the compute pool with parents that synchronously join children.
        // This also exercises the nested path on a single-processor machine.
        completed = 0;
        work::ParallelFor(work::Concurrency(), [&](size_t) {
            work::ParallelFor(4, [&](size_t) { ++completed; });
            auto child = work::Submit([&] { ++completed; return 7; }, work::Queue::Compute);
            Check(child.Ready() && child.Get() == 7);
        });
        Check(completed == work::Concurrency() * 5);

        // Compute work has a process-wide ceiling, shared by separate callers.
        Event allStarted, finish;
        std::atomic<unsigned> active{0}, peak{0};
        std::vector<work::Task<void>> tasks;
        for (unsigned i = 0; i < work::Concurrency() * 2; ++i) {
            tasks.push_back(work::Submit([&] {
                const auto count = ++active;
                auto previous = peak.load();
                while (previous < count && !peak.compare_exchange_weak(previous, count)) {}
                if (count == work::Concurrency()) allStarted.Signal();
                const bool released = finish.Wait();
                --active;
                Check(released);
            }, work::Queue::Compute));
        }
        const bool started = allStarted.Wait();
        // A full compute pool cannot prevent a background callback starting.
        Event backgroundDone;
        auto independent = work::Submit([&] { backgroundDone.Signal(); return 9; });
        const bool backgroundRan = backgroundDone.Wait();
        finish.Signal();
        for (auto& task : tasks) task.Get();
        Check(started && peak == work::Concurrency() && active == 0);
        Check(backgroundRan && independent.Get() == 9);
        std::cout << "Windows thread pool checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

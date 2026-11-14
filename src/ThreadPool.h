#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace work {
enum class Queue { Background, Compute };

inline unsigned Concurrency() noexcept {
    return (std::clamp)(static_cast<unsigned>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)), 1u, 8u);
}

namespace detail {
// Nested compute submissions run inline, so even a one-worker pool cannot
// deadlock waiting for children queued behind their parent.
inline thread_local bool inCompute = false;

class ComputePool {
    PTP_POOL pool = nullptr;
    TP_CALLBACK_ENVIRON environment{};
public:
    ComputePool() {
        pool = CreateThreadpool(nullptr);
        if (!pool) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateThreadpool");
        SetThreadpoolThreadMaximum(pool, Concurrency());
        InitializeThreadpoolEnvironment(&environment);
        SetThreadpoolCallbackPool(&environment, pool);
    }
    ~ComputePool() { DestroyThreadpoolEnvironment(&environment); CloseThreadpool(pool); }
    ComputePool(const ComputePool&) = delete;
    ComputePool& operator=(const ComputePool&) = delete;
    static PTP_CALLBACK_ENVIRON Environment() {
        static ComputePool instance;
        return &instance.environment;
    }
};
}

// A move-only owner of one submitted Windows work callback. Destruction and
// Reset drain the callback before freeing its captures, result or context.
// Owners must not destroy/wait for themselves from their callback.
template<class T> class Task {
    static_assert(!std::is_reference_v<T>);
    using Result = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    struct State {
        std::function<T()> action;
        std::optional<Result> result;
        std::exception_ptr error;
        std::atomic<bool> ready{false};
        PTP_WORK work = nullptr;
        Queue queue;
        State(std::function<T()> value, Queue target) : action(std::move(value)), queue(target) {}
        ~State() {
            if (work) {
                WaitForThreadpoolWorkCallbacks(work, FALSE);
                CloseThreadpoolWork(work);
            }
        }
    };
    std::unique_ptr<State> state;
    static void CALLBACK Run(PTP_CALLBACK_INSTANCE instance, void* context, PTP_WORK) noexcept {
        auto& value = *static_cast<State*>(context);
        const bool previous = detail::inCompute;
        detail::inCompute = value.queue == Queue::Compute;
        // Background callbacks may do synchronous I/O, wait for compute work,
        // or block on a bounded result queue. Let Windows compensate for that.
        if (value.queue == Queue::Background) CallbackMayRunLong(instance);
        try {
            if constexpr (std::is_void_v<T>) { value.action(); value.result.emplace(); }
            else value.result.emplace(value.action());
        } catch (...) { value.error = std::current_exception(); }
        detail::inCompute = previous;
        value.ready.store(true, std::memory_order_release);
    }
public:
    Task() = default;
    explicit Task(std::function<T()> action, Queue queue = Queue::Background) {
        auto environment = queue == Queue::Compute ? detail::ComputePool::Environment() : nullptr;
        state = std::make_unique<State>(std::move(action), queue);
        if (queue == Queue::Compute && detail::inCompute) {
            Run(nullptr, state.get(), nullptr);
            return;
        }
        state->work = CreateThreadpoolWork(Run, state.get(), environment);
        if (!state->work) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateThreadpoolWork");
        SubmitThreadpoolWork(state->work);
    }
    Task(Task&&) noexcept = default;
    Task& operator=(Task&&) noexcept = default;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    bool Valid() const noexcept { return state != nullptr; }
    bool Ready() const noexcept { return state && state->ready.load(std::memory_order_acquire); }
    void Wait() const noexcept { if (state && state->work) WaitForThreadpoolWorkCallbacks(state->work, FALSE); }
    void Reset() noexcept { state.reset(); }
    T Get() {
        if (!state) throw std::logic_error("Empty background task.");
        Wait();
        auto completed = std::move(state);
        if (completed->error) std::rethrow_exception(completed->error);
        if constexpr (!std::is_void_v<T>) return std::move(*completed->result);
    }
};

template<class F> auto Submit(F&& action, Queue queue = Queue::Background) {
    return Task<std::invoke_result_t<F>>(std::forward<F>(action), queue);
}

// A synchronous fork/join of independent leaf jobs. All captures remain alive
// on submission failure and worker exceptions because Task destruction drains.
template<class F> void ParallelFor(size_t count, F&& action) {
    if (count == 1 || detail::inCompute) {
        for (size_t index = 0; index < count; ++index) action(index);
        return;
    }
    std::vector<Task<void>> tasks;
    tasks.reserve(count);
    for (size_t index = 0; index < count; ++index)
        tasks.push_back(Submit([&action, index] { action(index); }, Queue::Compute));
    for (auto& task : tasks) task.Get();
}
}

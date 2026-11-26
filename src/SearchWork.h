#pragma once
#include "ThreadPool.h"
#include <mutex>
#include <condition_variable>
namespace work {
namespace searchDetail {
struct CancellableThread {
    std::mutex mutex;
    std::condition_variable idle;
    HANDLE thread = nullptr;
    unsigned cancelling = 0;
    bool closing = false;
    void Bind() {
        std::lock_guard guard(mutex);
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "DuplicateHandle");
    }
    void CancelIo() {
        HANDLE target;
        { std::lock_guard guard(mutex); if (closing || !thread) return; ++cancelling; target = thread; }
        CancelSynchronousIo(target);
        { std::lock_guard guard(mutex); --cancelling; } idle.notify_all();
    }
    void Close() {
        std::unique_lock guard(mutex); closing = true;
        // Keep the pool thread in this callback until every OS cancellation returns.
        idle.wait(guard, [&] { return cancelling == 0; });
        if (thread) { CloseHandle(thread); thread = nullptr; }
    }
    ~CancellableThread() { Close(); }
};
struct ReadState : CancellableThread { const std::atomic<bool>* token = nullptr; };
struct ReadRegistry { std::mutex mutex; std::vector<std::weak_ptr<ReadState>> states; };
// Retired callbacks can unregister during CRT teardown; the registry lasts until process exit.
inline ReadRegistry* Readers() { static auto* value = new ReadRegistry; return value; }
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
inline std::atomic<HANDLE> gate{nullptr};
inline std::atomic<size_t> entered{0};
#endif
struct StateBase : std::enable_shared_from_this<StateBase> {
    std::atomic<bool> cancel{false}, ready{false}, retryScheduled{false};
    std::mutex mutex;
    CancellableThread io;
    std::function<void()> onCancel;
    virtual ~StateBase() = default;
    void CancelOnce() noexcept {
        io.CancelIo();
        std::vector<std::shared_ptr<ReadState>> active;
        const auto readers = Readers();
        { std::lock_guard guard(readers->mutex);
            for (const auto& item : readers->states) if (auto reader = item.lock(); reader && reader->token == &cancel) active.push_back(std::move(reader));
        }
        for (const auto& reader : active) reader->CancelIo();
    }
    void Cancel() noexcept {
        cancel.store(true, std::memory_order_relaxed);
        { std::lock_guard guard(mutex); if (onCancel) onCancel(); }
        if (ready.load(std::memory_order_acquire) || retryScheduled.exchange(true)) return;
        auto* owner = new (std::nothrow) std::shared_ptr<StateBase>(shared_from_this());
        if (!owner) { retryScheduled = false; return; }
        if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE instance, void* data) {
            CallbackMayRunLong(instance);
            std::unique_ptr<std::shared_ptr<StateBase>> owner(static_cast<std::shared_ptr<StateBase>*>(data));
            // A token check cannot atomically precede synchronous I/O submission.
            while (!(**owner).ready.load(std::memory_order_acquire)) { (**owner).CancelOnce(); Sleep(10); }
        }, owner, nullptr)) { delete owner; retryScheduled = false; }
    }
};
struct Registry {
    std::mutex mutex;
    bool closing = false;
    size_t groups = 0;
    std::vector<std::weak_ptr<StateBase>> states;
};
inline std::shared_ptr<Registry> RegistryOwner() { static auto registry = std::make_shared<Registry>(); return registry; }
}
class ReadCancellation {
    std::shared_ptr<searchDetail::ReadState> state;
public:
    ReadCancellation(const ReadCancellation&) = delete;
    ReadCancellation& operator=(const ReadCancellation&) = delete;
    ~ReadCancellation() {
        if (!state) return;
        state->Close();
    }
    explicit ReadCancellation(const std::atomic<bool>* token) {
        if (!token) return;
        state = std::make_shared<searchDetail::ReadState>(); state->token = token;
        state->Bind();
        const auto readers = searchDetail::Readers(); std::lock_guard guard(readers->mutex);
        std::erase_if(readers->states, [](const auto& item) { return item.expired(); }); readers->states.push_back(state);
    }
};
struct SearchPermit {
    std::shared_ptr<searchDetail::Registry> registry;
    std::atomic<size_t> callbacks{0};
    bool HasCapacity() const { return callbacks.load(std::memory_order_acquire) < 9; }
    bool TryCallback() {
        auto count = callbacks.load();
        while (count < 9) if (callbacks.compare_exchange_weak(count, count + 1)) return true;
        return false;
    }
    ~SearchPermit() { std::lock_guard guard(registry->mutex); --registry->groups; }
};
inline std::shared_ptr<SearchPermit> TrySearchPermit() {
    const auto registry = searchDetail::RegistryOwner();
    std::lock_guard guard(registry->mutex);
    if (registry->closing || registry->groups == 8) return {};
    auto permit = std::make_shared<SearchPermit>(); permit->registry = registry; ++registry->groups; return permit;
}
inline bool SearchClosing() { const auto registry = searchDetail::RegistryOwner(); std::lock_guard guard(registry->mutex); return registry->closing; }
inline void StopSearchAdmission() {
    const auto registry = searchDetail::RegistryOwner();
    std::vector<std::shared_ptr<searchDetail::StateBase>> states;
    { std::lock_guard guard(registry->mutex); registry->closing = true;
        for (const auto& item : registry->states) if (auto state = item.lock()) states.push_back(std::move(state));
    }
    for (const auto& state : states) state->Cancel();
}
template<class T> void RetireOwner(std::shared_ptr<T> value) {
    if (!value) return;
    auto* context = new (std::nothrow) std::shared_ptr<T>(std::move(value));
    if (!context) return;
    if (!TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE instance, void* data) {
        CallbackMayRunLong(instance); std::unique_ptr<std::shared_ptr<T>> owner(static_cast<std::shared_ptr<T>*>(data));
    }, context, nullptr)) delete context;
}
// Callbacks own their state until return; dropping this handle never joins I/O.
template<class T> class SearchTask {
    using Result = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    struct State : searchDetail::StateBase {
        std::shared_ptr<SearchPermit> permit;
        std::function<T(const std::atomic<bool>&)> action;
        std::optional<Result> result;
        std::exception_ptr error;
        PTP_WORK work = nullptr;
        bool reserved = false;
        ~State() { action = {}; result.reset(); error = {}; onCancel = {}; if (work) CloseThreadpoolWork(work); if (reserved) --permit->callbacks; }
    };
    std::shared_ptr<State> state;
    static void CALLBACK Run(PTP_CALLBACK_INSTANCE instance, void* context, PTP_WORK) noexcept {
        std::unique_ptr<std::shared_ptr<State>> owner(static_cast<std::shared_ptr<State>*>(context));
        auto& state = **owner;
        CallbackMayRunLong(instance);
        try {
            state.io.Bind();
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
            if (const auto gate = searchDetail::gate.load()) { ++searchDetail::entered; WaitForSingleObject(gate, INFINITE); }
#endif
            if (state.cancel.load(std::memory_order_acquire)) throw std::runtime_error("Search cancelled.");
            if constexpr (std::is_void_v<T>) { state.action(state.cancel); state.result.emplace(); }
            else state.result.emplace(state.action(state.cancel));
        } catch (...) { state.error = std::current_exception(); }
        state.action = {};
        {
            std::lock_guard guard(state.mutex);
            state.onCancel = {};
        }
        state.io.Close();
        state.ready.store(true, std::memory_order_release);
    }
public:
    SearchTask() = default;
    ~SearchTask() { Reset(); }
    SearchTask(const SearchTask&) = delete;
    SearchTask& operator=(const SearchTask&) = delete;
    SearchTask(SearchTask&& other) noexcept : state(std::move(other.state)) {}
    SearchTask& operator=(SearchTask&& other) noexcept { if (this != &other) { Reset(); state = std::move(other.state); } return *this; }
    SearchTask(std::shared_ptr<SearchPermit> permit, std::function<T(const std::atomic<bool>&)> action, std::function<void()> onCancel = {}) {
        if (!permit || !permit->HasCapacity()) return;
        auto value = std::make_shared<State>(); value->permit = std::move(permit); value->action = std::move(action); value->onCancel = std::move(onCancel);
        if (!value->permit->TryCallback()) return;
        value->reserved = true;
        const auto registry = value->permit->registry;
        {
            std::lock_guard guard(registry->mutex);
            if (registry->closing) return;
            std::erase_if(registry->states, [](const auto& item) { return item.expired(); });
            registry->states.push_back(value);
            auto context = std::make_unique<std::shared_ptr<State>>(value);
            value->work = CreateThreadpoolWork(Run, context.get(), nullptr);
            if (!value->work) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateThreadpoolWork");
            state = std::move(value); SubmitThreadpoolWork(state->work); context.release();
        }
    }
    bool Valid() const { return state != nullptr; }
    bool Ready() const { return state && state->ready.load(std::memory_order_acquire); }
    void Cancel() const { if (state) state->Cancel(); }
    void Reset() { Cancel(); RetireOwner(std::move(state)); }
    T Get() {
        if (!Ready()) throw std::logic_error("Search result is not ready.");
        auto value = std::move(state);
        if (value->error) { const auto error = value->error; RetireOwner(std::move(value)); std::rethrow_exception(error); }
        if constexpr (!std::is_void_v<T>) { auto result = std::move(*value->result); RetireOwner(std::move(value)); return result; }
        else RetireOwner(std::move(value));
    }
};
}

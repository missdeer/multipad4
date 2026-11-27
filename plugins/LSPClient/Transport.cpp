#include "Transport.h"

namespace lsp {
void Transport::Fail(const char* message) {
    { std::lock_guard lock(mutex); if (failure.empty()) failure = message; }
    stopped = true; wake.notify_all();
}
Transport::Transport(const std::wstring& command, const std::filesystem::path& root, const std::filesystem::path& stderrFile) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE childIn = nullptr, childOut = nullptr, nullError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    try {
        if (!CreatePipe(&childIn, &input, &security, 0) || !CreatePipe(&output, &childOut, &security, 0) ||
            !SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("Cannot create LSP pipes");
        nullError = CreateFileW(stderrFile.empty() ? L"NUL" : stderrFile.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &security,
            stderrFile.empty() ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nullError == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open LSP stderr");
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            throw std::runtime_error("Cannot create LSP process job");
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) throw std::runtime_error("Cannot initialize LSP process attributes");
        HANDLE handles[]{childIn, childOut, nullError};
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = childIn; startup.StartupInfo.hStdOutput = childOut; startup.StartupInfo.hStdError = nullError;
        startup.lpAttributeList = attributes;
        auto line = command;
        const bool attributesOk = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr) != FALSE;
        const bool created = attributesOk && CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, root.c_str(), &startup.StartupInfo, &pi);
        DeleteProcThreadAttributeList(attributes);
        if (!created) throw std::runtime_error("Cannot start language server. Check LSPClient.json and PATH.");
        process = pi.hProcess;
        if (!AssignProcessToJobObject(job, process)) throw std::runtime_error("Cannot assign LSP process job");
        ResumeThread(pi.hThread); CloseHandle(pi.hThread); pi.hThread = nullptr;
        CloseHandle(childIn); childIn = nullptr; CloseHandle(childOut); childOut = nullptr; CloseHandle(nullError); nullError = INVALID_HANDLE_VALUE;
        reader = std::thread([this] {
            try {
                Decoder decoder; char bytes[8192]; DWORD count;
                while (!stopped && ReadFile(output, bytes, sizeof(bytes), &count, nullptr) && count)
                    decoder.Feed({bytes, count}, [this](Json value) {
                        const auto size = value.dump().size();
                        std::lock_guard lock(mutex);
                        if (incoming.size() >= 1024 || received + size > MaxMessage) throw std::runtime_error("LSP receive queue overflow");
                        received += size; incoming.push_back(std::move(value));
                    });
                if (!stopped) Fail("Language server disconnected");
            } catch (const std::exception& e) { Fail(e.what()); }
        });
        writer = std::thread([this] {
            for (;;) {
                std::string message;
                {
                    std::unique_lock lock(mutex); wake.wait(lock, [this] { return stopped || !outgoing.empty(); });
                    if (stopped) return;
                    message = std::move(outgoing.front()); outgoing.pop_front(); queued -= message.size();
                }
                size_t done = 0;
                while (done < message.size() && !stopped) {
                    DWORD count = 0;
                    if (!WriteFile(input, message.data() + done, static_cast<DWORD>(message.size() - done), &count, nullptr) || !count) {
                        Fail("Cannot write to language server"); return;
                    }
                    done += count;
                }
            }
        });
    } catch (...) {
        stopped = true; wake.notify_all();
        if (process) TerminateProcess(process, 1);
        if (pi.hThread) CloseHandle(pi.hThread);
        if (reader.joinable()) { CancelSynchronousIo(reader.native_handle()); reader.join(); }
        for (HANDLE h : {childIn, childOut, nullError, input, output, process, job}) if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
        throw;
    }
}
Transport::~Transport() {
    stopped = true; wake.notify_all();
    if (job) TerminateJobObject(job, 0);
    if (reader.joinable()) { CancelSynchronousIo(reader.native_handle()); reader.join(); }
    if (writer.joinable()) { CancelSynchronousIo(writer.native_handle()); writer.join(); }
    for (HANDLE h : {input, output, process, job}) if (h) CloseHandle(h);
}
void Transport::Send(Json value) {
    auto message = Frame(value);
    { std::lock_guard lock(mutex);
      if (stopped) throw std::runtime_error(failure.empty() ? "LSP transport stopped" : failure);
      if (queued + message.size() > MaxMessage) throw std::runtime_error("LSP send queue overflow");
      queued += message.size(); outgoing.push_back(std::move(message)); }
    wake.notify_one();
}
std::deque<Json> Transport::Poll() { std::lock_guard lock(mutex); received = 0; return std::exchange(incoming, {}); }
std::string Transport::Error() {
    std::lock_guard lock(mutex);
    if (failure.empty()) return {};
    DWORD code = STILL_ACTIVE;
    if (GetExitCodeProcess(process, &code) && code != STILL_ACTIVE) {
        char suffix[64]; snprintf(suffix, sizeof(suffix), " (server exit code 0x%08lX)", code);
        return failure + suffix;
    }
    return failure;
}
}

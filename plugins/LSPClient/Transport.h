#pragma once
#include "Protocol.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <filesystem>

namespace lsp {
class Transport {
    HANDLE process = nullptr, job = nullptr, input = nullptr, output = nullptr;
    std::thread reader, writer;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> outgoing;
    std::deque<Json> incoming;
    size_t queued = 0, received = 0;
    std::atomic<bool> stopped{false};
    std::string failure;
    void Fail(const char* message);
public:
    Transport(const std::wstring& command, const std::filesystem::path& root, const std::filesystem::path& stderrFile = {});
    ~Transport();
    void Send(Json value);
    std::deque<Json> Poll();
    std::string Error();
};
}

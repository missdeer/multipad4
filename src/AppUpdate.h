#pragma once
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>

class MainFrame;

struct AppRelease {
    std::wstring tag, url, hash;
};

struct StagedAppUpdate {
    std::filesystem::path directory;
    bool handedOff = false;
    ~StagedAppUpdate();
};

struct AppUpdater {
    std::future<std::optional<AppRelease>> check;
    std::future<std::unique_ptr<StagedAppUpdate>> staging;
    std::unique_ptr<StagedAppUpdate> ready;
    std::chrono::steady_clock::time_point nextCheck{};
    bool manual = false, enabled = false;
    bool Busy() const { return check.valid() || staging.valid() || ready != nullptr; }
    void Check(bool userRequested);
    void Poll(MainFrame& frame);
};

int RunAppUpdateWorker(const wchar_t* parentPid);
void LaunchAppUpdateWorker(StagedAppUpdate& update);
std::wstring TakeAppUpdateCleanup();
void CleanUpAppUpdate(const std::wstring& cleanup);
void TestAppUpdate(const std::filesystem::path& scratch);

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

struct AppUpdateRestart {
    std::filesystem::path directory;
    std::wstring workerPid;
};

struct AppUpdater {
    std::future<std::optional<AppRelease>> check;
    std::future<std::pair<AppRelease, std::string>> download;
    std::future<std::unique_ptr<StagedAppUpdate>> staging;
    std::unique_ptr<StagedAppUpdate> ready;
    std::chrono::steady_clock::time_point nextCheck{};
    bool manual = false, enabled = false;
    bool Busy() const { return check.valid() || download.valid() || staging.valid() || ready != nullptr; }
    void Check(bool userRequested);
    void Poll(MainFrame& frame);
};

int RunAppUpdateWorker(const wchar_t* parentPid, const std::filesystem::path& settingsPath);
void LaunchAppUpdateWorker(StagedAppUpdate& update, const std::filesystem::path& settingsPath);
std::optional<AppUpdateRestart> TakeAppUpdateRestart();
std::filesystem::path UpdateSessionPath(const MainFrame& frame, const std::filesystem::path& stage);
void CleanUpAppUpdate(const AppUpdateRestart& restart);
void TestAppUpdate(const std::filesystem::path& scratch);

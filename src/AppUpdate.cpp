#include "AppUpdate.h"
#include "MainFrame.h"
#include "PackageInstall.h"
#include "PluginDownload.h"
#include "../3rdparty/nlohmann-json/json.hpp"
#include <charconv>
#include <fstream>
#include <thread>
#include <system_error>

namespace {
namespace fs = std::filesystem;
using Version = std::array<unsigned, 3>;
constexpr wchar_t StagePrefix[] = L".multipad4-update-";
constexpr wchar_t AppFilename[] = L"multipad4.exe";
constexpr wchar_t RegistryHelperFilename[] = L"multipad4-registry.exe";
constexpr wchar_t CleanupVariable[] = L"MULTIPAD4_UPDATE_CLEANUP";
constexpr char ReleaseDownload[] = "https://github.com/missdeer/multipad4/releases/download/";
constexpr auto NoWait = std::chrono::seconds(0);

auto StartBackgroundRequest(auto request) {
    std::packaged_task task(std::move(request));
    auto result = task.get_future();
    std::thread(std::move(task)).detach();
    return result;
}

Version ReleaseVersion(std::string_view tag) {
    if (!tag.starts_with('v')) throw std::runtime_error("Invalid release version.");
    tag.remove_prefix(1);
    Version result{};
    for (size_t index = 0; index < result.size(); ++index) {
        const auto end = tag.find('.');
        const auto part = tag.substr(0, end);
        if (part.empty()) throw std::runtime_error("Invalid release version.");
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || result[index] > 65535)
            throw std::runtime_error("Invalid release version.");
        if (end == tag.npos && index > 0) return result;
        if (end == tag.npos || index == result.size() - 1) break;
        tag.remove_prefix(end + 1);
    }
    throw std::runtime_error("Invalid release version.");
}

std::optional<AppRelease> SelectRelease(std::string_view metadata, Version current, std::string_view assetName) {
    const auto release = nlohmann::json::parse(metadata);
    if (release.at("draft").get<bool>() || release.at("prerelease").get<bool>()) return {};
    const auto tag = release.at("tag_name").get<std::string>();
    if (ReleaseVersion(tag) <= current) return {};
    for (const auto& asset : release.at("assets")) {
        if (asset.at("name").get_ref<const std::string&>() != assetName ||
            asset.at("state").get_ref<const std::string&>() != "uploaded") continue;
        const auto url = asset.at("browser_download_url").get<std::string>();
        const auto digest = asset.at("digest").get<std::string>();
        if (url != ReleaseDownload + tag + '/' + std::string(assetName) ||
            !digest.starts_with("sha256:") || digest.size() != 71 ||
            digest.find_first_not_of("0123456789abcdefABCDEF", 7) != digest.npos)
            throw std::runtime_error("Invalid update package metadata.");
        return AppRelease{files::FromUtf8(tag), files::FromUtf8(url), files::FromUtf8(digest.substr(7))};
    }
    throw std::runtime_error("The latest release has no update for this build.");
}

fs::path ExecutablePath() {
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(nullptr, path, _countof(path));
    if (!length || length == _countof(path)) throw std::runtime_error("Cannot locate MultiPad4.");
    return path;
}

Version FileVersion(const fs::path& path) {
    DWORD ignored = 0;
    const auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    std::vector<char> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!size || !GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        length < sizeof(*info) || info->dwSignature != VS_FFI_SIGNATURE)
        throw std::runtime_error("Cannot read the MultiPad4 executable version.");
    return {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS)};
}

bool ExistingFile(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
        throw std::system_error(static_cast<int>(error), std::system_category(), "Cannot inspect package file");
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Expected a regular file: " + files::ToUtf8(path.wstring()));
    return true;
}

bool ExtensionIs(const fs::path& path, const wchar_t* extension) {
    return _wcsicmp(path.extension().c_str(), extension) == 0;
}

bool ValidStageName(std::wstring_view name) {
    if (!name.starts_with(StagePrefix)) return false;
    name.remove_prefix(std::size(StagePrefix) - 1);
    return name.size() == 38 && name.front() == L'{' && name.back() == L'}' &&
        name.substr(1, 36).find_first_not_of(L"0123456789abcdefABCDEF-") == name.npos;
}

void RemoveStage(const fs::path& directory) {
    std::error_code ignored;
    fs::remove_all(directory, ignored);
}

std::unique_ptr<StagedAppUpdate> StageUpdate(const AppRelease& release, const fs::path& exe, std::string_view bytes) {
    if (exe.filename() != AppFilename) throw std::runtime_error("The updater requires the original multipad4.exe filename.");
    const auto root = exe.parent_path();
    // Keep staged files and the installation on the same volume.
    packages::DirectoryExists(root);
    auto update = std::make_unique<StagedAppUpdate>();
    update->directory = packages::NewDirectory(root, StagePrefix);
    const auto zip = update->directory / L"package.zip";
    files::WriteAtomicChunks(zip, [&](const files::ChunkConsumer& write) { write(bytes); });
    const auto contents = packages::VerifyAndExtractZip(zip, release.hash, update->directory);
    if (FileVersion(contents / AppFilename) != ReleaseVersion(files::ToUtf8(release.tag)))
        throw std::runtime_error("The update executable does not match the release version.");
    const auto runner = update->directory / L"runner";
    fs::create_directory(runner);
    fs::copy_file(exe, runner / AppFilename);
    // The running build may need compiler runtime DLLs while it replaces its original files.
    for (const auto& item : fs::directory_iterator(root)) {
        if (ExtensionIs(item.path(), L".dll") && ExistingFile(item.path()))
            fs::copy_file(item.path(), runner / item.path().filename());
    }
    return update;
}

void ApplyUpdate(const fs::path& root, const fs::path& directory, bool& keepStaging) {
    const auto contents = directory / L"contents";
    const auto backups = directory / L"backup";
    fs::create_directory(backups);
    std::vector<packages::FileMove> moves;
    for (const auto& entry : fs::recursive_directory_iterator(contents)) {
        if (entry.is_directory()) continue;
        ExistingFile(entry.path());
        const auto relative = entry.path().lexically_relative(contents);
        const auto first = *relative.begin();
        if (relative.has_parent_path()) {
            if (first != L"config" && first != L"plugins")
                throw std::runtime_error("Unexpected update package directory.");
        } else if (first != AppFilename && first != RegistryHelperFilename &&
            first != L"Miniz.License.txt" && !ExtensionIs(first, L".dll")) {
            throw std::runtime_error("Unexpected update package file.");
        }
        auto destination = root;
        for (const auto& part : relative.parent_path()) {
            destination /= part;
            packages::DirectoryExists(destination);
        }
        if (first == L"plugins") {
            const auto folder = *std::next(relative.begin());
            if (!ValidPluginFolder(folder.native()) || !relative.parent_path().has_parent_path())
                throw std::runtime_error("Unexpected update plugin path.");
            const auto disabled = root / L"plugins" / L"disabled";
            if (packages::DirectoryExists(disabled) && packages::DirectoryExists(disabled / folder))
                destination = disabled / folder / relative.parent_path().lexically_relative(fs::path(L"plugins") / folder);
            // The redirected destination's parent chain needs the same reparse checks.
            auto parent = root;
            for (const auto& part : destination.lexically_relative(root))
                if (part != L".") packages::DirectoryExists(parent /= part);
        }
        destination /= relative.filename();
        const auto backup = ExistingFile(destination) ? backups / std::to_wstring(moves.size()) : fs::path{};
        moves.push_back({destination, entry.path(), backup});
    }
    packages::CommitMoves(moves, directory, keepStaging);
}

void StartProcess(const fs::path& exe, std::wstring arguments) {
    auto command = L"\"" + exe.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, exe.parent_path().c_str(), &startup, &process))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot start MultiPad4");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

void RestartAfterUpdate(const fs::path& directory, const fs::path& settingsPath) {
    const auto restart = std::to_wstring(GetCurrentProcessId()) + L" " + directory.filename().native();
    if (!SetEnvironmentVariableW(L"MULTIPAD4_SETTINGS", settingsPath.c_str()) ||
        !SetEnvironmentVariableW(CleanupVariable, restart.c_str()))
        throw std::runtime_error("Cannot prepare update restart.");
    StartProcess(directory.parent_path() / AppFilename, {});
}

void WaitForProcess(const wchar_t* processId) {
    wchar_t* end = nullptr;
    const auto pid = wcstoul(processId, &end, 10);
    if (!pid || *end || pid == GetCurrentProcessId()) throw std::runtime_error("Invalid updater process ID.");
    UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if ((!process && GetLastError() != ERROR_INVALID_PARAMETER) ||
        (process && WaitForSingleObject(process.get(), 15000) != WAIT_OBJECT_0))
        throw std::runtime_error("The previous MultiPad4 process did not exit.");
}

void ShowUpdateError(const std::exception& error) {
    std::wstring message;
    try { message = files::FromUtf8(error.what()); } catch (const std::exception&) { message = L"MultiPad4 could not apply the update."; }
    MessageBoxW(nullptr, message.c_str(), L"MultiPad4 Update", MB_OK | MB_ICONERROR);
}
}

StagedAppUpdate::~StagedAppUpdate() {
    if (!handedOff && !directory.empty()) RemoveStage(directory);
}

void AppUpdater::Check(bool userRequested) {
    if (check.valid()) { manual = manual || userRequested; return; }
    if (!enabled || Busy()) return;
    check = StartBackgroundRequest([] {
        constexpr auto url = L"https://api.github.com/repos/missdeer/multipad4/releases/latest";
        return SelectRelease(DownloadPackageBytes(url, 1024 * 1024), FileVersion(ExecutablePath()), MULTIPAD4_UPDATE_ASSET);
    });
    manual = userRequested;
    nextCheck = std::chrono::steady_clock::now() + std::chrono::hours(24);
}

void AppUpdater::Poll(MainFrame& frame) {
    const bool chinese = PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE;
    const auto title = chinese ? L"MultiPad4 更新" : L"MultiPad4 Update";
    try {
        if (frame.settings.Get(ID_AUTO_CHECK_UPDATES, 1) && !Busy() &&
            std::chrono::steady_clock::now() >= nextCheck) Check(false);
        const bool automatic = frame.settings.Get(ID_AUTO_CHECK_UPDATES, 1) != 0;
        if (check.valid() && (manual || !automatic || GetForegroundWindow() == frame.m_hWnd) &&
            check.wait_for(NoWait) == std::future_status::ready) {
            auto release = check.get();
            if (!manual && !automatic) return;
            if (!release) {
                if (manual) frame.MessageBox(chinese ? L"当前已是最新版本。" : L"MultiPad4 is up to date.", title, MB_OK | MB_ICONINFORMATION);
                return;
            }
            const auto prompt = chinese
                ? L"MultiPad4 " + release->tag + L" 已发布。要下载、安装并重启吗？"
                : L"MultiPad4 " + release->tag + L" is available. Download, install, and restart?";
            if (frame.MessageBox(prompt.c_str(), title, MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return;
            manual = true;
            // Keep the download in memory so closing the editor leaves no partial package on disk.
            download = StartBackgroundRequest([release = std::move(*release)]() mutable {
                auto bytes = DownloadPackageBytes(release.url, 64 * 1024 * 1024);
                return std::pair{std::move(release), std::move(bytes)};
            });
            SetWindowTextW(frame.m_hWnd, chinese ? L"MultiPad4 — 正在下载更新" : L"MultiPad4 — Downloading update");
        }
        if (download.valid() && download.wait_for(NoWait) == std::future_status::ready) {
            frame.Refresh();
            auto [release, bytes] = download.get();
            staging = std::async(std::launch::async, [release = std::move(release), bytes = std::move(bytes)] {
                return StageUpdate(release, ExecutablePath(), bytes);
            });
        }
        if (staging.valid() && staging.wait_for(NoWait) == std::future_status::ready) {
            frame.Refresh();
            manual = true;
            ready = staging.get();
            if (!frame.PostMessage(WM_CLOSE)) {
                ready.reset();
                throw std::runtime_error("Cannot close MultiPad4 for the update.");
            }
        }
    } catch (const std::exception& error) {
        if (manual) frame.Error(error);
    }
}

void LaunchAppUpdateWorker(StagedAppUpdate& update, const fs::path& settingsPath) {
    try {
        StartProcess(update.directory / L"runner" / AppFilename, L"--apply-update " +
            std::to_wstring(GetCurrentProcessId()) + L" \"" + settingsPath.native() + L"\"");
    } catch (const std::exception& error) {
        ShowUpdateError(error);
        try { RestartAfterUpdate(update.directory, settingsPath); }
        catch (const std::exception& restartError) { ShowUpdateError(restartError); }
    }
    update.handedOff = true;
}

int RunAppUpdateWorker(const wchar_t* parentPid, const fs::path& settingsPath) {
    try {
        if (!settingsPath.is_absolute()) throw std::runtime_error("Invalid update settings path.");
        const auto runner = ExecutablePath().parent_path();
        const auto directory = runner.parent_path();
        const auto root = directory.parent_path();
        if (runner.filename() != L"runner" || !ValidStageName(directory.filename().native()) ||
            !packages::DirectoryExists(directory) || !packages::DirectoryExists(runner))
            throw std::runtime_error("Invalid update staging directory.");
        WaitForProcess(parentPid);
        bool keepStaging = false;
        int result = 0;
        try {
            if (FindWindowW(L"MultiPad4.MainFrame", nullptr))
                throw std::runtime_error("Close all MultiPad4 windows before installing the update.");
            if (FileVersion(directory / L"contents" / AppFilename) > FileVersion(root / AppFilename))
                ApplyUpdate(root, directory, keepStaging);
        } catch (const std::exception& error) {
            ShowUpdateError(error);
            result = 1;
        }
        if (!keepStaging) {
            try {
                RestartAfterUpdate(directory, settingsPath);
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string(error.what()) + "\nUpdate files: " + files::ToUtf8(directory.wstring()));
            }
        }
        return result;
    } catch (const std::exception& error) {
        ShowUpdateError(error);
        return 1;
    }
}

std::optional<AppUpdateRestart> TakeAppUpdateRestart() {
    wchar_t value[100];
    const auto length = GetEnvironmentVariableW(CleanupVariable, value, _countof(value));
    if (!length) return {};
    SetEnvironmentVariableW(CleanupVariable, nullptr);
    if (length >= _countof(value)) return {};
    const std::wstring_view restart(value, length);
    const auto separator = restart.find(L' ');
    if (separator == restart.npos || !ValidStageName(restart.substr(separator + 1))) return {};
    try {
        const auto path = ExecutablePath().parent_path() / restart.substr(separator + 1);
        if (packages::DirectoryExists(path)) return AppUpdateRestart{path, std::wstring(restart.substr(0, separator))};
    } catch (const std::exception&) {}
    return {};
}

fs::path UpdateSessionPath(const MainFrame& frame, const fs::path& stage) {
    return frame.settings.path.parent_path() / (stage.filename().native() + L".session");
}

void CleanUpAppUpdate(const AppUpdateRestart& restart) {
    try {
        WaitForProcess(restart.workerPid.c_str());
        RemoveStage(restart.directory);
    } catch (const std::exception&) {}
}

#ifdef MULTIPAD4_TESTING
#include "../tests/hooks/AppUpdate.cpp.01.inc"
#endif

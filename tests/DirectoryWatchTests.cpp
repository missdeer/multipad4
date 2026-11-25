#include "DirectoryWatch.h"
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <cstdio>
#include <thread>
#include <algorithm>

namespace {
std::vector<DirectoryWatch::Change> changes;
size_t messages = 0;
struct BaselineWatch;
std::vector<BaselineWatch*> baseline;
size_t baselineMessages = 0, attributeChecks = 0;
struct BaselineWatch {
    HWND owner; HANDLE directory; PTP_IO io; OVERLAPPED overlap{}; BYTE buffer[16384]{};
    std::filesystem::path path;
    BaselineWatch(HWND owner, const std::filesystem::path& path) : owner(owner), path(path) {
        directory = CreateFileW(path.parent_path().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        RequireHandle(); io = CreateThreadpoolIo(directory, Complete, this, nullptr); if (!io) throw std::runtime_error("Baseline IO failed"); Arm();
    }
    void RequireHandle() { if (directory == INVALID_HANDLE_VALUE) throw std::runtime_error("Baseline handle failed"); }
    void Arm() { overlap = {}; StartThreadpoolIo(io); if (!ReadDirectoryChangesW(directory, buffer, sizeof(buffer), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &overlap, nullptr)) {
        CancelThreadpoolIo(io); throw std::runtime_error("Baseline arm failed"); } }
    ~BaselineWatch() { CancelIoEx(directory, &overlap); WaitForThreadpoolIoCallbacks(io, FALSE); CloseThreadpoolIo(io); CloseHandle(directory); }
    static void CALLBACK Complete(PTP_CALLBACK_INSTANCE, void* context, void*, ULONG status, ULONG_PTR, PTP_IO) {
        auto* self = static_cast<BaselineWatch*>(context); if (status != ERROR_OPERATION_ABORTED) PostMessageW(self->owner, WM_APP + 99, reinterpret_cast<WPARAM>(self), 0);
    }
};
LRESULT CALLBACK Window(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP + 99) {
        auto* watch = reinterpret_cast<BaselineWatch*>(w);
        if (std::find(baseline.begin(), baseline.end(), watch) == baseline.end()) return 0;
        watch->Arm(); ++baselineMessages;
        for (auto* snapshot : baseline) {
            const auto found = std::find(baseline.begin(), baseline.end(), snapshot);
            if (found != baseline.end() && *found == watch) {
                WIN32_FILE_ATTRIBUTE_DATA data{}; GetFileAttributesExW(watch->path.c_str(), GetFileExInfoStandard, &data); ++attributeChecks;
            }
        }
        return 0;
    }
    if (message == WM_FILE_CHANGE) {
        ++messages;
        auto batch = DirectoryWatch::Drain(window);
        changes.insert(changes.end(), batch.begin(), batch.end()); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
void Require(bool value) { if (!value) throw std::runtime_error("Directory watch regression failed"); }
void Pump(DWORD milliseconds = 100) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        if (GetTickCount64() >= end) break;
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (true);
}
void Wait(const auto& ready) {
    const auto end = GetTickCount64() + 5000;
    while (!ready()) { Require(GetTickCount64() < end); Pump(10); }
}
void Write(const std::filesystem::path& path, int value) { std::ofstream(path, std::ios::trunc) << value; }
}
int main() {
    auto path = std::filesystem::temp_directory_path() / (L"MultiPad4-watch-" + std::to_wstring(GetCurrentProcessId()));
    HWND window = nullptr;
    try {
        std::filesystem::create_directories(path);
        WNDCLASSW cls{}; cls.lpfnWndProc = Window; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"MultiPad4.WatchTests";
        Require(RegisterClassW(&cls) != 0);
        window = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr); Require(window != nullptr);
        std::vector<std::filesystem::path> files;
        for (int i = 0; i < 200; ++i) { files.push_back(path / (std::to_wstring(i) + L".txt")); Write(files.back(), 0); }
        auto unrelated = path / L"unrelated.txt"; Write(unrelated, 0);
        DWORD beforeHandles = 0, afterHandles = 0; GetProcessHandleCount(GetCurrentProcess(), &beforeHandles);
        std::vector<std::unique_ptr<DirectoryWatch>> watches;
        for (int i = 0; i < 200; ++i) { watches.push_back(std::make_unique<DirectoryWatch>(window, i + 1, files[i])); Require(watches.back()->Arm()); }
        GetProcessHandleCount(GetCurrentProcess(), &afterHandles);
        Require(afterHandles < beforeHandles + 20);
        Pump(); changes.clear(); messages = 0;
        std::vector<std::unique_ptr<BaselineWatch>> previous;
        for (const auto& file : files) { previous.push_back(std::make_unique<BaselineWatch>(window, file)); baseline.push_back(previous.back().get()); }
        Pump(); baselineMessages = 0; attributeChecks = 0;
        std::vector<double> elapsed;
        for (int i = 1; i <= 21; ++i) {
            const auto expected = baselineMessages + 200;
            const auto start = std::chrono::steady_clock::now(); Write(unrelated, i);
            Wait([&] { return baselineMessages >= expected; });
            elapsed.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(elapsed.begin(), elapsed.end());
        fprintf(stdout, "Paired old/new watchers same 200-file warm directory: baseline messages %zu attribute checks %zu; old write-to-dispatch ms median %.3f p95 %.3f max %.3f.\n",
            baselineMessages, attributeChecks, elapsed[10], elapsed[19], elapsed[20]);
        baseline.clear(); previous.clear(); Pump();
        Require(changes.empty() && messages == 0);
        fprintf(stdout, "200 subscriptions, directory handles shared: process handles +%lu; 21 unrelated writes: UI messages 0, document attribute checks 0.\n", afterHandles - beforeHandles);
        Write(files[73], 2); Wait([] { return !changes.empty(); });
        Require(std::all_of(changes.begin(), changes.end(), [](auto change) { return change.identity == 74; }));
        changes.clear();
        const auto survivor = watches.back()->token; watches.erase(watches.begin(), watches.end() - 1);
        Write(files[199], 3); Wait([&] { return std::any_of(changes.begin(), changes.end(), [&](auto c) { return c.token == survivor; }); });
        // Rename and deletion/recreation reconcile; a live subscription remains armed.
        changes.clear(); std::filesystem::rename(files[199], path / L"renamed.txt"); Wait([] { return !changes.empty(); });
        changes.clear(); Write(files[199], 4); Wait([] { return !changes.empty(); });
        // A hard-link alias created after subscription must resolve to its document.
        const auto alias = path / L"hardlink.txt";
        if (CreateHardLinkW(alias.c_str(), files[199].c_str(), nullptr)) {
            Require(watches.back()->Arm()); Pump(); changes.clear(); Write(alias, 5); Wait([] { return !changes.empty(); });
            Require(std::any_of(changes.begin(), changes.end(), [&](auto c) { return c.token == survivor; }));
        }
        // Replacing an already subscribed name with a hard link must notify both tabs.
        const auto subscribedAlias = path / L"already-open.txt"; Write(subscribedAlias, 0);
        auto aliasWatch = std::make_unique<DirectoryWatch>(window, 201, subscribedAlias); Require(aliasWatch->Arm());
        std::filesystem::remove(subscribedAlias);
        if (CreateHardLinkW(subscribedAlias.c_str(), files[199].c_str(), nullptr)) {
            // No UI pumping/rearm after relinking: lifecycle reconciliation reaches both tabs.
            changes.clear(); Write(subscribedAlias, 6);
            Wait([&] { return std::any_of(changes.begin(), changes.end(), [&](auto c) { return c.token == survivor; }) &&
                std::any_of(changes.begin(), changes.end(), [&](auto c) { return c.token == aliasWatch->token; }); });
            Require(aliasWatch->Arm()); Require(watches.back()->Arm()); Pump(); changes.clear();
            Write(subscribedAlias, 7);
            Wait([&] { return std::any_of(changes.begin(), changes.end(), [&](auto c) { return c.token == survivor; }) &&
                std::any_of(changes.begin(), changes.end(), [&](auto c) { return c.token == aliasWatch->token; }); });
        }
        // Keep failed subscribers alive while replacing their backend from the registry.
        watches.back()->FailForTest(); Require(!aliasWatch->Arm());
        auto repaired = std::make_unique<DirectoryWatch>(window, 202, files[199]); Require(repaired->Arm());
        Pump(); changes.clear(); Write(files[199], 7);
        Wait([&] { return std::any_of(changes.begin(), changes.end(), [&](auto c) { return c.token == repaired->token; }); });
        Require(std::all_of(changes.begin(), changes.end(), [&](auto c) { return c.token == repaired->token; }));
        aliasWatch.reset(); watches.back() = std::move(repaired);
        // Unknown names retain case when opening identities in a case-sensitive directory.
        const auto sensitive = path / L"case-sensitive"; std::filesystem::create_directory(sensitive);
        HANDLE dir = CreateFileW(sensitive.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        FILE_CASE_SENSITIVE_INFO info{FILE_CS_FLAG_CASE_SENSITIVE_DIR};
        const bool enabled = dir != INVALID_HANDLE_VALUE && SetFileInformationByHandle(dir, FileCaseSensitiveInfo, &info, sizeof(info));
        if (dir != INVALID_HANDLE_VALUE) CloseHandle(dir);
        fprintf(stdout, "Case-sensitive directory regression: %s.\n", enabled ? "enabled" : "unavailable on this system");
        if (enabled) {
            const auto watched = sensitive / L"watched.txt", unknown = sensitive / L"lowercase.txt";
            Write(watched, 0); Write(unknown, 0);
            DirectoryWatch caseWatch(window, 203, watched); Require(caseWatch.Arm()); Pump(); changes.clear();
            Write(unknown, 1); Pump(); Require(changes.empty());
        }
        for (int i = 0; i < 50; ++i) { Write(files[199], i); auto replacement = std::make_unique<DirectoryWatch>(window, 200, files[199]); Require(replacement->Arm()); watches.back() = std::move(replacement); }
        watches.clear(); Pump(); Require(DirectoryWatch::Drain(window).empty());
        DestroyWindow(window); std::filesystem::remove_all(path);
        puts("Shared watcher filename routing, last-subscriber lifetime, rename/recreate, aliases and teardown passed."); return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what()); if (window) DestroyWindow(window);
        std::error_code error; std::filesystem::remove_all(path, error); return 1;
    }
}

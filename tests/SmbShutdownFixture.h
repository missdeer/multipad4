#pragma once
#include "../src/FileSearch.h"
#include "../src/SearchWork.h"
#include <chrono>
#include <thread>
#include <source_location>
#include <iostream>
#include <winioctl.h>
inline bool RunSmbShutdownFixture(const std::function<void(work::SearchTask<files::FileSearchResult>&)>& shutdown) {
    auto Check = [](bool condition, std::source_location where = std::source_location::current()) {
        if (!condition) throw std::runtime_error("SMB shutdown regression at " + std::to_string(where.line()));
    };
    wchar_t root[32768]{};
    if (!GetEnvironmentVariableW(L"MULTIPAD4_SMB_TEST_ROOT", root, _countof(root))) return false;
    wchar_t localRoot[32768]{}; Check(GetEnvironmentVariableW(L"MULTIPAD4_SMB_TEST_LOCAL_ROOT", localRoot, _countof(localRoot)) != 0);
    const auto path = std::filesystem::path(root) / (L"multipad4-cancel-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".tmp");
    const auto localPath = std::filesystem::path(localRoot) / path.filename();
    struct File {
        std::filesystem::path path;
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); if (!path.empty()) DeleteFileW(path.c_str()); }
    } file;
    file.handle = CreateFileW(localPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OVERLAPPED, nullptr);
    Check(file.handle != INVALID_HANDLE_VALUE); file.path = localPath;
    const auto probe = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(probe != INVALID_HANDLE_VALUE);
    FILE_REMOTE_PROTOCOL_INFO protocol{};
    const bool remote = GetFileInformationByHandleEx(probe, FileRemoteProtocolInfo, &protocol, sizeof(protocol)) != FALSE;
    CloseHandle(probe); Check(remote && protocol.Protocol == 0x00020000);
    std::cout << "Remote protocol SMB " << protocol.ProtocolMajorVersion << '.' << protocol.ProtocolMinorVersion << '.' << protocol.ProtocolRevision << '\n';
    struct Event { HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr); ~Event() { CloseHandle(handle); } } broken;
    OVERLAPPED overlapped{}; overlapped.hEvent = broken.handle;
    DWORD returned = 0;
    REQUEST_OPLOCK_INPUT_BUFFER request{}; request.StructureVersion = REQUEST_OPLOCK_CURRENT_VERSION; request.StructureLength = sizeof(request);
    request.RequestedOplockLevel = OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_WRITE | OPLOCK_LEVEL_CACHE_HANDLE; request.Flags = REQUEST_OPLOCK_INPUT_FLAG_REQUEST;
    REQUEST_OPLOCK_OUTPUT_BUFFER output{};
    const bool immediate = DeviceIoControl(file.handle, FSCTL_REQUEST_OPLOCK, &request, sizeof(request), &output, sizeof(output), &returned, &overlapped) != FALSE;
    const auto oplockError = GetLastError();
    if (immediate || oplockError != ERROR_IO_PENDING) throw std::runtime_error("SMB oplock request failed: " + std::to_string(oplockError));
    auto payload = std::make_shared<int>(42); const auto weak = std::weak_ptr<int>(payload);
    auto outcome = std::make_shared<std::atomic<DWORD>>(ERROR_IO_PENDING);
    work::SearchTask<files::FileSearchResult> blocked(work::TrySearchPermit(), [path, payload, outcome](const auto&) {
        const auto opened = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        const auto result = opened == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
        if (*payload != 42) throw std::runtime_error("SMB payload corruption."); *outcome = result; return files::FileSearchResult{};
    });
    payload.reset();
    std::atomic<bool> acknowledged{false};
    std::jthread release([&] {
        Sleep(2000); CloseHandle(file.handle); file.handle = INVALID_HANDLE_VALUE; acknowledged = true;
    });
    Check(WaitForSingleObject(broken.handle, 5000) == WAIT_OBJECT_0 && !blocked.Ready());
    const auto shutdownStart = std::chrono::steady_clock::now();
    shutdown(blocked); blocked.Reset();
    const auto shutdownMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shutdownStart).count();
    Check(shutdownMs < 100 && !acknowledged && !work::TrySearchPermit());
    Sleep(100);
    if (*outcome == ERROR_IO_PENDING) Check(!weak.expired());
    release.join();
    const auto deadline = GetTickCount64() + 5000;
    for (;;) {
        const auto registry = work::searchDetail::RegistryOwner();
        { std::lock_guard guard(registry->mutex); if (!registry->groups) break; }
        Check(GetTickCount64() < deadline); Sleep(1);
    }
    Check(weak.expired() && *outcome != ERROR_IO_PENDING);
    std::cout << "SMB blocked-open shutdown: " << shutdownMs << " ms; retained payload and admission until acknowledgement; outcome " << outcome->load() << '\n';
    return true;
}

#pragma once
#include <windows.h>
#include <filesystem>
#include <atomic>
#include <memory>
#include <vector>

constexpr UINT WM_FILE_CHANGE = WM_APP + 0x105;

// Subscriptions share one outstanding directory read, independent of UI dispatch.
class DirectoryWatch {
public:
    struct Change { UINT_PTR identity, token; DWORD error; };
    DirectoryWatch(HWND owner, UINT_PTR document, const std::filesystem::path& path);
    ~DirectoryWatch();
    DirectoryWatch(const DirectoryWatch&) = delete;
    DirectoryWatch& operator=(const DirectoryWatch&) = delete;
    bool Arm();
    static std::vector<Change> Drain(HWND owner);
#ifdef MULTIPAD4_DIRECTORY_WATCH_TEST
    void FailForTest();
#endif
    const std::filesystem::path path;
    const UINT_PTR token;
    std::atomic<DWORD> error{ERROR_SUCCESS};
private:
    struct State;
    std::shared_ptr<State> state;
};

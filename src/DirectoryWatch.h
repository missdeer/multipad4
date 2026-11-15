#pragma once
#include <windows.h>
#include <filesystem>
#include <atomic>

constexpr UINT WM_FILE_CHANGE = WM_APP + 0x105;

// One outstanding directory read; only the UI thread arms or destroys it.
class DirectoryWatch {
public:
    DirectoryWatch(HWND owner, UINT_PTR document, const std::filesystem::path& path);
    ~DirectoryWatch();
    DirectoryWatch(const DirectoryWatch&) = delete;
    DirectoryWatch& operator=(const DirectoryWatch&) = delete;
    bool Arm();
    const std::filesystem::path path;
    const UINT_PTR token;
    std::atomic<DWORD> error{ERROR_SUCCESS};
private:
    static void CALLBACK Complete(PTP_CALLBACK_INSTANCE, void*, void*, ULONG, ULONG_PTR, PTP_IO);
    HWND owner;
    UINT_PTR document;
    HANDLE directory = INVALID_HANDLE_VALUE;
    PTP_IO io = nullptr;
    OVERLAPPED overlapped{};
    alignas(DWORD) BYTE buffer[16384]{};
};

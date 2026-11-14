#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <climits>
#include <memory>
struct AutoSaveTask;
struct DocumentRuntime {
    HANDLE changeNotification = INVALID_HANDLE_VALUE;
    ~DocumentRuntime() { if (changeNotification != INVALID_HANDLE_VALUE) FindCloseChangeNotification(changeNotification); }
    FILETIME observedWriteTime{};
    ULONGLONG observedSize = 0, lastAutoSave = 0;
    bool snapshotValid = false, externalChangePending = false;
    int monitorMode = 0;
    int lineSelectionMode = 1;
    std::wstring timestampTemplate = L"\\$Date:[^\\$]+\\$ | $Date: %Y/%m/%d %H:%M:%S $";
    std::wstring dateTimeShort, dateTimeLong;
    bool monitorLog = false, keepAtEnd = false, resetWatch = false;
    std::filesystem::path recoveryPath;
    std::vector<std::filesystem::path> recoverySnapshots;
    uint64_t lastRecoveryRevision = UINT64_MAX;
    unsigned lastRecoveryEncoding = UINT_MAX;
    int lastRecoveryEol = -1;
};
struct FrameRuntime {
    HMENU mainMenu = nullptr;
    bool menuVisible = true, fullScreen = false, trayPresent = false;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    LONG_PTR windowStyle = 0;
    bool inBackgroundWork = false;
    HWND pasteBoard = nullptr;
    DWORD clipboardSequence = 0;
    std::wstring titleOverride;
    int useSystemMRU = -1;
    unsigned operationDepth = 0;
    std::vector<std::pair<ULONG_PTR, std::wstring>> pendingCommands;
    std::vector<HWND> pendingTabCloses;
    int restoredShowCmd = SW_SHOWNORMAL;
    size_t styleCursor = 0;
    std::shared_ptr<AutoSaveTask> autoSaveTask;
    bool workTimerArmed = false;
};
struct FrameOperation {
    FrameRuntime& runtime;
    HWND owner;
    FrameOperation(FrameRuntime& state, HWND window) : runtime(state), owner(window) { ++runtime.operationDepth; }
    ~FrameOperation() {
        if (--runtime.operationDepth == 0 && (!runtime.pendingCommands.empty() || !runtime.pendingTabCloses.empty()))
            PostMessageW(owner, WM_APP + 0x104, 0, 0);
    }
};

#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <climits>
#include <cstddef>
#include <memory>
#include "DirectoryWatch.h"
#include <unordered_set>
struct AutoSaveTask;
struct DocumentRuntime {
    std::unique_ptr<DirectoryWatch> directoryWatch;
    FILETIME observedWriteTime{};
    DWORD fileAttributes = INVALID_FILE_ATTRIBUTES;
    ULONGLONG observedSize = 0, lastAutoSave = 0;
    bool snapshotValid = false, externalChangePending = false, externalCheckPending = false;
    int monitorMode = 2;
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
// Clear exhausted queues so existing empty checks always describe the live suffix.
template<class T> T TakePending(std::vector<T>& queue,size_t& head) {
    auto value=std::move(queue[head++]);
    if(head==queue.size()){queue.clear();head=0;}
    else if(head>=1024 && head>=queue.size()-head){
        queue.erase(queue.begin(),queue.begin()+static_cast<std::ptrdiff_t>(head));head=0;
    }
    return value;
}
struct FrameRuntime {
    HMENU mainMenu = nullptr;
    bool menuVisible = true, fullScreen = false, trayPresent = false;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    LONG_PTR windowStyle = 0;
    bool inBackgroundWork = false;
    std::unordered_set<UINT_PTR> pendingExternalChecks;
    HWND pasteBoard = nullptr;
    DWORD clipboardSequence = 0;
    std::wstring titleOverride;
    int useSystemMRU = -1;
    unsigned operationDepth = 0;
    std::vector<std::pair<ULONG_PTR, std::wstring>> pendingCommands;
    std::vector<HWND> pendingTabCloses;
    size_t pendingCommandHead=0,pendingTabCloseHead=0;
    void ClearPendingTabCloses(){pendingTabCloses.clear();pendingTabCloseHead=0;}
    int restoredShowCmd = SW_SHOWNORMAL;
    size_t styleCursor = 0;
    std::shared_ptr<AutoSaveTask> autoSaveTask;
    std::vector<std::shared_ptr<AutoSaveTask>> autoSaveWrites;
    size_t autoSaveCursor = 0;
    bool workTimerArmed = false;
};
struct FrameOperation {
    FrameRuntime& runtime;
    HWND owner;
    FrameOperation(FrameRuntime& state, HWND window) : runtime(state), owner(window) { ++runtime.operationDepth; }
    ~FrameOperation() {
        if (--runtime.operationDepth == 0) {
            PostMessageW(owner, WM_FILE_CHANGE, 0, 0);
            if (!runtime.pendingCommands.empty() || !runtime.pendingTabCloses.empty()) PostMessageW(owner, WM_APP + 0x104, 0, 0);
        }
    }
};

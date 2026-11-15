#include "DirectoryWatch.h"

DirectoryWatch::DirectoryWatch(HWND owner, UINT_PTR document, const std::filesystem::path& path)
    : path(path), token([] { static UINT_PTR next = 0; return ++next; }()), owner(owner), document(document) {
    directory = CreateFileW(path.parent_path().c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE) { error = GetLastError(); return; }
    io = CreateThreadpoolIo(directory, Complete, this, nullptr);
    if (!io) error = GetLastError();
}
DirectoryWatch::~DirectoryWatch() {
    if (io) {
        CancelIoEx(directory, &overlapped);
        WaitForThreadpoolIoCallbacks(io, FALSE);
        CloseThreadpoolIo(io);
    }
    if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
}
bool DirectoryWatch::Arm() {
    if (error != ERROR_SUCCESS) return false;
    overlapped = {};
    StartThreadpoolIo(io);
    if (ReadDirectoryChangesW(directory, buffer, sizeof(buffer), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
        nullptr, &overlapped, nullptr)) return true;
    error = GetLastError();
    CancelThreadpoolIo(io);
    return false;
}
void CALLBACK DirectoryWatch::Complete(PTP_CALLBACK_INSTANCE, void* context, void*, ULONG result, ULONG_PTR, PTP_IO) {
    auto& watch = *static_cast<DirectoryWatch*>(context);
    if (result == ERROR_OPERATION_ABORTED) return;
    watch.error = result == ERROR_NOTIFY_ENUM_DIR ? ERROR_SUCCESS : result;
    // No editor access on worker threads. The UI re-arms before examining disk.
    PostMessageW(watch.owner, WM_FILE_CHANGE, watch.document, watch.token);
}

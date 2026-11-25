#include "DirectoryWatch.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <algorithm>

namespace {
std::wstring Fold(std::wstring value) {
    if (!value.empty()) CharUpperBuffW(value.data(), static_cast<DWORD>(value.size()));
    return value;
}
std::string FileIdentity(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    FILE_ID_INFO info{}; const auto ok = GetFileInformationByHandleEx(file, FileIdInfo, &info, sizeof(info)); CloseHandle(file);
    return ok ? std::string(reinterpret_cast<const char*>(&info), sizeof(info)) : std::string();
}
struct Pending { bool posted = false; std::unordered_map<UINT_PTR, DirectoryWatch::Change> changes; };
std::mutex pendingMutex;
std::unordered_map<HWND, Pending> pending;
void Queue(HWND owner, DirectoryWatch::Change change) {
    std::lock_guard lock(pendingMutex);
    auto& batch = pending[owner]; batch.changes[change.token] = change;
    if (!batch.posted) batch.posted = PostMessageW(owner, WM_FILE_CHANGE, 0, 0) != FALSE;
}
}
struct DirectoryWatch::State {
    HWND owner;
    std::filesystem::path path;
    HANDLE directory = INVALID_HANDLE_VALUE;
    PTP_IO io = nullptr;
    OVERLAPPED overlapped{};
    alignas(DWORD) BYTE buffer[16384]{};
    std::mutex mutex;
    bool stopping = false, armed = false;
    DWORD error = ERROR_SUCCESS;
    std::unordered_map<UINT_PTR, Change> subscribers;
    std::unordered_map<std::wstring, std::vector<UINT_PTR>> names;
    std::unordered_map<UINT_PTR, std::string> identities;
    std::unordered_map<std::string, std::vector<UINT_PTR>> tokensByFile;
    State(HWND owner, HANDLE directory, const std::filesystem::path& path) : owner(owner), path(path), directory(directory) {
        io = CreateThreadpoolIo(directory, Complete, this, nullptr);
        if (!io) error = GetLastError();
    }
    ~State() {
        { std::lock_guard lock(mutex); stopping = true; if (io) CancelIoEx(directory, &overlapped); }
        if (io) { WaitForThreadpoolIoCallbacks(io, FALSE); CloseThreadpoolIo(io); }
        CloseHandle(directory);
    }
    bool Arm() {
        if (stopping || error != ERROR_SUCCESS) return false;
        if (armed) return true;
        overlapped = {}; StartThreadpoolIo(io);
        if (ReadDirectoryChangesW(directory, buffer, sizeof(buffer), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
            nullptr, &overlapped, nullptr)) { armed = true; return true; }
        error = GetLastError(); CancelThreadpoolIo(io); return false;
    }
    static void CALLBACK Complete(PTP_CALLBACK_INSTANCE, void* context, void*, ULONG status, ULONG_PTR bytes, PTP_IO) {
        auto& self = *static_cast<State*>(context);
        std::lock_guard lock(self.mutex);
        self.armed = false;
        if (self.stopping || status == ERROR_OPERATION_ABORTED) return;
        bool reconcile = status != ERROR_SUCCESS || bytes == 0 || bytes > sizeof(self.buffer);
        std::vector<std::wstring> changed;
        if (!reconcile) {
            size_t offset = 0;
            for (;;) {
                constexpr size_t header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
                if (offset + header > bytes) { reconcile = true; break; }
                const auto& item = *reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(self.buffer + offset);
                if (item.FileNameLength % sizeof(wchar_t) || item.FileNameLength > bytes - offset - header) { reconcile = true; break; }
                std::wstring name(item.FileName, item.FileNameLength / sizeof(wchar_t));
                if (item.Action == FILE_ACTION_RENAMED_OLD_NAME || item.Action == FILE_ACTION_RENAMED_NEW_NAME || name.find(L'~') != std::wstring::npos ||
                    ((item.Action == FILE_ACTION_ADDED || item.Action == FILE_ACTION_REMOVED) && self.names.contains(Fold(name))))
                    reconcile = true;
                changed.push_back(std::move(name));
                if (!item.NextEntryOffset) break;
                if (item.NextEntryOffset < header + item.FileNameLength || item.NextEntryOffset % alignof(DWORD) || item.NextEntryOffset > bytes - offset) { reconcile = true; break; }
                offset += item.NextEntryOffset;
            }
        }
        self.error = status == ERROR_NOTIFY_ENUM_DIR ? ERROR_SUCCESS : status;
        // Rearm under the same mutex teardown uses, before any UI or disk queries.
        if (!self.Arm()) reconcile = true;
        if (reconcile) {
            for (auto [token, change] : self.subscribers) { change.error = self.error; Queue(self.owner, change); }
        } else {
            for (const auto& name : changed) {
                const auto found = self.names.find(Fold(name));
                if (found != self.names.end()) {
                    std::unordered_set<std::string> expanded;
                    for (auto token : found->second) {
                        const auto& identity = self.identities.at(token);
                        if (identity.empty()) Queue(self.owner, self.subscribers.at(token));
                        else if (expanded.insert(identity).second)
                            for (auto sibling : self.tokensByFile.at(identity)) Queue(self.owner, self.subscribers.at(sibling));
                    }
                    continue;
                }
                // A newly introduced hard link can notify under a name not yet subscribed.
                const auto identity = FileIdentity(self.path / name);
                if (identity.empty()) {
                    for (const auto& [token, change] : self.subscribers) Queue(self.owner, change);
                    break;
                } else {
                    const auto aliases = self.tokensByFile.find(identity);
                    if (aliases != self.tokensByFile.end())
                        for (auto token : aliases->second) Queue(self.owner, self.subscribers.at(token));
                }
            }
        }
    }
};
DirectoryWatch::DirectoryWatch(HWND owner, UINT_PTR document, const std::filesystem::path& path)
    : path(path), token([] { static UINT_PTR next = 0; return ++next; }()) {
    HANDLE directory = CreateFileW(path.parent_path().c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE) { error = GetLastError(); return; }
    std::wstring key(32768, L'\0');
    auto length = GetFinalPathNameByHandleW(directory, key.data(), static_cast<DWORD>(key.size()), FILE_NAME_NORMALIZED);
    if (!length || length >= key.size()) { error = length ? ERROR_FILENAME_EXCED_RANGE : GetLastError(); CloseHandle(directory); return; }
    key.resize(length); key = Fold(std::move(key));
    // Subscriptions are created and destroyed on the UI thread; callbacks never touch the registry.
    static std::unordered_map<HWND, std::unordered_map<std::wstring, std::weak_ptr<State>>> registry;
    auto& entry = registry[owner][key]; state = entry.lock();
    if (state) {
        bool usable;
        { std::lock_guard lock(state->mutex); usable = state->error == ERROR_SUCCESS && !state->stopping; }
        if (!usable) state.reset();
    }
    if (state) CloseHandle(directory);
    else { state = std::make_shared<State>(owner, directory, path.parent_path()); entry = state; }
    std::lock_guard lock(state->mutex);
    state->subscribers.emplace(token, Change{document, token, ERROR_SUCCESS});
    const auto identity = FileIdentity(path);
    state->identities.emplace(token, identity);
    if (!identity.empty()) state->tokensByFile[identity].push_back(token);
    auto addName = [&](const std::filesystem::path& name) {
        auto& tokens = state->names[Fold(name.filename().wstring())];
        if (tokens.empty() || tokens.back() != token) tokens.push_back(token);
    };
    addName(path);
    // Include known long/short aliases and hard links in this directory.
    std::wstring alias(32768, L'\0');
    auto count = GetLongPathNameW(path.c_str(), alias.data(), static_cast<DWORD>(alias.size()));
    if (count && count < alias.size()) { alias.resize(count); addName(alias); }
    std::vector<wchar_t> link(32768); DWORD linkLength = static_cast<DWORD>(link.size());
    HANDLE links = FindFirstFileNameW(path.c_str(), 0, &linkLength, link.data());
    if (links != INVALID_HANDLE_VALUE) {
        do {
            const std::filesystem::path name(link.data());
            auto parent = name.parent_path().wstring();
            const auto longDirectory = key.size() > 6 && key[5] == L':' ? key.substr(6) : std::wstring();
            if (Fold(parent) == Fold(longDirectory)) addName(name);
            linkLength = static_cast<DWORD>(link.size());
        } while (FindNextFileNameW(links, &linkLength, link.data()));
        FindClose(links);
    }
}
DirectoryWatch::~DirectoryWatch() {
    if (!state) return;
    { std::lock_guard lock(state->mutex);
        state->subscribers.erase(token);
        const auto identity = state->identities.at(token);
        if (!identity.empty()) { auto& tokens = state->tokensByFile.at(identity); std::erase(tokens, token); if (tokens.empty()) state->tokensByFile.erase(identity); }
        state->identities.erase(token);
        for (auto& [name, tokens] : state->names) std::erase(tokens, token);
        std::erase_if(state->names, [](const auto& entry) { return entry.second.empty(); });
    }
    std::lock_guard lock(pendingMutex);
    auto found = pending.find(state->owner);
    if (found != pending.end()) { found->second.changes.erase(token); if (found->second.changes.empty()) pending.erase(found); }
}
bool DirectoryWatch::Arm() {
    if (!state) return false;
    std::lock_guard lock(state->mutex);
    const auto identity = FileIdentity(path);
    auto& previous = state->identities.at(token);
    if (previous != identity) {
        if (!previous.empty()) { auto& tokens = state->tokensByFile.at(previous); std::erase(tokens, token); if (tokens.empty()) state->tokensByFile.erase(previous); }
        previous = identity;
        if (!identity.empty()) state->tokensByFile[identity].push_back(token);
    }
    const auto ok = state->Arm(); error = state->error; return ok;
}
std::vector<DirectoryWatch::Change> DirectoryWatch::Drain(HWND owner) {
    std::lock_guard lock(pendingMutex);
    std::vector<Change> result;
    auto found = pending.find(owner);
    if (found == pending.end()) return result;
    for (const auto& [token, change] : found->second.changes) result.push_back(change);
    pending.erase(found); return result;
}

#ifdef MULTIPAD4_DIRECTORY_WATCH_TEST
void DirectoryWatch::FailForTest() {
    if (!state) return;
    { std::lock_guard lock(state->mutex); CancelIoEx(state->directory, &state->overlapped); }
    WaitForThreadpoolIoCallbacks(state->io, FALSE);
    std::lock_guard lock(state->mutex); state->error = ERROR_ACCESS_DENIED;
}
#endif

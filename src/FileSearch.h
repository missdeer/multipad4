#pragma once
#include "LazySearch.h"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <map>
#include "ThreadPool.h"

namespace files {
struct FileIdentity {
    unsigned long long volume = 0, high = 0, low = 0;
    bool operator==(const FileIdentity&) const = default;
};
std::optional<FileIdentity> QueryIdentity(const std::filesystem::path& path);
class OpenFileIndex {
    struct Hash { size_t operator()(const FileIdentity& value) const noexcept { return static_cast<size_t>(value.volume ^ value.high ^ value.low); } };
    std::vector<std::filesystem::path> paths;
    struct PathLess {
        bool operator()(const std::wstring& a, const std::wstring& b) const noexcept {
            return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
        }
    };
    std::unordered_map<FileIdentity, std::vector<size_t>, Hash> identities;
    std::map<std::wstring, std::vector<size_t>, PathLess> names;
    std::vector<size_t> unresolved;
public:
    explicit OpenFileIndex(std::vector<std::filesystem::path> open = {});
    std::optional<size_t> Find(const std::filesystem::path& path) const;
};
struct DirectorySearchOptions {
    std::filesystem::path root;
    std::wstring filter = L"*.*";
    bool recursive = true, hidden = false;
    unsigned workers = 0;
};
// Incremental depth-first traversal: memory follows directory depth, not file count.
class SearchWalker {
    DirectorySearchOptions options;
    struct Directory;
    std::vector<std::unique_ptr<Directory>> stack;
    bool started = false;
public:
    explicit SearchWalker(DirectorySearchOptions value);
    ~SearchWalker();
    std::optional<std::filesystem::path> Next(const std::atomic<bool>& cancel, size_t& errors);
};
struct FileSearchMatch { size_t start, end, line, lineStart; };
struct FileSearchResult {
    std::filesystem::path path;
    std::shared_ptr<const std::string> text;
    std::vector<FileSearchMatch> matches;
    bool openDocument = false;
    std::shared_ptr<const large::Document::Snapshot> snapshot;
    std::shared_ptr<large::HitArchive> archive;
    unsigned long long originalSize = 0, originalWriteTime = 0;
};
// Worker-only search over an immutable UTF-8 snapshot; offsets and line metadata are spooled.
FileSearchResult SearchSnapshot(large::Document::Snapshot snapshot, large::SearchOptions pattern, const std::atomic<bool>& cancel);
// Each worker callback owns its matcher. Workers never access editor windows or documents.
class FileSearch {
    SearchWalker walker;
    large::SearchOptions pattern;
    std::vector<std::filesystem::path> openPaths;
    std::unique_ptr<OpenFileIndex> openFiles;
    std::once_flag openIndexOnce;
    std::atomic<bool> cancel{false};
    std::atomic<size_t> errors{0}, running{0};
    std::mutex walkMutex, resultMutex;
    std::condition_variable space;
    std::deque<FileSearchResult> results;
    std::vector<work::Task<void>> tasks;
    void Run();
    void Publish(FileSearchResult result);
public:
    FileSearch(DirectorySearchOptions options, large::SearchOptions search,
        std::vector<std::filesystem::path> openDocuments = {});
    ~FileSearch();
    void Stop();
    bool Poll(FileSearchResult& result);
    bool Done();
    size_t Errors() const { return errors.load(); }
};
}

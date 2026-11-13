#pragma once
#include "LazySearch.h"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace files {
struct DirectorySearchOptions {
    std::filesystem::path root;
    std::wstring filter = L"*.*";
    bool recursive = true, hidden = false;
    unsigned workers = 0;
};
// Incremental depth-first traversal: memory follows directory depth, not file count.
class SearchWalker {
    DirectorySearchOptions options;
    std::vector<std::filesystem::directory_iterator> stack;
    bool started = false;
public:
    explicit SearchWalker(DirectorySearchOptions value);
    std::optional<std::filesystem::path> Next(const std::atomic<bool>& cancel, size_t& errors);
};
struct FileSearchMatch { size_t start, end, line, lineStart; };
struct FileSearchResult {
    std::filesystem::path path;
    std::shared_ptr<const std::string> text;
    std::vector<FileSearchMatch> matches;
    bool openDocument = false;
};
// Each thread owns its matcher. Workers never access editor windows or documents.
class FileSearch {
    SearchWalker walker;
    large::SearchOptions pattern;
    std::vector<std::filesystem::path> openPaths;
    std::atomic<bool> cancel{false};
    std::atomic<size_t> errors{0}, running{0};
    std::mutex walkMutex, resultMutex;
    std::condition_variable space;
    std::deque<FileSearchResult> results;
    std::vector<std::thread> threads;
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

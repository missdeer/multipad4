#include "FileSearch.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <stdexcept>

namespace files {
SearchWalker::SearchWalker(DirectorySearchOptions value) : options(std::move(value)) {
    if (options.filter.empty()) options.filter = L"*.*";
    std::replace(options.filter.begin(), options.filter.end(), L' ', L';');
}
std::optional<std::filesystem::path> SearchWalker::Next(const std::atomic<bool>& cancel, size_t& errors) {
    std::error_code error;
    if (!started) {
        started = true;
        stack.emplace_back(options.root, error);
        if (error) ++errors;
    }
    while (!stack.empty() && !cancel.load(std::memory_order_relaxed)) {
        auto& iterator = stack.back();
        if (iterator == std::filesystem::directory_iterator{}) { stack.pop_back(); continue; }
        auto entry = *iterator;
        iterator.increment(error);
        if (error) { ++errors; iterator = {}; }
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) { ++errors; continue; }
        if (!options.hidden && (attributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (options.recursive && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                stack.emplace_back(entry.path(), error);
                if (error) ++errors;
            }
            continue;
        }
        // Do not follow file symlinks either: a recursive search stays in its root.
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!entry.is_regular_file(error)) { if (error) ++errors; continue; }
        if (PathMatchSpecW(entry.path().filename().c_str(), options.filter.c_str())) return entry.path();
    }
    return {};
}
FileSearch::FileSearch(DirectorySearchOptions options, large::SearchOptions search,
    std::vector<std::filesystem::path> openDocuments)
    : walker(options), pattern(std::move(search)), openPaths(std::move(openDocuments)) {
    // Validate once on the caller before starting threads. No nested file parallelism.
    pattern.workers = 1;
    large::Search validate({}, Encoding::Utf8, pattern);
    const unsigned count = (std::min)(8u, options.workers ? options.workers : (std::max)(1u, std::thread::hardware_concurrency()));
    try {
        for (unsigned i = 0; i < count; ++i) {
            ++running;
            try { threads.emplace_back([this] { Run(); }); }
            catch (...) { --running; throw; }
        }
    } catch (...) { Stop(); throw; }
}
FileSearch::~FileSearch() { Stop(); }
void FileSearch::Stop() {
    cancel.store(true); space.notify_all();
    for (auto& thread : threads) if (thread.joinable()) thread.join();
}
void FileSearch::Publish(FileSearchResult result) {
    std::unique_lock lock(resultMutex);
    space.wait(lock, [&] { return cancel.load() || results.size() < 8; });
    if (!cancel.load()) results.push_back(std::move(result));
}
bool FileSearch::Poll(FileSearchResult& result) {
    std::lock_guard lock(resultMutex);
    if (results.empty()) return false;
    result = std::move(results.front()); results.pop_front(); space.notify_one(); return true;
}
bool FileSearch::Done() {
    std::lock_guard lock(resultMutex);
    return running.load() == 0 && results.empty();
}
void FileSearch::Run() {
    struct Complete { std::atomic<size_t>& running; ~Complete() { --running; } } complete{running};
    try {
        large::Search matcher({}, Encoding::Utf8, pattern, &cancel);
        while (!cancel.load()) {
            std::optional<std::filesystem::path> path;
            size_t walkErrors = 0;
            {
                std::lock_guard lock(walkMutex);
                path = walker.Next(cancel, walkErrors);
            }
            errors.fetch_add(walkErrors);
            if (!path) break;
            try {
                FileSearchResult result; result.path = *path;
                for (const auto& open : openPaths) {
                    std::error_code error;
                    if (std::filesystem::equivalent(open, *path, error)) { result.openDocument = true; break; }
                }
                if (result.openDocument) { Publish(std::move(result)); continue; }
                matcher.Rebase({});
                auto text = std::make_shared<std::string>();
                struct Binary {};
                ReadOptions read; read.validationWorkers = 1; read.cancel = &cancel;
                try {
                    ReadChunks(*path, [&](std::string_view chunk) {
                        if (cancel.load()) throw std::runtime_error("Search cancelled.");
                        if (chunk.find('\0') != std::string_view::npos) throw Binary{};
                        text->append(chunk);
                    }, read);
                } catch (const Binary&) { continue; }
                if (cancel.load()) break;
                auto document = large::Document::FromUtf8(text);
                matcher.Rebase(document->Capture());
                size_t position = 0, line = 0, lineStart = 0, scan = 0;
                while (position <= text->size() && !cancel.load()) {
                    const auto hit = matcher.Next(position, text->size());
                    if (!hit) break;
                    // Linear line accounting, including CRLF, bare CR and empty EOF hits.
                    while (scan < hit->start) {
                        if ((scan & 65535) == 0 && cancel.load()) throw std::runtime_error("Search cancelled.");
                        if ((*text)[scan] == '\r' && scan + 1 == hit->start && scan + 1 < text->size() && (*text)[scan + 1] == '\n') break;
                        const char c = (*text)[scan++];
                        if (c == '\r') {
                            if (scan < text->size() && (*text)[scan] == '\n') ++scan;
                            ++line; lineStart = scan;
                        } else if (c == '\n') { ++line; lineStart = scan; }
                    }
                    result.matches.push_back({static_cast<size_t>(hit->start), static_cast<size_t>(hit->end), line, lineStart});
                    position = static_cast<size_t>(hit->end);
                    if (hit->start == hit->end) {
                        if (position == text->size()) break;
                        position = static_cast<size_t>(matcher.Advance(position));
                    }
                }
                if (!result.matches.empty() && !cancel.load()) { result.text = std::move(text); Publish(std::move(result)); }
            } catch (const std::exception&) { if (!cancel.load()) ++errors; }
        }
    } catch (const std::exception&) { if (!cancel.load()) ++errors; }
}
}

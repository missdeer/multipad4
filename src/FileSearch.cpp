#include "FileSearch.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <stdexcept>

namespace files {
FileSearchResult SearchSnapshot(large::Document::Snapshot snapshot, large::SearchOptions pattern, const std::atomic<bool>& cancel) {
    FileSearchResult result;
    result.snapshot = std::make_shared<const large::Document::Snapshot>(snapshot);
    result.archive = std::make_shared<large::HitArchive>();
    pattern.workers = 1;
    large::Search matcher(snapshot, Encoding::Utf8, pattern, &cancel);
    large::Document::ReadSession reader(snapshot);
    const auto size = large::Document::Size(snapshot);
    large::Position position = 0, line = 0, lineStart = 0, scan = 0, blockStart = 0;
    std::string block;
    auto at = [&](large::Position offset) {
        if (block.empty() || offset < blockStart || offset >= blockStart + block.size()) { blockStart = offset; block = reader.Read(offset, 64 * 1024); }
        return block[static_cast<size_t>(offset - blockStart)];
    };
    while (position <= size && !cancel.load()) {
        auto hit = matcher.Next(position, size); if (!hit) break;
        while (scan < hit->start) {
            if ((scan & 65535) == 0 && cancel.load()) throw std::runtime_error("Search cancelled.");
            if (at(scan) == '\r' && scan + 1 == hit->start && scan + 1 < size && at(scan + 1) == '\n') break;
            auto c = at(scan++);
            if (c == '\r') { if (scan < size && at(scan) == '\n') ++scan; ++line; lineStart = scan; }
            else if (c == '\n') { ++line; lineStart = scan; }
        }
        hit->line = line; hit->lineStart = lineStart; result.archive->Append(*hit);
        position = hit->end;
        if (hit->start == hit->end) { if (position == size) break; position = matcher.Advance(position); }
    }
    if (cancel.load()) throw std::runtime_error("Search cancelled.");
    return result;
}
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
    // Validate once on the caller before submitting work. No nested file parallelism.
    pattern.workers = 1;
    large::Search validate({}, Encoding::Utf8, pattern);
    const unsigned count = (std::min)(8u, options.workers ? options.workers : work::Concurrency());
    try {
        tasks.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            ++running;
            try { tasks.push_back(work::Submit([this] { Run(); })); }
            catch (...) { --running; throw; }
        }
    } catch (...) { Stop(); throw; }
}
FileSearch::~FileSearch() { Stop(); }
void FileSearch::Stop() {
    cancel.store(true); space.notify_all();
    for (auto& task : tasks) task.Reset();
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
                constexpr size_t MemoryLimit = 1024 * 1024;
                auto text = std::make_shared<std::string>();
                struct Spool {
                    HANDLE file = INVALID_HANDLE_VALUE;
                    std::filesystem::path path;
                    ~Spool() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); if (!path.empty()) DeleteFileW(path.c_str()); }
                    void Open() {
                        wchar_t directory[32768]{}, name[32768]{};
                        if (!GetTempPathW(_countof(directory), directory) || !GetTempFileNameW(directory, L"MPS", 0, name)) throw std::runtime_error("Cannot create search snapshot.");
                        path = name;
                        file = CreateFileW(name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr);
                        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot write search snapshot.");
                    }
                    void Write(std::string_view bytes) {
                        DWORD written = 0;
                        if (!WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size()) throw std::runtime_error("Cannot write search snapshot.");
                    }
                } spool;
                auto times = GetTimes(*path); result.originalWriteTime = times.write;
                result.originalSize = std::filesystem::file_size(*path);
                struct Binary {};
                ReadOptions read; read.validationWorkers = 1; read.cancel = &cancel;
                try {
                    ReadChunks(*path, [&](std::string_view chunk) {
                        if (cancel.load()) throw std::runtime_error("Search cancelled.");
                        if (chunk.find('\0') != std::string_view::npos) throw Binary{};
                        if (spool.file == INVALID_HANDLE_VALUE && text->size() + chunk.size() > MemoryLimit) {
                            spool.Open(); spool.Write(*text); text = std::make_shared<std::string>();
                        }
                        if (spool.file != INVALID_HANDLE_VALUE) spool.Write(chunk); else text->append(chunk);
                    }, read);
                } catch (const Binary&) { continue; }
                if (cancel.load()) break;
                std::shared_ptr<large::Document> document;
                if (spool.file != INVALID_HANDLE_VALUE) {
                    CloseHandle(spool.file); spool.file = INVALID_HANDLE_VALUE;
                    auto mapped = std::make_shared<MappedFile>(spool.path, nullptr, true, true);
                    document = std::make_shared<large::Document>(mapped, Encoding::Utf8);
                    // The snapshot retains the handle after the temporary name is unlinked.
                } else document = large::Document::FromUtf8(text);
                const auto snapshot = document->Capture(); const auto size = document->Size();
                matcher.Rebase(snapshot);
                auto archive = std::make_shared<large::HitArchive>();
                large::Document::ReadSession reader(snapshot);
                size_t position = 0, line = 0, lineStart = 0, scan = 0;
                std::string block; size_t blockStart = 0;
                auto at = [&](size_t offset) {
                    if (block.empty() || offset < blockStart || offset >= blockStart + block.size()) {
                        blockStart = offset; block = reader.Read(offset, 64 * 1024);
                    }
                    return block[offset - blockStart];
                };
                while (position <= size && !cancel.load()) {
                    auto hit = matcher.Next(position, size);
                    if (!hit) break;
                    while (scan < hit->start) {
                        if ((scan & 65535) == 0 && cancel.load()) throw std::runtime_error("Search cancelled.");
                        if (at(scan) == '\r' && scan + 1 == hit->start && scan + 1 < size && at(scan + 1) == '\n') break;
                        const char c = at(scan++);
                        if (c == '\r') { if (scan < size && at(scan) == '\n') ++scan; ++line; lineStart = scan; }
                        else if (c == '\n') { ++line; lineStart = scan; }
                    }
                    hit->line = line; hit->lineStart = lineStart; archive->Append(*hit);
                    if (spool.path.empty() && archive->Count() <= 4096) result.matches.push_back({static_cast<size_t>(hit->start), static_cast<size_t>(hit->end), line, lineStart});
                    position = static_cast<size_t>(hit->end);
                    if (hit->start == hit->end) { if (position == size) break; position = static_cast<size_t>(matcher.Advance(position)); }
                }
                if (archive->Count() && !cancel.load()) {
                    result.snapshot = std::make_shared<const large::Document::Snapshot>(snapshot);
                    result.archive = archive;
                    if (spool.path.empty() && archive->Count() <= 4096) result.text = std::move(text);
                    else {
                        result.matches.clear(); result.matches.shrink_to_fit();
                        result.snapshot = std::make_shared<const large::Document::Snapshot>(snapshot);
                        result.archive = std::move(archive);
                    }
                    if (GetTimes(*path).write != result.originalWriteTime || std::filesystem::file_size(*path) != result.originalSize) throw std::runtime_error("File changed while searching.");
                    Publish(std::move(result));
                }
            } catch (const std::exception&) { if (!cancel.load()) ++errors; }
        }
    } catch (const std::exception&) { if (!cancel.load()) ++errors; }
}
}

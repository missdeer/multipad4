#include "FileSearch.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <stdexcept>

namespace files {
namespace {
#ifdef MULTIPAD4_FILE_STREAM_TEST
std::atomic<unsigned long long> tempSnapshotBytes{0}, decodedBytes{0};
#endif
class LiteralStream {
    std::string needle, buffer;
    const std::atomic<bool>& cancel;
    large::Position base = 0, line = 0, lineStart = 0;
    bool cr = false;
public:
    std::shared_ptr<large::HitArchive> hits = std::make_shared<large::HitArchive>();
    LiteralStream(std::string pattern, const std::atomic<bool>& stop) : needle(std::move(pattern)), cancel(stop) {}
    void Feed(std::string_view chunk) {
        if (cancel.load(std::memory_order_relaxed)) throw std::runtime_error("Search cancelled.");
        buffer.append(chunk);
        const auto safe = buffer.size() >= needle.size() ? buffer.size() - needle.size() + 1 : 0;
        size_t cursor = 0, searched = 0;
        auto locate = [&](size_t limit) {
            while (cursor < limit) {
                const auto c = buffer[cursor];
                if (cr && c != '\n') { ++line; lineStart = base + cursor; }
                if (c == '\n') { ++line; lineStart = base + cursor + 1; }
                cr = c == '\r'; ++cursor;
                if (!cr) {
                    const auto next = std::string_view(buffer).substr(cursor, limit - cursor).find_first_of("\r\n");
                    cursor += next == std::string_view::npos ? limit - cursor : next;
                }
            }
            if (cr && limit < buffer.size() && buffer[limit] != '\n') { ++line; lineStart = base + limit; cr = false; }
        };
        for (;;) {
            const auto found = buffer.find(needle, searched);
            if (found == std::string::npos || found >= safe) break;
            if ((hits->Count() & 4095) == 0 && cancel.load(std::memory_order_relaxed)) throw std::runtime_error("Search cancelled.");
            locate(found);
            large::SearchHit hit{base + found, base + found + needle.size(), {}};
            hit.line = line; hit.lineStart = lineStart; hits->Append(hit);
            searched = found + needle.size();
        }
        const auto consumed = (std::max)(safe, searched);
        locate(consumed); buffer.erase(0, consumed); base += consumed;
    }
};
class LineLocations {
    static constexpr size_t BlockBytes = 64 * 1024;
    large::Document::ReadSession reader;
    const std::atomic<bool>& cancel;
    std::string block;
    large::Position blockStart = 0, blockEnd = 0, cursor = 0, line = 0, lineStart = 0;
public:
    LineLocations(const large::Document::Snapshot& snapshot, const std::atomic<bool>& stop) : reader(snapshot), cancel(stop) {}
    void Set(large::SearchHit& hit) {
        while (cursor < hit.start) {
            if (cursor >= blockEnd) {
                if (cancel.load(std::memory_order_relaxed)) throw std::runtime_error("Search cancelled.");
                blockStart = cursor;
                block = reader.Read(cursor, BlockBytes + 1);
                blockEnd = cursor + (std::min)(block.size(), BlockBytes);
            }
            const auto offset = static_cast<size_t>(cursor - blockStart);
            const auto count = static_cast<size_t>((std::min)(hit.start, blockEnd) - cursor);
            const auto newline = std::string_view(block.data() + offset, count).find_first_of("\r\n");
            if (newline == std::string_view::npos) { cursor += count; continue; }
            cursor += newline;
            const auto index = offset + newline;
            const bool crlf = block[index] == '\r' && index + 1 < block.size() && block[index + 1] == '\n';
            if (crlf && cursor + 1 == hit.start) break;
            cursor += crlf ? 2 : 1;
            ++line;
            lineStart = cursor;
        }
        hit.line = line;
        hit.lineStart = lineStart;
    }
};
}
FileSearchResult SearchSnapshot(large::Document::Snapshot snapshot, large::SearchOptions pattern, const std::atomic<bool>& cancel) {
    FileSearchResult result;
    result.snapshot = std::make_shared<const large::Document::Snapshot>(snapshot);
    result.archive = std::make_shared<large::HitArchive>();
    pattern.workers = 1;
    large::Search matcher(snapshot, Encoding::Utf8, pattern, &cancel);
    LineLocations locations(snapshot, cancel);
    const auto size = large::Document::Size(snapshot);
    large::Position position = 0;
    while (position <= size && !cancel.load()) {
        auto hit = matcher.Next(position, size); if (!hit) break;
        locations.Set(*hit);
        result.archive->Append(*hit);
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
                const bool streamLiteral =
#ifdef MULTIPAD4_FILE_STREAM_BASELINE
                    false &&
#endif
                    !pattern.regex && pattern.matchCase && !pattern.wholeWord && !pattern.wordStart && !pattern.pattern.empty();
                // Keep this version stable until a matching file's independent snapshot is captured.
                std::unique_ptr<MappedFile> sourceGuard;
                std::unique_ptr<LiteralStream> literal;
                if (streamLiteral) {
                    sourceGuard = std::make_unique<MappedFile>(*path);
                    literal = std::make_unique<LiteralStream>(ToUtf8(pattern.pattern), cancel);
                }
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
#ifdef MULTIPAD4_FILE_STREAM_TEST
                        tempSnapshotBytes += bytes.size();
#endif
                        DWORD written = 0;
                        if (!WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size()) throw std::runtime_error("Cannot write search snapshot.");
                    }
                } spool;
                auto times = GetTimes(*path); result.originalWriteTime = times.write;
                result.originalSize = std::filesystem::file_size(*path);
                struct Binary {};
                ReadOptions read; read.validationWorkers = 1; read.cancel = &cancel;
                auto capture = [&](std::string_view chunk) {
                    if (spool.file == INVALID_HANDLE_VALUE && text->size() + chunk.size() > MemoryLimit) {
                        spool.Open(); spool.Write(*text); text = std::make_shared<std::string>();
                    }
                    if (spool.file != INVALID_HANDLE_VALUE) spool.Write(chunk); else text->append(chunk);
                };
                Encoding searchedEncoding = Encoding::Utf8;
                try {
                    searchedEncoding = ReadChunks(*path, [&](std::string_view chunk) {
#ifdef MULTIPAD4_FILE_STREAM_TEST
                        decodedBytes += chunk.size();
#endif
                        if (cancel.load()) throw std::runtime_error("Search cancelled.");
                        if (chunk.find('\0') != std::string_view::npos) throw Binary{};
                        if (literal) literal->Feed(chunk); else capture(chunk);
                    }, read);
                    if (literal) {
                        if (!literal->hits->Count()) continue;
                        ReadChunks(*path, [&](std::string_view chunk) {
#ifdef MULTIPAD4_FILE_STREAM_TEST
                            decodedBytes += chunk.size();
#endif
                            capture(chunk);
                        }, read, searchedEncoding);
                    }
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
                if (literal) archive = literal->hits;
                else {
                    LineLocations locations(snapshot, cancel);
                    large::Position position = 0;
                    while (position <= size && !cancel.load()) {
                        auto hit = matcher.Next(position, size);
                        if (!hit) break;
                        locations.Set(*hit); archive->Append(*hit);
                        position = hit->end;
                        if (hit->start == hit->end) { if (position == size) break; position = static_cast<size_t>(matcher.Advance(position)); }
                    }
                }
                if (archive->Count() && !cancel.load()) {
                    result.snapshot = std::make_shared<const large::Document::Snapshot>(snapshot);
                    result.archive = archive;
                    if (GetTimes(*path).write != result.originalWriteTime || std::filesystem::file_size(*path) != result.originalSize) throw std::runtime_error("File changed while searching.");
                    sourceGuard.reset();
                    Publish(std::move(result));
                }
            } catch (const std::exception&) { if (!cancel.load()) ++errors; }
        }
    } catch (const std::exception&) { if (!cancel.load()) ++errors; }
}
}

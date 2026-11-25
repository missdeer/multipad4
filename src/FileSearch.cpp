#include "FileSearch.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>

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
std::optional<FileIdentity> QueryIdentity(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    FILE_ID_INFO id{};
    if (GetFileInformationByHandleEx(file, FileIdInfo, &id, sizeof(id))) {
        FileIdentity value; value.volume = id.VolumeSerialNumber;
        memcpy(&value.low, id.FileId.Identifier, 8); memcpy(&value.high, id.FileId.Identifier + 8, 8);
        CloseHandle(file); return value;
    }
    CloseHandle(file); return {};
}
namespace {
std::wstring IndexedPath(const std::filesystem::path& path) {
    auto name = std::filesystem::absolute(path).lexically_normal().native();
    if (name.starts_with(L"\\\\?\\UNC\\")) name = L"\\\\" + name.substr(8);
    else if (name.starts_with(L"\\\\?\\")) name.erase(0, 4);
    return name;
}
}
OpenFileIndex::OpenFileIndex(std::vector<std::filesystem::path> open) : paths(std::move(open)) {
    for (size_t i = 0; i < paths.size(); ++i) {
        names[IndexedPath(paths[i])].push_back(i);
        if (auto id = QueryIdentity(paths[i])) identities[*id].push_back(i);
        else unresolved.push_back(i);
    }
}
std::optional<size_t> OpenFileIndex::Find(const std::filesystem::path& path) const {
    if (paths.empty()) return {};
    auto equivalent = [&](size_t index) { std::error_code error; return std::filesystem::equivalent(paths[index], path, error); };
    auto id = QueryIdentity(path);
    if (id) if (auto found = identities.find(*id); found != identities.end())
        for (const auto index : found->second) if (equivalent(index)) return index;
    // Atomic saves change IDs while the same path still belongs to its live buffer.
    if (auto found = names.find(IndexedPath(path)); found != names.end())
        for (const auto index : found->second) if (equivalent(index)) return index;
    // Retain equivalent's behavior when a provider cannot supply a stable file ID.
    for (size_t i = 0; i < (id ? unresolved.size() : paths.size()); ++i) {
        const auto index = id ? unresolved[i] : i; std::error_code error;
        if (std::filesystem::equivalent(paths[index], path, error)) return index;
    }
    return {};
}
struct SearchWalker::Directory {
    std::filesystem::path path;
    HANDLE find = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW data{};
    bool first = true;
    DWORD error = ERROR_SUCCESS;
    explicit Directory(std::filesystem::path value) : path(std::move(value)) {
        const auto query = path / L"*";
        find = FindFirstFileExW(query.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) error = GetLastError();
        if (find == INVALID_HANDLE_VALUE && (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED)) {
            find = FindFirstFileExW(query.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
            error = find == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        }
    }
    ~Directory() { if (find != INVALID_HANDLE_VALUE) FindClose(find); }
};
SearchWalker::SearchWalker(DirectorySearchOptions value) : options(std::move(value)) {
    if (options.filter.empty()) options.filter = L"*.*";
    std::replace(options.filter.begin(), options.filter.end(), L' ', L';');
}
SearchWalker::~SearchWalker() = default;
std::optional<std::filesystem::path> SearchWalker::Next(const std::atomic<bool>& cancel, size_t& errors) {
    auto push = [&](const std::filesystem::path& path) {
        auto directory = std::make_unique<Directory>(path);
        if (directory->find == INVALID_HANDLE_VALUE) { if (directory->error != ERROR_FILE_NOT_FOUND) ++errors; }
        else stack.push_back(std::move(directory));
    };
    if (!started) { started = true; push(options.root); }
    while (!stack.empty() && !cancel.load(std::memory_order_relaxed)) {
        auto& directory = *stack.back();
        if (!directory.first && !FindNextFileW(directory.find, &directory.data)) {
            if (GetLastError() != ERROR_NO_MORE_FILES) ++errors;
            stack.pop_back(); continue;
        }
        directory.first = false;
        const auto& data = directory.data;
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        if (!options.hidden && (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (options.recursive) push(directory.path / data.cFileName); continue; }
        if (PathMatchSpecW(data.cFileName, options.filter.c_str())) return directory.path / data.cFileName;
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
        std::call_once(openIndexOnce, [&] { openFiles = std::make_unique<OpenFileIndex>(openPaths); });
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
                if (auto index = openFiles->Find(*path)) {
                    std::error_code error;
                    result.openDocument = std::filesystem::equivalent(openPaths[*index], *path, error);
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

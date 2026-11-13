#pragma once
#include "FileIO.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
namespace large {
using Position = uint64_t;
struct Selection { Position caret = 0, anchor = 0; };
class Document {
    struct Node;
    struct Impl;
    std::unique_ptr<Impl> impl;
    Document();
public:
    struct Snapshot { std::shared_ptr<const Node> root; };
    class ReadSession {
        struct Impl; std::unique_ptr<Impl> impl;
    public:
        explicit ReadSession(Snapshot snapshot);
        ~ReadSession();
        std::string Read(Position offset, size_t length);
    };
    struct Metrics { Position sourceBytesRead = 0, addedBytes = 0, indexedBlocks = 0, allocatedNodes = 0; };
    Document(std::shared_ptr<files::MappedFile> file, files::Encoding encoding, std::string signature = {});
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Snapshot Capture() const;
    Position Size() const;
    static Position Size(const Snapshot& snapshot);
    std::string Read(Position offset, size_t length) const;
    static std::string Read(const Snapshot& snapshot, Position offset, size_t length);
    void Begin(Selection selection);
    void Replace(Position offset, Position length, std::string_view nativeBytes);
    void End(Selection selection);
    void Abort();
    bool CanUndo() const;
    bool CanRedo() const;
    std::optional<Selection> Undo();
    std::optional<Selection> Redo();
    bool Modified() const;
    void MarkSaved();
    std::shared_ptr<Document> Branch(const Snapshot& snapshot) const;
    void Commit(const Snapshot& expected, const Snapshot& changed, Selection before, Selection after);
    void Save(const std::filesystem::path& path, bool copy);
    Metrics Statistics() const;
    // Explicit queries materialize only the index blocks needed by the query.
    static Position LineCount(const Snapshot& snapshot, const std::atomic<bool>* cancel = nullptr);
    static Position PositionFromLine(const Snapshot& snapshot, Position line, const std::atomic<bool>* cancel = nullptr);
    static Position LineFromPosition(const Snapshot& snapshot, Position position, const std::atomic<bool>* cancel = nullptr);
    struct Match { Position position = 0; size_t length = 0; };
    static std::optional<Match> Find(const Snapshot& snapshot, std::string_view nativeNeedle,
        Position start, Position limit, bool backwards, const std::atomic<bool>* cancel = nullptr);
};
}

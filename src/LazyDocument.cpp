#include "LazyDocument.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <limits>
#include <array>
#include <cstring>
#include "VectorISA.h"
namespace large {
namespace {
constexpr size_t IndexBlockBytes = 64 * 1024;
#ifdef MULTIPAD4_PIECE_READ_TEST
#include "../tests/hooks/LazyDocument.cpp.01.inc"
#endif
thread_local const std::atomic<bool>* cancellation = nullptr;
struct CancelScope {
    const std::atomic<bool>* previous = cancellation;
    explicit CancelScope(const std::atomic<bool>* value) { cancellation = value; }
    ~CancelScope() { cancellation = previous; }
};
void CheckCancel() { if (cancellation && cancellation->load(std::memory_order_relaxed)) throw std::runtime_error("Operation cancelled."); }
struct Summary { Position bytes = 0, breaks = 0; unsigned first = 0, last = 0; };
Summary Join(Summary a, Summary b) {
    if (!a.bytes) return b; if (!b.bytes) return a;
    return {a.bytes + b.bytes, a.breaks + b.breaks - (a.last == '\r' && b.first == '\n'), a.first, b.last};
}
unsigned Unit(std::string_view bytes, size_t offset, unsigned width, bool little) {
    auto a = static_cast<unsigned char>(bytes[offset]);
    if (width == 1) return a;
    auto b = static_cast<unsigned char>(bytes[offset + 1]); return little ? a | (b << 8) : (a << 8) | b;
}
constexpr size_t AppendBlockBytes = 64 * 1024;
#ifdef MULTIPAD4_APPEND_TEST
#include "../tests/hooks/LazyDocument.cpp.02.inc"
#endif
class Journal {
    HANDLE file = INVALID_HANDLE_VALUE;
    Position size = 0, persisted = 0;
    std::array<char, AppendBlockBytes> pending{};
    size_t used = 0;
    std::mutex mutex;
    void Persist(std::string_view bytes) {
        LARGE_INTEGER position{}; position.QuadPart = persisted;
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) throw std::runtime_error("Cannot seek edit journal.");
        try {
            for (size_t offset = 0; offset < bytes.size();) {
                DWORD written = 0, count = static_cast<DWORD>((std::min)(bytes.size() - offset, size_t(16 * 1024 * 1024)));
#ifdef MULTIPAD4_APPEND_TEST
#include "../tests/hooks/LazyDocument.cpp.03.inc"
#endif
                if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || !written) throw std::runtime_error("Cannot write edit journal.");
                offset += written;
            }
        } catch (...) { SetFilePointerEx(file, position, nullptr, FILE_BEGIN); SetEndOfFile(file); throw; }
        persisted += bytes.size();
    }
    void Flush() { if (used) { Persist(std::string_view(pending.data(), used)); used = 0; } }

public:
    Journal() {
        wchar_t directory[32768]{}, name[32768]{};
        if (!GetTempPathW(_countof(directory), directory) || !GetTempFileNameW(directory, L"MP4", 0, name)) throw std::runtime_error("Cannot create edit journal.");
        file = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file == INVALID_HANDLE_VALUE) { DeleteFileW(name); throw std::runtime_error("Cannot open edit journal."); }
    }
    ~Journal() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    Position Append(std::string_view bytes) {
        std::lock_guard guard(mutex); const auto begin = size;
#ifdef MULTIPAD4_APPEND_BASELINE
#include "../tests/hooks/LazyDocument.cpp.04.inc"
#else
        if (bytes.size() > pending.size() - used) Flush();
        if (bytes.size() >= pending.size()) Persist(bytes);
        else { memcpy(pending.data() + used, bytes.data(), bytes.size()); used += bytes.size(); }
#endif
        size += bytes.size(); return begin;
    }
    HANDLE ReadHandle() {
        const auto reader = ReOpenFile(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
        if (reader == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot reopen edit journal."); return reader;
    }
    void ReadInto(Position offset, size_t length, char* output, HANDLE reader = INVALID_HANDLE_VALUE) {
        std::unique_lock guard(mutex);
        if (offset > size || length > size - offset) throw std::runtime_error("Invalid journal range.");
        const auto diskLength = offset < persisted ? static_cast<size_t>((std::min)(Position(length), persisted - offset)) : 0;
        if (length > diskLength) memcpy(output + diskLength, pending.data() + static_cast<size_t>(offset + diskLength - persisted), length - diskLength);
        if (reader != INVALID_HANDLE_VALUE) guard.unlock(); else reader = file;
        if (diskLength) {
            LARGE_INTEGER position{}; position.QuadPart = offset;
            if (!SetFilePointerEx(reader, position, nullptr, FILE_BEGIN)) throw std::runtime_error("Cannot seek edit journal.");
        }
        for (size_t done = 0; done < diskLength;) {
            CheckCancel();
            DWORD got = 0, count = static_cast<DWORD>((std::min)(diskLength - done, size_t(16 * 1024 * 1024)));
            if (!ReadFile(reader, output + done, count, &got, nullptr) || !got) throw std::runtime_error("Cannot read edit journal."); done += got;
        }
    }
    std::string Read(Position offset, size_t length, HANDLE reader = INVALID_HANDLE_VALUE) {
        std::string bytes(length, '\0'); ReadInto(offset, length, bytes.data(), reader); return bytes;
    }
};
struct Counters { std::atomic<Position> read{0}, added{0}, blocks{0}, nodes{0}; };
class Source;
struct JournalSession {
    HANDLE file = INVALID_HANDLE_VALUE;
    ~JournalSession() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};
struct ReadSessions {
    std::map<const Source*, std::unique_ptr<files::MappedFile>> mapped;
    std::map<const Journal*, JournalSession> journals;
};
class Source {
    struct Block { std::vector<uint32_t> ends; Summary summary; };
    struct Index { std::mutex mutex; std::optional<Summary> summary; std::unique_ptr<Index> left, right; std::unique_ptr<Block> block; };
    std::shared_ptr<files::MappedFile> file;
    std::shared_ptr<Journal> journal;
    std::shared_ptr<const std::string> memory;
    Position journalBegin = 0;
    std::shared_ptr<Counters> counters;
    std::mutex readMutex;
    Index index;
    Position blockCount;
    unsigned width;
    bool little;
    unsigned Character(Position offset, ReadSessions& sessions) {
        auto bytes = Read(offset, width, &sessions); return Unit(bytes, 0, width, little);
    }
    Block Build(Position number, ReadSessions& sessions) {
        CheckCancel(); auto begin = number * IndexBlockBytes;
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
#include "../tests/hooks/LazyDocument.cpp.05.inc"
#endif
        const auto count = static_cast<size_t>((std::min)(Position(IndexBlockBytes), size - begin));
        auto bytes = Read(begin, count, &sessions);
        Block block; block.summary.bytes = bytes.size();
        if (!bytes.empty()) { block.summary.first = Unit(bytes, 0, width, little); block.summary.last = Unit(bytes, bytes.size() - width, width, little); }
        auto addBreak = [&](size_t offset) {
            auto c = Unit(bytes, offset, width, little);
            if (c == '\n' && offset && Unit(bytes, offset - width, width, little) == '\r') block.ends.back() = static_cast<uint32_t>(offset + width);
            else block.ends.push_back(static_cast<uint32_t>(offset + width));
        };
        if (width == 1) {
            const auto cr = _mm_set1_epi8('\r'), lf = _mm_set1_epi8('\n');
            size_t offset = 0;
            for (; offset + 16 <= bytes.size(); offset += 16) {
                const auto blockBytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes.data() + offset));
                unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(blockBytes, cr), _mm_cmpeq_epi8(blockBytes, lf))));
                while (mask) { unsigned long bit; _BitScanForward(&bit, mask); addBreak(offset + bit); mask &= mask - 1; }
            }
            for (; offset < bytes.size(); ++offset) if (bytes[offset] == '\r' || bytes[offset] == '\n') addBreak(offset);
        } else {
            const auto cr = _mm_set1_epi16(little ? '\r' : ('\r' << 8)), lf = _mm_set1_epi16(little ? '\n' : ('\n' << 8));
            size_t offset = 0;
            for (; offset + 16 <= bytes.size(); offset += 16) {
                auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes.data() + offset));
                unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi16(value, cr), _mm_cmpeq_epi16(value, lf)))) & 0x5555u;
                while (mask) { unsigned long bit; _BitScanForward(&bit, mask); addBreak(offset + bit); mask &= mask - 1; }
            }
            for (; offset < bytes.size(); offset += 2) { auto c = Unit(bytes, offset, width, little); if (c == '\r' || c == '\n') addBreak(offset); }
        }
        block.summary.breaks = block.ends.size(); ++counters->blocks; return block;
    }
    Summary Query(Index& tree, Position first, Position last, Position begin, Position end, ReadSessions& sessions) {
        CheckCancel(); const auto regionBegin = first * IndexBlockBytes, regionEnd = (std::min)(size, last * IndexBlockBytes);
        if (end <= regionBegin || begin >= regionEnd) return {};
        const bool full = begin <= regionBegin && end >= regionEnd;
        { std::lock_guard guard(tree.mutex); if (full && tree.summary) return *tree.summary; }
        if (last - first == 1) {
            const Block* block;
            { std::lock_guard guard(tree.mutex); block = tree.block.get(); }
            if (!block) {
                auto built = std::make_unique<Block>(Build(first, sessions));
                std::lock_guard guard(tree.mutex);
                if (!tree.block) { tree.block = std::move(built); tree.summary = tree.block->summary; }
                block = tree.block.get();
            }
            if (full) return block->summary;
            auto left = (std::max)(begin, regionBegin), right = (std::min)(end, regionEnd);
            auto lo = static_cast<uint32_t>(left - regionBegin), hi = static_cast<uint32_t>(right - regionBegin);
            const auto& ends = block->ends;
            Summary result{right - left, static_cast<Position>(std::upper_bound(ends.begin(), ends.end(), hi) - std::upper_bound(ends.begin(), ends.end(), lo)), Character(left, sessions), Character(right - width, sessions)};
            if (right < regionEnd && result.last == '\r' && Character(right, sessions) == '\n') ++result.breaks;
            return result;
        }
        const auto middle = first + (last - first) / 2;
        { std::lock_guard guard(tree.mutex);
            if (!tree.left) tree.left = std::make_unique<Index>(); if (!tree.right) tree.right = std::make_unique<Index>();
        }
        auto result = Join(Query(*tree.left, first, middle, begin, end, sessions), Query(*tree.right, middle, last, begin, end, sessions));
        if (full) { std::lock_guard guard(tree.mutex); tree.summary = result; }
        return result;
    }
    std::optional<Position> Select(Index& tree, Position first, Position last, Position begin, Position end, Position line, Summary& prefix, ReadSessions& sessions) {
        CheckCancel();
        const auto regionBegin = first * IndexBlockBytes, regionEnd = (std::min)(size, last * IndexBlockBytes);
        if (end <= regionBegin || begin >= regionEnd) return {};
        const bool full = begin <= regionBegin && end >= regionEnd;
        { std::lock_guard guard(tree.mutex); if (full && tree.summary && Join(prefix, *tree.summary).breaks < line) { prefix = Join(prefix, *tree.summary); return {}; } }
        if (last - first == 1) {
            const auto left = (std::max)(begin, regionBegin), right = (std::min)(end, regionEnd);
            const auto summary = Query(tree, first, last, left, right, sessions);
            const auto combined = Join(prefix, summary);
            if (combined.breaks < line) { prefix = combined; return {}; }
            const auto& ends = tree.block->ends;
            auto candidate = std::upper_bound(ends.begin(), ends.end(), static_cast<uint32_t>(left - regionBegin));
            auto ordinal = line - prefix.breaks;
            if (prefix.bytes && prefix.last == '\r' && summary.first == '\n') ++ordinal;
            const auto remaining = static_cast<Position>(ends.end() - candidate);
            // A piece ending between CR and LF has a break not yet in ends.
            if (ordinal > remaining || regionBegin + candidate[static_cast<size_t>(ordinal - 1)] > right) return right;
            return regionBegin + candidate[static_cast<size_t>(ordinal - 1)];
        }
        const auto middle = first + (last - first) / 2;
        { std::lock_guard guard(tree.mutex); if (!tree.left) tree.left = std::make_unique<Index>(); if (!tree.right) tree.right = std::make_unique<Index>(); }
        auto result = Select(*tree.left, first, middle, begin, end, line, prefix, sessions);
        if (!result) result = Select(*tree.right, middle, last, begin, end, line, prefix, sessions);
        if (full) {
            std::scoped_lock guard(tree.mutex, tree.left->mutex, tree.right->mutex);
            if (tree.left->summary && tree.right->summary) tree.summary = Join(*tree.left->summary, *tree.right->summary);
        }
        return result;
    }

public:
    unsigned Width() const { return width; }
    bool Little() const { return little; }
    const Position size;
    Source(std::shared_ptr<files::MappedFile> original, unsigned unitWidth, bool isLittle, std::shared_ptr<Counters> values) :
        file(std::move(original)), counters(std::move(values)), blockCount((file->Size() + IndexBlockBytes - 1) / IndexBlockBytes), width(unitWidth), little(isLittle), size(file->Size()) {}
    Source(std::shared_ptr<Journal> changes, Position begin, Position length, unsigned unitWidth, bool isLittle, std::shared_ptr<Counters> values) :
        journal(std::move(changes)), journalBegin(begin), counters(std::move(values)), blockCount((length + IndexBlockBytes - 1) / IndexBlockBytes), width(unitWidth), little(isLittle), size(length) {}
    Source(std::shared_ptr<const std::string> text, std::shared_ptr<Counters> values) :
        memory(std::move(text)), counters(std::move(values)), blockCount((memory->size() + IndexBlockBytes - 1) / IndexBlockBytes), width(1), little(true), size(memory->size()) {}
    std::shared_ptr<Source> Combined(Position offset, Position length, const Source& next, Position nextOffset, Position nextLength) {
        if (!journal || journal != next.journal || journalBegin + offset + length != next.journalBegin + nextOffset) return {};
        const auto first = journalBegin + offset, last = first + length + nextLength;
        if (first / AppendBlockBytes != (last - 1) / AppendBlockBytes) return {};
        return std::make_shared<Source>(journal, first, length + nextLength, width, little, counters);
    }
    bool Contiguous(Position offset, Position length, const Source& next, Position nextOffset) const {
        if (journal && journal == next.journal) return journalBegin + offset + length == next.journalBegin + nextOffset;
        if (file && file == next.file) return offset + length == nextOffset;
        return this == &next && offset + length == nextOffset;
    }
    void ReadInto(Position offset, size_t count, char* output, ReadSessions* sessions) {
#ifdef MULTIPAD4_PIECE_READ_TEST
#include "../tests/hooks/LazyDocument.cpp.06.inc"
#endif
        // Coalesced journal runs may extend past this Source into the same backing journal.
        if (journal) {
            HANDLE reader = INVALID_HANDLE_VALUE;
            if (sessions) { auto& session = sessions->journals[journal.get()]; if (session.file == INVALID_HANDLE_VALUE) session.file = journal->ReadHandle(); reader = session.file; }
            journal->ReadInto(journalBegin + offset, count, output, reader); return;
        }
        if (offset > size || count > size - offset) throw std::runtime_error("Invalid source range.");
        if (memory) { memcpy(output, memory->data() + static_cast<size_t>(offset), count); return; }
        auto fill = [&](files::MappedFile& mapped) {
            for (size_t done = 0; done < count;) {
                const auto take = (std::min)(count - done, size_t(16 * 1024 * 1024));
#ifdef MULTIPAD4_PIECE_READ_TEST
#include "../tests/hooks/LazyDocument.cpp.07.inc"
#endif
                mapped.Visit(offset + done, take, [&](std::string_view bytes) { memcpy(output + done, bytes.data(), bytes.size()); });
                done += take;
            }
        };
        if (sessions) {
            auto& session = sessions->mapped[this]; if (!session) session = file->NewReadSession();
            fill(*session);
        } else {
            std::lock_guard guard(readMutex); fill(*file);
        }
        counters->read += count;
    }
    std::string Read(Position offset, size_t count, ReadSessions* sessions = nullptr) {
        if (offset > size || count > size - offset) throw std::runtime_error("Invalid source range.");
        if (memory) return memory->substr(static_cast<size_t>(offset), count);
        if (file) {
            if (sessions) {
                auto& session = sessions->mapped[this]; if (!session) session = file->NewReadSession();
                auto bytes = session->Read(offset, count); counters->read += bytes.size(); return bytes;
            }
            std::lock_guard guard(readMutex); auto bytes = file->Read(offset, count); counters->read += bytes.size(); return bytes;
        }
        if (sessions) {
            auto& reader = sessions->journals[journal.get()].file; if (reader == INVALID_HANDLE_VALUE) reader = journal->ReadHandle();
            return journal->Read(journalBegin + offset, count, reader);
        }
        return journal->Read(journalBegin + offset, count);
    }
    void Stream(Position offset, Position length, const files::ChunkConsumer& consume) {
        while (length) {
            CheckCancel(); const auto count = static_cast<size_t>((std::min)(length, Position(16 * 1024 * 1024)));
            if (file) { std::lock_guard guard(readMutex); file->Visit(offset, count, consume); counters->read += count; }
            else if (memory) consume(std::string_view(*memory).substr(static_cast<size_t>(offset), count));
            else { auto bytes = journal->Read(journalBegin + offset, count); consume(bytes); }
            offset += count; length -= count;
        }
    }
    Summary Describe(Position offset, Position length, ReadSessions* shared = nullptr) {
        if (!length) return {};
        if (shared) return Query(index, 0, blockCount, offset, offset + length, *shared);
        ReadSessions sessions; return Query(index, 0, blockCount, offset, offset + length, sessions);
    }
    std::optional<Position> Select(Position offset, Position length, Position line, Summary& prefix, ReadSessions& sessions) {
        if (!length) return {};
        return Select(index, 0, blockCount, offset, offset + length, line, prefix, sessions);
    }
};
struct Piece { std::shared_ptr<Source> source; Position offset = 0, length = 0; };
}
struct Document::Node {
    Piece piece;
    std::shared_ptr<const Node> left, right;
    const Position bytes;
    const uint64_t priority;
    mutable std::mutex mutex;
    mutable std::optional<Summary> summary;
    Node(Piece value, std::shared_ptr<const Node> a, std::shared_ptr<const Node> b, uint64_t order) : piece(std::move(value)), left(std::move(a)), right(std::move(b)), bytes((left ? left->bytes : 0) + piece.length + (right ? right->bytes : 0)), priority(order) {}
};
struct Document::Impl {
    using Root = std::shared_ptr<const Node>;
    std::shared_ptr<Counters> counters = std::make_shared<Counters>();
    std::shared_ptr<Journal> journal;
    Root root, saved;
    unsigned width;
    bool little;
    std::string signature;
    struct Revision { Root before, after; Selection beforeSelection, afterSelection; };
    std::vector<Revision> undo, redo;
    unsigned depth = 0;
    Root groupRoot;
    Selection groupSelection;
    uint64_t random = 0xD1B54A32D192ED03ULL;
    uint64_t Priority() { random ^= random >> 12; random ^= random << 25; random ^= random >> 27; return random * 0x2545F4914F6CDD1DULL; }
    static Position Length(const Root& node) { return node ? node->bytes : 0; }
    Root Make(Piece piece, Root left = {}, Root right = {}, std::optional<uint64_t> priority = {}) {
        ++counters->nodes; return std::make_shared<Node>(std::move(piece), std::move(left), std::move(right), priority ? *priority : Priority());
    }
    Root Merge(Root a, Root b) {
        if (!a) return b; if (!b) return a;
        if (a->priority >= b->priority) return Make(a->piece, a->left, Merge(a->right, b), a->priority);
        return Make(b->piece, Merge(a, b->left), b->right, b->priority);
    }
    std::pair<Root, Root> Split(Root node, Position position) {
        if (!node) return {};
        const auto left = Length(node->left), end = left + node->piece.length;
        if (position < left) { auto [a, b] = Split(node->left, position); return {a, Make(node->piece, b, node->right, node->priority)}; }
        if (position > end) { auto [a, b] = Split(node->right, position - end); return {Make(node->piece, node->left, a, node->priority), b}; }
        if (position == left) return {node->left, Make(node->piece, {}, node->right, node->priority)};
        if (position == end) return {Make(node->piece, node->left, {}, node->priority), node->right};
        auto first = node->piece, second = first; first.length = position - left; second.offset += first.length; second.length -= first.length;
        return {Merge(node->left, Make(first)), Merge(Make(second), node->right)};
    }
    Root PopLast(const Root& node) { return node->right ? Make(node->piece, node->left, PopLast(node->right), node->priority) : node->left; }
    Root PopFirst(const Root& node) { return node->left ? Make(node->piece, PopFirst(node->left), node->right, node->priority) : node->right; }
    Root Concat(Root a, Root b) {
#ifdef MULTIPAD4_APPEND_BASELINE
#include "../tests/hooks/LazyDocument.cpp.08.inc"
#else
        if (!a) return b; if (!b) return a;
        auto last = a.get(), first = b.get();
        while (last->right) last = last->right.get();
        while (first->left) first = first->left.get();
        auto piece = last->piece;
        if (piece.source == first->piece.source && piece.offset + piece.length == first->piece.offset) piece.length += first->piece.length;
        else {
            auto combined = piece.source->Combined(piece.offset, piece.length, *first->piece.source, first->piece.offset, first->piece.length);
            if (!combined) return Merge(a, b);
            piece = {std::move(combined), 0, piece.length + first->piece.length};
        }
        return Merge(Merge(PopLast(a), Make(std::move(piece))), PopFirst(b));
#endif
    }
#ifdef MULTIPAD4_PIECE_READ_BASELINE
#include "../tests/hooks/LazyDocument.cpp.09.inc"
#else
    struct ReadBuffer {
        char* output;
        ReadSessions* sessions;
        Source* source = nullptr;
        Position offset = 0, length = 0;
        void Flush() {
            if (!length) return;
            source->ReadInto(offset, static_cast<size_t>(length), output, sessions);
            output += length; length = 0;
        }
        void Add(const Piece& piece, Position start, Position count) {
            if (length && !source->Contiguous(offset, length, *piece.source, start)) Flush();
            if (!length) { source = piece.source.get(); offset = start; }
            length += count;
        }
    };
    static void Append(const Root& node, Position offset, Position length, ReadBuffer& buffer) {
        if (!node || !length) return;
        const auto left = Length(node->left), end = left + node->piece.length;
        if (offset < left) { auto take = (std::min)(length, left - offset); Append(node->left, offset, take, buffer); offset += take; length -= take; }
        if (length && offset < end) { auto start = offset - left, take = (std::min)(length, end - offset); buffer.Add(node->piece, node->piece.offset + start, take); offset += take; length -= take; }
        if (length) Append(node->right, offset - end, length, buffer);
    }
    static void Append(const Root& node, Position offset, Position length, std::string& output, ReadSessions* sessions = nullptr) {
        output.resize(static_cast<size_t>(length));
        ReadBuffer buffer{output.data(), sessions}; Append(node, offset, length, buffer); buffer.Flush();
    }
#endif
    static void Stream(const Root& node, const files::ChunkConsumer& consume) {
        if (!node) return;
        Stream(node->left, consume); node->piece.source->Stream(node->piece.offset, node->piece.length, consume); Stream(node->right, consume);
    }
    static Summary Describe(const Root& node, ReadSessions* sessions = nullptr) {
        if (!node) return {};
        CheckCancel();
        { std::lock_guard guard(node->mutex); if (node->summary) return *node->summary; }
        const auto summary = Join(Join(Describe(node->left, sessions), node->piece.source->Describe(node->piece.offset, node->piece.length, sessions)), Describe(node->right, sessions));
        std::lock_guard guard(node->mutex); if (!node->summary) node->summary = summary; return *node->summary;
    }
    static Summary Prefix(const Root& node, Position length, ReadSessions& sessions) {
        if (!node || !length) return {};
        if (length >= node->bytes) return Describe(node, &sessions);
        auto left = Length(node->left);
        if (length <= left) return Prefix(node->left, length, sessions);
        auto result = Describe(node->left, &sessions); auto count = (std::min)(length - left, node->piece.length);
        result = Join(result, node->piece.source->Describe(node->piece.offset, count, &sessions));
        if (length > left + count) result = Join(result, Prefix(node->right, length - left - count, sessions));
        return result;
    }
    static std::optional<Position> SelectLine(const Root& node, Position base, Position line, Summary& prefix, ReadSessions& sessions) {
        if (!node) return {};
        CheckCancel();
        {
            std::lock_guard guard(node->mutex);
            if (node->summary && Join(prefix, *node->summary).breaks < line) { prefix = Join(prefix, *node->summary); return {}; }
        }
        if (auto found = SelectLine(node->left, base, line, prefix, sessions)) return found;
        const auto pieceBase = base + Length(node->left);
        if (auto found = node->piece.source->Select(node->piece.offset, node->piece.length, line, prefix, sessions)) return pieceBase + *found - node->piece.offset;
        auto found = SelectLine(node->right, pieceBase + node->piece.length, line, prefix, sessions);
        if (!found) Describe(node, &sessions); // All source blocks in this subtree are now cached.
        return found;
    }
};
Document::Document(std::shared_ptr<files::MappedFile> file, files::Encoding encoding, std::string signature) : impl(std::make_unique<Impl>()) {
    impl->width = encoding == files::Encoding::Utf16LE || encoding == files::Encoding::Utf16LEPlain || encoding == files::Encoding::Utf16BE || encoding == files::Encoding::Utf16BEPlain ? 2 : 1;
    impl->little = encoding != files::Encoding::Utf16BE && encoding != files::Encoding::Utf16BEPlain;
    impl->signature = std::move(signature);
    // Include the encoding probe already read through the shared file handle.
    impl->counters->read = file->BytesRead();
    const auto skip = impl->signature.size();
    if (skip > file->Size() || (file->Size() - skip) % impl->width) throw std::runtime_error("Truncated encoded file.");
    auto source = std::make_shared<Source>(file, impl->width, impl->little, impl->counters);
    if (file->Size() > skip) impl->root = impl->Make({source, skip, file->Size() - skip});
    impl->saved = impl->root;
}
Document::Document() : impl(std::make_unique<Impl>()) {}
std::shared_ptr<Document> Document::FromUtf8(std::shared_ptr<const std::string> text) {
    auto document = std::shared_ptr<Document>(new Document);
    if (!text) throw std::invalid_argument("Missing UTF-8 text.");
    document->impl->width = 1; document->impl->little = true;
    auto source = std::make_shared<Source>(std::move(text), document->impl->counters);
    if (source->size) document->impl->root = document->impl->Make({source, 0, source->size});
    document->MarkSaved();
    return document;
}
std::shared_ptr<Document> Document::Branch(const Snapshot& snapshot) const {
    auto branch = std::shared_ptr<Document>(new Document);
    branch->impl->root = snapshot.root; branch->impl->saved = snapshot.root; branch->impl->counters = impl->counters;
    branch->impl->width = impl->width; branch->impl->little = impl->little; branch->impl->signature = impl->signature;
    return branch;
}
void Document::Commit(const Snapshot& expected, const Snapshot& changed, Selection before, Selection after) {
    if (impl->depth || impl->root != expected.root) throw std::runtime_error("The document changed while searching. Run the operation again.");
    if (expected.root == changed.root) return;
    impl->undo.push_back({expected.root, changed.root, before, after}); impl->redo.clear(); impl->root = changed.root;
}
Document::~Document() = default;
Document::Snapshot Document::Capture() const { return {impl->root}; }
Position Document::Size() const { return Impl::Length(impl->root); }
Position Document::Size(const Snapshot& snapshot) { return Impl::Length(snapshot.root); }
std::string Document::Read(Position offset, size_t length) const { return Read(Capture(), offset, length); }
std::string Document::Read(const Snapshot& snapshot, Position offset, size_t length) {
    if (offset > Size(snapshot)) throw std::runtime_error("Document offset out of range.");
    length = static_cast<size_t>((std::min)(Position(length), Size(snapshot) - offset));
    std::string output; output.reserve(length); Impl::Append(snapshot.root, offset, length, output); return output;
}
struct Document::ReadSession::Impl { Snapshot snapshot; ReadSessions sessions; explicit Impl(Snapshot source) : snapshot(std::move(source)) {} };
Document::ReadSession::ReadSession(Snapshot snapshot) : impl(std::make_unique<Impl>(std::move(snapshot))) {}
Document::ReadSession::~ReadSession() = default;
std::string Document::ReadSession::Read(Position offset, size_t length) {
    const auto size = Document::Size(impl->snapshot);
    if (offset > size) throw std::runtime_error("Document offset out of range.");
    length = static_cast<size_t>((std::min)(Position(length), size - offset));
    std::string output; output.reserve(length); Document::Impl::Append(impl->snapshot.root, offset, length, output, &impl->sessions); return output;
}
void Document::Begin(Selection selection) {
    if (!impl->depth) {
        if (impl->undo.size() == impl->undo.capacity()) impl->undo.reserve((std::max)(size_t(64), impl->undo.capacity() * 2));
        impl->groupRoot = impl->root; impl->groupSelection = selection; }
    ++impl->depth;
}
void Document::Replace(Position offset, Position length, std::string_view nativeBytes) {
    if (offset > Size() || length > Size() - offset || offset % impl->width || length % impl->width || nativeBytes.size() % impl->width) throw std::runtime_error("Invalid native edit range.");
    if (!length && nativeBytes.empty()) return;
    if (nativeBytes.size() > Position(PTRDIFF_MAX) - (Size() - length)) throw std::runtime_error("Edited document exceeds address space.");
    const bool single = !impl->depth;
    if (single) Begin({offset, offset});
    try {
        auto [left, rest] = impl->Split(impl->root, offset);
        Impl::Root right;
#ifdef MULTIPAD4_APPEND_BASELINE
#include "../tests/hooks/LazyDocument.cpp.10.inc"
#else
        if (length) right = impl->Split(rest, length).second; else right = std::move(rest);
#endif
        Impl::Root added;
        if (!nativeBytes.empty()) {
            if (!impl->journal) impl->journal = std::make_shared<Journal>();
            auto begin = impl->journal->Append(nativeBytes);
            auto source = std::make_shared<Source>(impl->journal, begin, nativeBytes.size(), impl->width, impl->little, impl->counters);
            added = impl->Make({source, 0, nativeBytes.size()}); impl->counters->added += nativeBytes.size();
        }
        impl->root = impl->Concat(impl->Concat(left, added), right);
    } catch (...) { if (single) End({offset, offset}); throw; }
    if (single) End({offset + nativeBytes.size(), offset + nativeBytes.size()});
}
void Document::End(Selection selection) {
    if (!impl->depth || --impl->depth) return;
    if (impl->root != impl->groupRoot) {
        impl->undo.push_back({impl->groupRoot, impl->root, impl->groupSelection, selection}); impl->redo.clear();
    }
    impl->groupRoot.reset();
}
void Document::Abort() { if (impl->depth) { impl->root = impl->groupRoot; impl->groupRoot.reset(); impl->depth = 0; } }
bool Document::CanUndo() const { return !impl->undo.empty(); }
bool Document::CanRedo() const { return !impl->redo.empty(); }
std::optional<Selection> Document::Undo() {
    if (impl->depth || impl->undo.empty()) return {};
    auto revision = impl->undo.back(); impl->redo.push_back(revision); impl->undo.pop_back(); impl->root = revision.before; return revision.beforeSelection;
}
std::optional<Selection> Document::Redo() {
    if (impl->depth || impl->redo.empty()) return {};
    auto revision = impl->redo.back(); impl->undo.push_back(revision); impl->redo.pop_back(); impl->root = revision.after; return revision.afterSelection;
}
bool Document::Modified() const { return impl->root != impl->saved; }
void Document::MarkSaved() { impl->saved = impl->root; }
Position Document::FileSize() const { return Size() + impl->signature.size(); }
void Document::Write(const files::ChunkConsumer& consume) const {
    const auto snapshot = Capture();
    consume(impl->signature);
#ifdef MULTIPAD4_APPEND_BASELINE
#include "../tests/hooks/LazyDocument.cpp.11.inc"
#else
    std::string pending; pending.reserve(AppendBlockBytes);
    auto flush = [&] { if (!pending.empty()) { consume(pending); pending.clear(); } };
    Impl::Stream(snapshot.root, [&](std::string_view bytes) {
        if (bytes.size() >= AppendBlockBytes) { flush(); consume(bytes); }
        else { if (bytes.size() > AppendBlockBytes - pending.size()) flush(); pending.append(bytes); }
    });
    flush();
#endif
}
void Document::Save(const std::filesystem::path& path, bool copy) {
    files::WriteAtomicChunks(path, [&](const files::ChunkConsumer& consume) { Write(consume); });
    if (!copy) MarkSaved();
}
Document::Metrics Document::Statistics() const { return {impl->counters->read.load(), impl->counters->added.load(), impl->counters->blocks.load(), impl->counters->nodes.load()}; }
Position Document::LineCount(const Snapshot& snapshot, const std::atomic<bool>* cancel) { CancelScope scope(cancel); ReadSessions sessions; return Impl::Describe(snapshot.root, &sessions).breaks + 1; }
Position Document::PositionFromLine(const Snapshot& snapshot, Position line, const std::atomic<bool>* cancel) {
    CancelScope scope(cancel); if (!line) return 0; ReadSessions sessions;
#ifdef MULTIPAD4_LINE_BENCHMARK
#include "../tests/hooks/LazyDocument.cpp.12.inc"
#endif
    const auto size = Size(snapshot); Summary prefix;
    const auto found = Impl::SelectLine(snapshot.root, 0, line, prefix, sessions);
    if (!found) return size;
    auto low = *found;
    const auto width = snapshot.root ? snapshot.root->piece.source->Width() : 1;
    const bool little = !snapshot.root || snapshot.root->piece.source->Little();
    if (low && low < size) { std::string pair; Impl::Append(snapshot.root, low - width, 2 * width, pair, &sessions); if (Unit(pair, 0, width, little) == '\r' && Unit(pair, width, width, little) == '\n') low += width; }
    return low;
}
Position Document::LineFromPosition(const Snapshot& snapshot, Position position, const std::atomic<bool>* cancel) {
    CancelScope scope(cancel); ReadSessions sessions; position = (std::min)(position, Size(snapshot));
    const auto width = snapshot.root ? snapshot.root->piece.source->Width() : 1;
    const bool little = !snapshot.root || snapshot.root->piece.source->Little();
    position -= position % width; auto count = Impl::Prefix(snapshot.root, position, sessions).breaks;
    if (position && position < Size(snapshot)) {
        std::string pair; Impl::Append(snapshot.root, position - width, 2 * width, pair, &sessions);
        if (Unit(pair, 0, width, little) == '\r' && Unit(pair, width, width, little) == '\n') --count;
    }
    return count;
}
std::optional<Document::Match> Document::Find(const Snapshot& snapshot, std::string_view needle, Position start, Position limit, bool backwards, const std::atomic<bool>* cancel) {
    CancelScope scope(cancel); if (needle.empty()) return {};
    const auto width = snapshot.root ? snapshot.root->piece.source->Width() : 1;
    if (needle.size() % width) throw std::runtime_error("Search text is not aligned to the source encoding.");
    auto size = Size(snapshot); start = (std::min)(start, size); limit = (std::min)(limit, size);
    if (!backwards) {
        if (limit < start) return {};
        for (Position offset = start; offset < limit;) {
            CheckCancel(); auto count = static_cast<size_t>((std::min)(limit - offset, Position(1024 * 1024 + needle.size())));
            auto bytes = Read(snapshot, offset, count); auto found = bytes.find(needle);
            while (found != std::string::npos && (offset + found) % width) found = bytes.find(needle, found + 1);
            if (found != std::string::npos) return Match{offset + found, needle.size()};
            if (count <= needle.size()) break; offset += count - needle.size() + 1;
        }
    } else {
        if (start < limit) return {};
        for (Position end = start; end > limit;) {
            CheckCancel(); auto begin = end > 1024 * 1024 + needle.size() ? end - 1024 * 1024 - needle.size() : 0; begin = (std::max)(begin, limit);
            auto bytes = Read(snapshot, begin, static_cast<size_t>(end - begin)); auto found = bytes.rfind(needle);
            while (found != std::string::npos && (begin + found) % width) { if (!found) { found = std::string::npos; break; } found = bytes.rfind(needle, found - 1); }
            if (found != std::string::npos) return Match{begin + found, needle.size()};
            if (begin == limit) break; end = begin + needle.size() - 1;
        }
    }
    return {};
}
}

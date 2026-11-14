#include "LazySearch.h"
#include "SimdText.h"
#include <emmintrin.h>
#include <windows.h>
#include <locale>
#define BOOST_REGEX_STANDALONE
#include <boost/regex.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <thread>
#include <exception>
#include <iterator>
#include <stdexcept>
namespace large {
namespace {
constexpr Position CacheBytes = 64 * 1024;
void FoldAscii(std::string& text) {
    size_t at = 0;
    const auto lower = _mm_set1_epi8('A' - 1), upper = _mm_set1_epi8('Z' + 1), bit = _mm_set1_epi8(32);
    for (; at + 16 <= text.size(); at += 16) {
        const auto value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(text.data() + at));
        const auto letters = _mm_and_si128(_mm_cmpgt_epi8(value, lower), _mm_cmpgt_epi8(upper, value));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(text.data() + at), _mm_or_si128(value, _mm_and_si128(letters, bit)));
    }
    for (; at < text.size(); ++at) { auto& c = text[at]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; }
}
files::Encoding Plain(files::Encoding value) {
    if (value == files::Encoding::Utf8Bom) return files::Encoding::Utf8;
    if (value == files::Encoding::Utf16LE) return files::Encoding::Utf16LEPlain;
    if (value == files::Encoding::Utf16BE) return files::Encoding::Utf16BEPlain;
    return value;
}
struct Reader {
    Document::Snapshot snapshot;
    files::Encoding encoding;
    const std::atomic<bool>* cancel;
    mutable std::unique_ptr<Document::ReadSession> session;
    std::string Read(Position offset, size_t count) const { return session ? session->Read(offset, count) : Document::Read(snapshot, offset, count); }
    struct Cache { Position begin = 0; std::string bytes; unsigned long long stamp = 0; };
    mutable std::array<Cache, 4> cache;
    mutable unsigned long long clock = 0, visits = 0;
    mutable std::map<Position, Position> checkpoints{{0, 0}};
    mutable std::map<Position, std::vector<Position>> boundaries;
    unsigned width = 1, codePage = CP_UTF8;
    bool utf8 = true, little = true, dbcs = false;
    struct Character { wchar_t value[2]{}; unsigned units = 1, bytes = 1; };
    Reader(Document::Snapshot value, files::Encoding codec, const std::atomic<bool>* stop, bool independent = false) : snapshot(std::move(value)), encoding(Plain(codec)), cancel(stop) {
        if (independent) session = std::make_unique<Document::ReadSession>(snapshot);
        width = encoding == files::Encoding::Utf16LEPlain || encoding == files::Encoding::Utf16BEPlain ? 2 : 1;
        little = encoding != files::Encoding::Utf16BEPlain; codePage = files::CodePage(encoding); utf8 = width == 1 && codePage == CP_UTF8;
        CPINFO info{}; if (width == 1 && GetCPInfo(codePage, &info)) dbcs = !utf8 && info.MaxCharSize > 1;
    }
    void Check() const { if (cancel && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Search cancelled."); }
    Position Size() const { return Document::Size(snapshot); }
    Position Align(Position position) const {
        position = (std::min)(position, Size());
        if (width == 2) {
            position += position % 2;
            if (position < Size() && position && Unit(position) >= 0xDC00 && Unit(position) <= 0xDFFF && Unit(position - 2) >= 0xD800 && Unit(position - 2) <= 0xDBFF) position += 2;
        } else if (utf8) while (position < Size() && (Byte(position) & 0xC0) == 0x80) ++position;
        return position;
    }
    unsigned char Byte(Position position) const {
        if (position >= Size()) throw std::runtime_error("Search iterator out of bounds.");
        for (auto& block : cache) if (!block.bytes.empty() && position >= block.begin && position - block.begin < block.bytes.size()) { block.stamp = ++clock; return static_cast<unsigned char>(block.bytes[static_cast<size_t>(position - block.begin)]); }
        Check(); auto* slot = &*std::min_element(cache.begin(), cache.end(), [](const auto& a, const auto& b) { return a.stamp < b.stamp; });
        slot->begin = position / CacheBytes * CacheBytes; slot->bytes = Read(slot->begin, static_cast<size_t>((std::min)(CacheBytes + 8, Size() - slot->begin))); slot->stamp = ++clock;
        return static_cast<unsigned char>(slot->bytes[static_cast<size_t>(position - slot->begin)]);
    }
    unsigned Unit(Position position) const { auto a = Byte(position), b = Byte(position + 1); return little ? a | (b << 8) : (a << 8) | b; }
    Character At(Position position) const {
        if ((++visits & 1023) == 0) Check(); Character result;
        if (width == 2) { result.value[0] = static_cast<wchar_t>(Unit(position)); result.bytes = 2; return result; }
        auto first = Byte(position);
        if (utf8 && first < 0x80) { result.value[0] = first; return result; }
        unsigned count = 1;
        if (utf8) count = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC2 ? 2 : 1;
        else if (codePage == 54936 && first >= 0x81 && first <= 0xFE && position + 1 < Size()) { auto next = Byte(position + 1); count = next >= 0x30 && next <= 0x39 ? 4 : 2; }
        else if (IsDBCSLeadByteEx(codePage, first)) count = 2;
        if (count > Size() - position) throw std::runtime_error("Truncated encoded search input.");
        char bytes[4]{}; for (unsigned i = 0; i < count; ++i) bytes[i] = static_cast<char>(Byte(position + i));
        auto units = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes, count, result.value, 2);
        if (!units && GetLastError() == ERROR_INVALID_FLAGS) units = MultiByteToWideChar(codePage, 0, bytes, count, result.value, 2);
        if (!units) throw std::runtime_error("Invalid encoded search input.");
        result.units = units; result.bytes = count; return result;
    }
    Position Next(Position position) const {
        if (position >= Size()) return Size(); auto value = At(position); auto next = position + value.bytes;
        if (dbcs && next / CacheBytes != position / CacheBytes) checkpoints[next / CacheBytes] = next;
        return next;
    }
    Position Previous(Position position) const {
        if (!position) return 0;
        if (width == 2) return position - 2;
        if (utf8) { auto previous = position - 1; for (int i = 0; i < 3 && previous && (Byte(previous) & 0xC0) == 0x80; ++i) --previous; return previous; }
        if (!dbcs) return position - 1;
        auto block = (position - 1) / CacheBytes;
        auto found = boundaries.find(block);
        if (found == boundaries.end()) {
            auto checkpoint = checkpoints.upper_bound(block); --checkpoint; auto current = checkpoint->second;
            while (current < block * CacheBytes) { auto next = Next(current); if (next >= block * CacheBytes) break; current = next; }
            std::vector<Position> points;
            while (current < (std::min)(Size(), (block + 1) * CacheBytes)) { points.push_back(current); current = Next(current); }
            if (boundaries.size() >= 4) boundaries.erase(boundaries.begin());
            found = boundaries.emplace(block, std::move(points)).first;
        }
        auto next = std::lower_bound(found->second.begin(), found->second.end(), position);
        if (next == found->second.begin()) throw std::runtime_error("Cannot resolve a search character boundary.");
        return *--next;
    }
    Position Advance(Position position, bool backwards) const {
        if (backwards) {
            auto previous = Previous(position);
            if (width == 2 && previous && Unit(previous) >= 0xDC00 && Unit(previous) <= 0xDFFF && Unit(previous - 2) >= 0xD800 && Unit(previous - 2) <= 0xDBFF) previous -= 2;
            return previous;
        }
        auto next = Next(position);
        if (width == 2 && next < Size() && Unit(position) >= 0xD800 && Unit(position) <= 0xDBFF && Unit(next) >= 0xDC00 && Unit(next) <= 0xDFFF) next += 2;
        return next;
    }
    bool Word(Position position) const {
        if (position >= Size()) return false; auto c = At(position).value[0]; WORD type = 0;
        return c == L'_' || (GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & (C1_ALPHA | C1_DIGIT)));
    }
    bool WordBefore(Position position) const { return position && Word(Advance(position, true)); }
};
struct Iterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = wchar_t;
    using difference_type = ptrdiff_t;
    using reference = wchar_t;
    using pointer = const wchar_t*;
    Reader* reader = nullptr;
    Position position = 0;
    unsigned part = 0;
    wchar_t operator*() const { return reader->At(position).value[part]; }
    Iterator& operator++() { auto c = reader->At(position); if (part + 1 < c.units) ++part; else { position = reader->Next(position); part = 0; } return *this; }
    Iterator operator++(int) { auto copy = *this; ++*this; return copy; }
    Iterator& operator--() { if (part) --part; else { position = reader->Previous(position); part = reader->At(position).units - 1; } return *this; }
    Iterator operator--(int) { auto copy = *this; --*this; return copy; }
    bool operator==(const Iterator& other) const { return reader == other.reader && position == other.position && part == other.part; }
    bool operator!=(const Iterator& other) const { return !(*this == other); }
    Position Start() const { if (reader->width == 2 && position && position < reader->Size() && reader->Unit(position) >= 0xDC00 && reader->Unit(position) <= 0xDFFF && reader->Unit(position - 2) >= 0xD800 && reader->Unit(position - 2) <= 0xDBFF) return position - 2; return position; }
    Position End() const {
        if (part) return position + reader->At(position).bytes;
        if (reader->width == 2 && position && position < reader->Size() && reader->Unit(position) >= 0xDC00 && reader->Unit(position) <= 0xDFFF && reader->Unit(position - 2) >= 0xD800 && reader->Unit(position - 2) <= 0xDBFF) return position + 2;
        return position;
    }
};
}
struct HitArchive::Impl {
    struct Record { Position start, end, line, lineStart; };
    HANDLE file = INVALID_HANDLE_VALUE;
    std::vector<Record> pending;
    size_t count = 0;
    bool retainOffsets;
    explicit Impl(bool retain) : retainOffsets(retain) {}
    void Open() {
        wchar_t directory[32768]{}, name[32768]{};
        if (!GetTempPathW(_countof(directory), directory) || !GetTempFileNameW(directory, L"MPQ", 0, name)) throw std::runtime_error("Cannot create search result archive.");
        file = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file == INVALID_HANDLE_VALUE) { DeleteFileW(name); throw std::runtime_error("Cannot open search result archive."); }
    }
    ~Impl() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    void Flush() {
        if (pending.empty()) return;
        if (file == INVALID_HANDLE_VALUE) Open();
        LARGE_INTEGER offset{}; offset.QuadPart = static_cast<LONGLONG>((count - pending.size()) * sizeof(Record)); DWORD written = 0;
        const auto bytes = static_cast<DWORD>(pending.size() * sizeof(Record));
        if (!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) || !WriteFile(file, pending.data(), bytes, &written, nullptr) || written != bytes) throw std::runtime_error("Cannot write complete search result archive.");
        pending.clear();
    }
};
HitArchive::HitArchive(bool retainOffsets) : impl(std::make_unique<Impl>(retainOffsets)) {}
HitArchive::~HitArchive() = default;
void HitArchive::Append(const SearchHit& hit) {
    if (impl->count >= static_cast<size_t>(LLONG_MAX) / sizeof(Impl::Record)) throw std::runtime_error("Too many search results.");
    if (!impl->retainOffsets) { ++impl->count; return; }
    impl->pending.push_back({hit.start, hit.end, hit.line, hit.lineStart}); ++impl->count;
    if (impl->pending.size() == 4096) impl->Flush();
}
size_t HitArchive::Count() const { return impl->count; }
bool HitArchive::RetainsOffsets() const { return impl->retainOffsets; }
void HitArchive::AddCount(size_t count) {
    if (impl->retainOffsets || count > SIZE_MAX - impl->count) throw std::runtime_error("Invalid count-only search merge.");
    impl->count += count;
}
std::vector<SearchHit> HitArchive::Page(size_t first, size_t count) const {
    if (!impl->retainOffsets) throw std::runtime_error("Offsets are unavailable for a count-only search.");
    if (first > impl->count) throw std::runtime_error("Invalid search result page.");
    count = (std::min)(count, impl->count - first);
    std::vector<SearchHit> output; output.reserve(count);
    // Small results remain bounded in memory; creating a temp file for every
    // matching (or empty) directory entry would dominate small-file searches.
    if (impl->file == INVALID_HANDLE_VALUE) {
        for (size_t i = first; i < first + count; ++i) {
            const auto& record = impl->pending[i];
            output.push_back({record.start, record.end, {}, record.line, record.lineStart});
        }
        return output;
    }
    impl->Flush();
    for (size_t index = 0; index < count;) {
        const auto take = (std::min)(size_t(4096), count - index); std::vector<Impl::Record> records(take);
        LARGE_INTEGER offset{}; offset.QuadPart = static_cast<LONGLONG>((first + index) * sizeof(Impl::Record)); DWORD got = 0;
        const auto bytes = static_cast<DWORD>(take * sizeof(Impl::Record));
        if (!SetFilePointerEx(impl->file, offset, nullptr, FILE_BEGIN) || !ReadFile(impl->file, records.data(), bytes, &got, nullptr) || got != bytes) throw std::runtime_error("Cannot read search result page.");
        for (const auto& record : records) output.push_back({record.start, record.end, {}, record.line, record.lineStart}); index += take;
    }
    return output;
}
size_t HitArchive::FirstEndingAtOrAfter(Position position) const {
    size_t low = 0, high = Count();
    while (low < high) { auto middle = low + (high - low) / 2; auto hit = Page(middle, 1)[0]; if (hit.end < position) low = middle + 1; else high = middle; }
    return low;
}
struct Search::Impl {
    Reader reader;
    SearchOptions options;
    boost::wregex expression;
    std::string nativeNeedle;
    bool fastLiteral;
    std::string literalBytes;
    Position literalBegin = 0;
    std::wstring literalWide;
    std::vector<uint32_t> wideOffsets;
    bool literalAscii = false, asciiLiteral = false;
    std::string asciiNeedle, foldedBytes;
    Position LiteralOffset(size_t index) const { return literalBegin + (reader.width == 2 ? index * 2 : literalAscii ? index : wideOffsets[index]); }
    void LoadLiteral(Position begin, Position end) {
        reader.Check(); literalBegin = begin;
        const auto requested = (std::min)(end - begin, Position(1024 * 1024 + nativeNeedle.size() + 8));
        literalBytes = reader.Read(begin, static_cast<size_t>(requested));
        auto count = literalBytes.size();
        if (begin + count < end) {
            count -= 8;
            if (reader.utf8) while (count && (static_cast<unsigned char>(literalBytes[count]) & 0xC0) == 0x80) --count;
            else if (reader.width == 2) { count -= count % 2; auto unit = static_cast<unsigned char>(literalBytes[count - 2]) | (static_cast<unsigned char>(literalBytes[count - 1]) << 8); if (!reader.little) unit = (unit >> 8) | ((unit & 255) << 8); if (unit >= 0xD800 && unit <= 0xDBFF) count -= 2; }
            else if (reader.dbcs) { size_t position = 0; while (position < count) {
                const auto c = static_cast<unsigned char>(literalBytes[position]); size_t width = c < 0x80 ? 1 : IsDBCSLeadByteEx(reader.codePage, c) ? 2 : 1;
                if (reader.codePage == 54936 && c >= 0x81 && c <= 0xFE) { auto next = static_cast<unsigned char>(literalBytes[position + 1]); width = next >= 0x30 && next <= 0x39 ? 4 : 2; }
                if (position + width > count) { count = position; break; } position += width;
            } }
        }
        literalBytes.resize(count); literalWide.clear(); wideOffsets.clear(); asciiLiteral = false;
        if (fastLiteral) return;
        literalAscii = reader.width == 1 && simd::IsAscii(literalBytes);
        if (literalAscii && !asciiNeedle.empty()) {
            asciiLiteral = true; foldedBytes = literalBytes;
            if (!options.matchCase) FoldAscii(foldedBytes);
            return;
        }
        auto utf8 = files::Decode(literalBytes, reader.encoding, false).utf8;
        literalWide = files::FromUtf8(utf8);
        literalAscii = reader.width == 1 && literalWide.size() == literalBytes.size() && std::all_of(literalBytes.begin(), literalBytes.end(), [](unsigned char c) { return c < 128; });
        if (reader.width == 2 || literalAscii) return;
        size_t byte = 0, character = 0;
        wideOffsets.resize(literalWide.size() + 1);
        while (byte < literalBytes.size()) {
            auto c = static_cast<unsigned char>(literalBytes[byte]); size_t width = 1;
            if (reader.utf8) width = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            else if (reader.codePage == 54936 && c >= 0x81 && c <= 0xFE) { auto next = static_cast<unsigned char>(literalBytes[byte + 1]); width = next >= 0x30 && next <= 0x39 ? 4 : 2; }
            else if (IsDBCSLeadByteEx(reader.codePage, c)) width = 2;
            wideOffsets[character++] = static_cast<uint32_t>(byte);
            if (character < literalWide.size() && literalWide[character - 1] >= 0xD800 && literalWide[character - 1] <= 0xDBFF && literalWide[character] >= 0xDC00 && literalWide[character] <= 0xDFFF) wideOffsets[character++] = static_cast<uint32_t>(byte);
            byte += width;
        }
        if (character != literalWide.size()) throw std::runtime_error("Invalid character mapping in search input.");
        wideOffsets.back() = static_cast<uint32_t>(literalBytes.size());
    }
    std::optional<SearchHit> Literal(Position begin, Position end) {
        auto cursor = begin;
        for (;;) {
            reader.Check();
            if (literalBytes.empty() || cursor < literalBegin || cursor >= literalBegin + literalBytes.size()) LoadLiteral(cursor, end);
            if (literalBytes.empty()) return {};
            const auto cacheEnd = (std::min)(end, literalBegin + literalBytes.size());
            if (fastLiteral || asciiLiteral) {
                const auto& bytes = asciiLiteral ? foldedBytes : literalBytes;
                const auto& needle = asciiLiteral ? asciiNeedle : nativeNeedle;
                auto found = bytes.find(needle, static_cast<size_t>(cursor - literalBegin));
                while (found != std::string::npos) {
                    const auto start = literalBegin + found, finish = start + needle.size();
                    if (finish > end) break;
                    if (start % reader.width == 0 && ((!options.wholeWord && !options.wordStart) || !reader.WordBefore(start)) && (!options.wholeWord || !reader.Word(finish))) return SearchHit{start, finish, {{start, finish, true}}};
                    found = bytes.find(needle, found + 1);
                }
            } else {
                size_t first = reader.width == 2 ? static_cast<size_t>((cursor - literalBegin) / 2) : literalAscii ? static_cast<size_t>(cursor - literalBegin) : static_cast<size_t>(std::lower_bound(wideOffsets.begin(), wideOffsets.end(), cursor - literalBegin) - wideOffsets.begin());
                while (first <= literalWide.size()) {
                    auto found = FindStringOrdinal(FIND_FROMSTART, literalWide.data() + first, static_cast<int>(literalWide.size() - first), options.pattern.data(), static_cast<int>(options.pattern.size()), !options.matchCase);
                    if (found < 0) break;
                    auto at = first + found, last = at + options.pattern.size();
                    auto start = LiteralOffset(at), finish = LiteralOffset(last);
                    if (finish > end) break;
                    if (((!options.wholeWord && !options.wordStart) || !reader.WordBefore(start)) && (!options.wholeWord || !reader.Word(finish))) return SearchHit{start, finish, {{start, finish, true}}};
                    first = at + 1;
                }
            }
            if (cacheEnd >= end) return {};
            Position next;
            if (fastLiteral || asciiLiteral) next = cacheEnd - (std::min)(Position(nativeNeedle.size() - 1), cacheEnd - literalBegin);
            else { auto overlap = (std::min)(literalWide.size(), options.pattern.size() + 2); auto first = literalWide.size() - overlap; if (first && literalWide[first] >= 0xDC00 && literalWide[first] <= 0xDFFF) --first; next = LiteralOffset(first); }
            if (next <= cursor) next = reader.Advance(cursor, false);
            cursor = next; literalBytes.clear();
        }
    }
    Impl(Document::Snapshot snapshot, files::Encoding encoding, SearchOptions value, const std::atomic<bool>* cancel, bool independentReads) : reader(std::move(snapshot), encoding, cancel, independentReads), options(std::move(value)), fastLiteral(!options.regex && options.matchCase && !options.wholeWord && !options.wordStart && !reader.dbcs) {
        if (options.pattern.size() > INT_MAX) throw std::runtime_error("Search expression is too long.");
        try { boost::wregex::flag_type flags = options.regex ? boost::wregex::ECMAScript : boost::wregex::literal;
            if (!options.matchCase) flags |= boost::wregex::icase;
            if (!options.pattern.empty()) expression.assign(options.pattern, flags);
        } catch (const boost::regex_error& error) { throw std::runtime_error(std::string("Invalid regular expression: ") + error.what()); }
        if (!options.regex) {
            nativeNeedle = files::Encode({files::ToUtf8(options.pattern), Plain(encoding)});
            if (reader.width == 1 && std::all_of(options.pattern.begin(), options.pattern.end(), [](wchar_t c) { return c < 128; })) {
                asciiNeedle = nativeNeedle;
                if (!options.matchCase) for (char& c : asciiNeedle) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            }
        }
    }
    static constexpr Position ParallelBlock = 64 * 1024 * 1024;
    unsigned Workers(Position length) const {
        if (options.regex || options.pattern.empty() || reader.dbcs || nativeNeedle.size() > 1024 * 1024 || length < ParallelBlock) return 1;
        const auto available = (std::max)(1u, std::thread::hardware_concurrency());
        return (std::min)(options.workers ? options.workers : 8u, (std::min)(available, 8u));
    }
    Position Halo() const { return Position(options.pattern.size()) * 4 + 8; }
    struct Job {
        Position first = 0, last = 0, limit = 0, lastEnd = 0;
        std::optional<SearchHit> hit;
        std::shared_ptr<HitArchive> archive;
        std::exception_ptr error;
    };
    void Scan(Job& job, bool backwards, bool enumerate, bool retainOffsets) {
        auto serial = options; serial.workers = 1;
        Search search(reader.snapshot, reader.encoding, serial, reader.cancel, true);
        if (enumerate) {
            job.archive = std::make_shared<HitArchive>(retainOffsets);
            auto position = job.first;
            while (position < job.last) {
                auto hit = search.Next(position, job.limit); if (!hit || hit->start >= job.last) break;
                if (!job.hit) job.hit = hit;
                job.archive->Append(*hit); job.lastEnd = hit->end; position = hit->end;
            }
        } else if (!backwards) {
            job.hit = search.Next(job.first, job.limit);
            if (job.hit && job.hit->start >= job.last) job.hit.reset();
        } else {
            auto end = job.limit;
            while (end > job.first) {
                auto hit = search.Next(end, job.first, true); if (!hit) break;
                if (hit->start < job.last) { job.hit = hit; break; }
                end = search.Advance(hit->end, true);
            }
        }
    }
    std::vector<Job> Batch(Position first, Position last, Position globalLimit, unsigned workers, bool backwards, bool enumerate, bool retainOffsets) {
        reader.Check();
        std::vector<Job> jobs;
        for (auto at = first; at < last;) {
            const auto next = (std::min)(last, reader.Align((std::min)(last, at + ParallelBlock)));
            jobs.push_back({at, next, (std::min)(globalLimit, reader.Align(next + (std::min)(Halo(), globalLimit - next)))}); at = next;
        }
        // jthread also joins workers if creating a later thread throws.
        std::vector<std::jthread> threads; threads.reserve(workers);
        for (size_t index = 0; index < jobs.size(); ++index) threads.emplace_back([&, index] {
            try { Scan(jobs[index], backwards, enumerate, retainOffsets); } catch (...) { jobs[index].error = std::current_exception(); }
        });
        for (auto& thread : threads) thread.join();
        reader.Check(); return jobs;
    }
    std::optional<SearchHit> Forward(Position begin, Position end) {
        reader.Check();
        if (begin > end || end > reader.Size()) return {};
        if (!options.regex) return options.pattern.empty() ? std::optional<SearchHit>{} : Literal(begin, end);
        // Scintilla starts regex ranges outside a CRLF pair. A previous
        // one-character match may have ended between its two code units.
        auto current = begin;
        if (current && current < end && reader.At(current).value[0] == L'\n' && reader.At(reader.Previous(current)).value[0] == L'\r') current = reader.Next(current);
        for (;;) {
            reader.Check();
            auto flags = boost::regex_constants::match_default;
            if (!options.dotAll) flags |= boost::regex_constants::match_not_dot_newline;
            if (current) flags |= boost::regex_constants::match_prev_avail | boost::regex_constants::match_not_bob;
            if (end < reader.Size()) {
                flags |= boost::regex_constants::match_not_eob;
                auto c = reader.At(end).value[0]; if (c != L'\n' && c != L'\r') flags |= boost::regex_constants::match_not_eol;
            }
            boost::match_results<Iterator> match;
            bool found = false;
            try { found = boost::regex_search(Iterator{&reader, current}, Iterator{&reader, end}, match, expression, flags, Iterator{&reader, 0}); }
            catch (const boost::regex_error& error) { throw std::runtime_error(std::string("Regular expression failed: ") + error.what()); }
            if (!found) return {};
            SearchHit hit; hit.start = match[0].first.Start(); hit.end = match[0].second.End();
            if (hit.start >= begin && hit.end <= end &&
                ((!options.wholeWord && !options.wordStart) || !reader.WordBefore(hit.start)) &&
                (!options.wholeWord || !reader.Word(hit.end))) {
                for (size_t group = 0; group < (std::min)(match.size(), size_t(10)); ++group) hit.captures.push_back({match[group].first.Start(), match[group].second.End(), match[group].matched});
                return hit;
            }
            if (hit.start >= end) return {}; current = reader.Advance(hit.start, false);
        }
    }
};
Search::Search(Document::Snapshot snapshot, files::Encoding encoding, SearchOptions options, const std::atomic<bool>* cancel, bool independentReads) : impl(std::make_unique<Impl>(std::move(snapshot), encoding, std::move(options), cancel, independentReads)) {}
Search::~Search() = default;
void Search::Rebase(Document::Snapshot snapshot) { impl->reader.snapshot = std::move(snapshot); if (impl->reader.session) impl->reader.session = std::make_unique<Document::ReadSession>(impl->reader.snapshot); impl->reader.cache = {}; impl->reader.checkpoints = {{0, 0}}; impl->reader.boundaries.clear(); impl->literalBytes.clear(); impl->literalWide.clear(); impl->wideOffsets.clear(); }
Position Search::Advance(Position position, bool backwards) const { return impl->reader.Advance(position, backwards); }
Position Search::Size() const { return impl->reader.Size(); }
std::optional<SearchHit> Search::Next(Position start, Position limit, bool backwards) {
    const auto size = Size();
    if (start > size || limit > size || (!backwards && start > limit) || (backwards && start < limit)) return {};
    const auto workers = impl->Workers(backwards ? start - limit : limit - start);
    if (workers > 1) {
        const auto lower = backwards ? limit : start, upper = backwards ? start : limit;
        auto first = lower, last = upper;
        // Keep nearby matches cheap; only launch multiple workers after a miss.
        if (!backwards) {
            const auto probeEnd = impl->reader.Align((std::min)(upper, first + 256 * 1024));
            if (auto hit = impl->Forward(first, (std::min)(upper, impl->reader.Align(probeEnd + (std::min)(impl->Halo(), upper - probeEnd))))) return hit;
            first = probeEnd;
        } else {
            auto serial = impl->options; serial.workers = 1;
            Search nearby(impl->reader.snapshot, impl->reader.encoding, serial, impl->reader.cancel);
            const auto probeBegin = impl->reader.Align(upper - lower > 256 * 1024 ? upper - 256 * 1024 : lower);
            if (auto hit = nearby.Next(upper, probeBegin, true)) return hit;
            last = probeBegin;
        }
        while (first < last) {
            const auto batchFirst = backwards ? (last - first > impl->ParallelBlock * workers ? impl->reader.Align(last - impl->ParallelBlock * workers) : first) : first;
            const auto batchLast = backwards ? last : (std::min)(last, impl->reader.Align((std::min)(last, first + impl->ParallelBlock * workers)));
            auto jobs = impl->Batch(batchFirst, batchLast, upper, workers, backwards, false, false);
            for (size_t index = 0; index < jobs.size(); ++index) {
                auto& job = jobs[backwards ? jobs.size() - 1 - index : index];
                if (job.error) std::rethrow_exception(job.error);
                if (job.hit) return job.hit;
            }
            if (backwards) last = batchFirst; else first = batchLast;
        }
        return {};
    }
    if (!backwards) return impl->Forward(start, limit);
    if (start < limit) return {};
    if (impl->fastLiteral && !impl->nativeNeedle.empty()) {
        for (auto end = start; end > limit;) {
            impl->reader.Check();
            const auto bytesToRead = Position(1024 * 1024) + impl->nativeNeedle.size();
            const auto begin = end - limit > bytesToRead ? end - bytesToRead : limit;
            auto bytes = impl->reader.Read(begin, static_cast<size_t>(end - begin));
            auto found = bytes.rfind(impl->nativeNeedle);
            while (found != std::string::npos && (begin + found) % impl->reader.width) { if (!found) { found = std::string::npos; break; } found = bytes.rfind(impl->nativeNeedle, found - 1); }
            if (found != std::string::npos) { const auto at = begin + found; return SearchHit{at, at + impl->nativeNeedle.size(), {{at, at + impl->nativeNeedle.size(), true}}}; }
            if (begin == limit) break; end = begin + impl->nativeNeedle.size() - 1;
        }
        return {};
    }
    std::optional<SearchHit> last;
    auto position = limit;
    while (position <= start) {
        auto match = impl->Forward(position, start); if (!match) break; last = match;
        auto next = impl->options.regex ? match->end : match->start;
        if (next == match->start) { if (next == start) break; next = Advance(next); }
        if (next <= position) break; position = next;
    }
    return last;
}
void Search::Enumerate(Position first, Position limit, HitArchive& hits) {
    if (first > limit || limit > Size() || impl->options.pattern.empty()) return;
    const auto workers = impl->Workers(limit - first);
    if (workers == 1) {
        while (first <= limit) {
            auto hit = Next(first, limit); if (!hit) break; hits.Append(*hit); first = hit->end;
            if (first == hit->start) { if (first == limit) break; first = Advance(first); }
        }
        return;
    }
    auto previousEnd = first;
    while (first < limit) {
        const auto last = (std::min)(limit, impl->reader.Align((std::min)(limit, first + impl->ParallelBlock * workers)));
        auto jobs = impl->Batch(first, last, limit, workers, false, true, hits.RetainsOffsets());
        for (auto& job : jobs) {
            if (job.error) std::rethrow_exception(job.error);
            // Local non-overlapping matches can have a different phase when a
            // previous block's match crosses the boundary (e.g. repeated aaa).
            // Replay only that block from the global accepted endpoint.
            if (job.hit && job.hit->start < previousEnd) {
                job.first = previousEnd; job.hit.reset(); job.lastEnd = 0;
                impl->Scan(job, false, true, hits.RetainsOffsets());
            }
            if (!job.hit) continue;
            previousEnd = job.lastEnd;
            if (!hits.RetainsOffsets()) hits.AddCount(job.archive->Count());
            else for (size_t at = 0; at < job.archive->Count();) {
                impl->reader.Check(); auto page = job.archive->Page(at, 4096);
                for (const auto& hit : page) hits.Append(hit); at += page.size();
            }
        }
        first = last;
    }
}
std::string Search::Utf8(Position start, Position length) const {
    if (length > static_cast<Position>(std::string().max_size())) throw std::runtime_error("Search text exceeds available address space.");
    return files::Decode(impl->reader.Read(start, static_cast<size_t>(length)), impl->reader.encoding, false).utf8;
}
std::string Search::Native(std::string_view text) const { return files::Encode({std::string(text), impl->reader.encoding}); }
std::string Search::Substitute(const SearchHit& hit, std::string_view replacement, bool expandCaptures) const {
    if (!expandCaptures) return std::string(replacement);
    std::string output;
    for (size_t i = 0; i < replacement.size(); ++i) {
        auto c = replacement[i];
        if ((c == '\\' || c == '$') && i + 1 < replacement.size()) {
            auto next = replacement[i + 1]; int group = next >= '0' && next <= '9' ? next - '0' : c == '$' && next == '&' ? 0 : -1;
            if (group >= 0) { if (static_cast<size_t>(group) < hit.captures.size() && hit.captures[group].matched) output += Utf8(hit.captures[group].start, hit.captures[group].end - hit.captures[group].start); ++i; continue; }
            if (c == '$' && next == '$') { output += '$'; ++i; continue; }
            if (c == '\\') {
                const auto at = std::string_view("\\abefnrtv").find(next);
                if (at != std::string_view::npos) { output += "\\\a\b\x1B\f\n\r\t\v"[at]; ++i; continue; }
            }
        }
        output += c;
    }
    return output;
}
}

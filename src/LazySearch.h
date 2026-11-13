#pragma once
#include "LazyDocument.h"
#include <vector>
#include <string>
namespace large {
struct SearchOptions {
    std::wstring pattern;
    bool regex = false, matchCase = false, wholeWord = false, wordStart = false, dotAll = false;
    unsigned workers = 0; // 0: automatic (up to eight); 1: serial.
};
struct SearchHit {
    Position start = 0, end = 0;
    struct Capture { Position start = 0, end = 0; bool matched = false; };
    std::vector<Capture> captures;
};
// Fixed-size offsets are spooled to a temporary file; Find All/marking need
// memory for one displayed page rather than one object per match.
class HitArchive {
    struct Impl; std::unique_ptr<Impl> impl;
public:
    explicit HitArchive(bool retainOffsets = true);
    ~HitArchive();
    void Append(const SearchHit& hit);
    size_t Count() const;
    bool RetainsOffsets() const;
    void AddCount(size_t count);
    std::vector<SearchHit> Page(size_t first, size_t count) const;
    size_t FirstEndingAtOrAfter(Position position) const;
};
class Search {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Search(Document::Snapshot snapshot, files::Encoding encoding, SearchOptions options, const std::atomic<bool>* cancel = nullptr, bool independentReads = false);
    ~Search();
    void Rebase(Document::Snapshot snapshot);
    std::optional<SearchHit> Next(Position start, Position limit, bool backwards = false);
    void Enumerate(Position first, Position limit, HitArchive& hits);
    Position Advance(Position position, bool backwards = false) const;
    std::string Utf8(Position start, Position length) const;
    std::string Native(std::string_view utf8) const;
    std::string Substitute(const SearchHit& hit, std::string_view utf8Replacement, bool expandCaptures) const;
    Position Size() const;
};
}

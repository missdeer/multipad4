#pragma once
#include <filesystem>
#include <atomic>
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <memory>
namespace files {
enum class Encoding : unsigned { Utf8, Utf8Bom, Utf16LE, Utf16BE, Utf16LEPlain, Utf16BEPlain, Ansi, Oem, CodePageBase = 0x10000 };
struct Text { std::string utf8; Encoding encoding = Encoding::Utf8; };
enum class ReadBackend { Mapped, Buffered };
struct ReadOptions {
    bool unicodeDetection = true, fileVariables = true;
    Encoding fallback = Encoding::Ansi;
    ReadBackend backend = ReadBackend::Buffered;
    const std::atomic<bool>* cancel = nullptr; // Checked between validation and decode chunks.
    unsigned validationWorkers = 0; // 0: up to four workers for >=300 MiB, otherwise serial.
};
inline constexpr unsigned long long LargeFileThreshold = 300ULL * 1024 * 1024;
struct ReadStats;
class MappedFile {
    struct Impl;
    std::unique_ptr<Impl> impl;
    explicit MappedFile(const MappedFile& source, int);
public:
    explicit MappedFile(const std::filesystem::path& path, ReadStats* stats = nullptr, bool shareDelete = false);
    ~MappedFile();
    // Independent window over the same open file version, including after save.
    std::unique_ptr<MappedFile> NewReadSession() const;
    unsigned long long Size() const;
    unsigned long long BytesRead() const;
    std::string Read(unsigned long long offset, size_t count);
    // Borrowed mapped view; the callback must finish before another access.
    void Visit(unsigned long long offset, size_t count, const std::function<void(std::string_view)>& consume);
};
Text DetectSample(const std::string& bytes, const ReadOptions& options = {});
using ChunkConsumer = std::function<void(std::string_view)>;
// Wall-clock milliseconds. Mapping first-touch faults are charged to the
// operation that accesses the page. pageFaults includes soft faults and callbacks.
struct ReadStats {
    double mappingMs = 0, copyOrReadMs = 0, probeMs = 0, validationMs = 0, decodeMs = 0, callbackMs = 0, totalMs = 0;
    unsigned long long sourceBytes = 0, validationBytes = 0, decodedBytes = 0, deliveredBytes = 0;
    size_t mappings = 0;
    unsigned long pageFaults = 0;
};
// Synchronous, bounded windows; callback views expire on return.
Encoding ReadChunks(const std::filesystem::path& path, const ChunkConsumer& consume,
    const ReadOptions& options = {}, std::optional<Encoding> forced = {}, ReadStats* stats = nullptr);
void WriteAtomicChunks(const std::filesystem::path& path,
    const std::function<void(const ChunkConsumer&)>& produce);
Text Read(const std::filesystem::path& path);
Text Read(const std::filesystem::path& path, const ReadOptions& options);
Text ReadWithEncoding(const std::filesystem::path& path, Encoding encoding);
Text ReadForReload(const std::filesystem::path& path, Encoding current, const ReadOptions& options = {});
Encoding FromCodePage(unsigned codePage);
unsigned CodePage(Encoding encoding);
bool ParseEncodingName(const std::wstring& name, Encoding& encoding);
std::string Encode(const Text& text);
std::string EncodeLossy(const Text& text);
Text Decode(std::string_view bytes, Encoding encoding, bool stripBom = true);
struct FileTimes { unsigned long long creation = 0, access = 0, write = 0; };
FileTimes GetTimes(const std::filesystem::path& path);
void SetTimes(const std::filesystem::path& path, const FileTimes& times);
void Touch(const std::filesystem::path& path);
void WriteAtomic(const std::filesystem::path& path, const Text& text);
void WriteNew(const std::filesystem::path& path, const Text& text);
std::string ToUtf8(const std::wstring& text);
std::wstring FromUtf8(const std::string& text);
const wchar_t* EncodingName(Encoding encoding);
}

#include <mutex>
#include "FileIO.h"
#include "SimdText.h"
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include "ThreadPool.h"
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <regex>
#include <cctype>
#include <unordered_map>
#include <climits>
#include <exception>
#include "FileVariables.h"
namespace files {
namespace {
long long ReadClock() { LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick); return tick.QuadPart; }
double ReadElapsed(long long start) {
    static const double scale = [] { LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); return 1000.0 / frequency.QuadPart; }();
    return (ReadClock() - start) * scale;
}
struct ReadFaults {
    ReadStats* stats;
    DWORD start = 0;
    explicit ReadFaults(ReadStats* trace) : stats(trace) {
        PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
        if (stats && GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) start = memory.PageFaultCount;
    }
    ~ReadFaults() {
        PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
        if (stats && GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) stats->pageFaults = memory.PageFaultCount - start;
    }
};
struct ReadTimer {
    double* output;
    long long start;
    explicit ReadTimer(double* target) : output(target), start(target ? ReadClock() : 0) {}
    ~ReadTimer() { if (output) *output += ReadElapsed(start); }
};
// Independent validation jobs share only the failure flag. ParallelFor drains
// every callback before the borrowed mapped view can be released or reused.
class Utf8Validator {
    unsigned workers;
    void Check(std::string_view input, size_t index, std::atomic<bool>& valid) const noexcept {
        size_t begin = input.size() * index / workers, end = input.size() * (index + 1) / workers;
        while (begin && begin < input.size() && (static_cast<unsigned char>(input[begin]) & 0xC0) == 0x80) ++begin;
        while (end < input.size() && (static_cast<unsigned char>(input[end]) & 0xC0) == 0x80) ++end;
        if (!simd::IsValidUtf8(input.substr(begin, end - begin))) valid.store(false, std::memory_order_relaxed);
    }
public:
    explicit Utf8Validator(unsigned count) : workers((std::clamp)(count, 1u, 8u)) {}
    bool Validate(std::string_view text) const {
        if (workers == 1 || text.size() < 1024 * 1024) return simd::IsValidUtf8(text);
        std::atomic<bool> valid{true};
        work::ParallelFor(workers, [&](size_t index) { Check(text, index, valid); });
        return valid.load(std::memory_order_relaxed);
    }
};
constexpr size_t ConversionChunk = 16 * 1024 * 1024;
[[noreturn]] void Fail(const char* message) { throw std::runtime_error(message); }
size_t Utf8End(const std::string& text,size_t start) {
    size_t end=(std::min)(text.size(),start+ConversionChunk);
    if(end<text.size())while(end>start && (static_cast<unsigned char>(text[end])&0xC0)==0x80)--end;
    return end;
}
size_t ChunkEnd(std::string_view bytes, Encoding encoding, bool last);
void Validate(std::string_view text) {
    if (!simd::IsValidUtf8(text)) throw InvalidByteSequence("Invalid UTF-8.");
}
}
std::string ToUtf8(const std::wstring& text) {
    std::string output;
    for(size_t start=0;start<text.size();) {
        size_t end=(std::min)(text.size(),start+ConversionChunk);
        if(end<text.size() && text[end-1]>=0xD800 && text[end-1]<=0xDBFF)--end;
        int count=static_cast<int>(end-start);
        int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data()+start,count,nullptr,0,nullptr,nullptr);
        if(!length)Fail("Invalid UTF-16 text.");
        auto offset=output.size();output.resize(offset+length);
        if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data()+start,count,output.data()+offset,length,nullptr,nullptr))Fail("Invalid UTF-16 text.");
        start=end;
    }
    return output;
}
std::wstring FromUtf8(const std::string& text) {
    std::wstring output;
    for(size_t start=0;start<text.size();) {
        auto end=Utf8End(text,start);if(end==start)Fail("Invalid UTF-8.");
        int count=static_cast<int>(end-start);
        int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data()+start,count,nullptr,0);
        if(!length)Fail("Invalid UTF-8.");
        auto offset=output.size();output.resize(offset+length);
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data()+start,count,output.data()+offset,length))Fail("Invalid UTF-8.");
        start=end;
    }
    return output;
}
unsigned CodePage(Encoding encoding) {
    if (encoding == Encoding::Ansi || encoding == Encoding::Oem) {
        UINT codePage=encoding==Encoding::Ansi?GetACP():GetOEMCP();
        // Match upstream Encoding_InitDefaults/GetLegacyACP: choosing ANSI/OEM
        // still offers the legacy code page when Windows' UTF-8 locale is on.
        if(codePage==CP_UTF8){const LCTYPE setting=(encoding==Encoding::Ansi?0x00000666:0x00000999)|LOCALE_RETURN_NUMBER;
            UINT legacy=codePage;if(GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT,setting,reinterpret_cast<LPWSTR>(&legacy),sizeof(legacy)/sizeof(wchar_t))&&IsValidCodePage(legacy))codePage=legacy;
        }
        return codePage;
    }
    if (static_cast<unsigned>(encoding) >= static_cast<unsigned>(Encoding::CodePageBase))
        return static_cast<unsigned>(encoding) - static_cast<unsigned>(Encoding::CodePageBase);
    return CP_UTF8;
}
Encoding FromCodePage(unsigned page) {
    if (page == CP_UTF8) return Encoding::Utf8;
    if (page == 1200) return Encoding::Utf16LEPlain;
    if (page == 1201) return Encoding::Utf16BEPlain;
    if (!IsValidCodePage(page)) Fail("This encoding is not installed on Windows.");
    return static_cast<Encoding>(static_cast<unsigned>(Encoding::CodePageBase) + page);
}
bool ParseEncodingName(const std::wstring& value, Encoding& encoding) {
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
    if(testing::encodingAliasParser)return testing::encodingAliasParser(value,encoding);
#endif
    auto normalize=[](const std::string& text){std::string result;for(unsigned char c:text)if(isalnum(c))result+=static_cast<char>(tolower(c));return result;};
    auto name=normalize(ToUtf8(value));
    if(name=="utf16"||name=="utf16le"||name=="unicode"||name=="ucs2"){encoding=Encoding::Utf16LEPlain;return true;}
    if(name=="utf16be"||name=="unicodebe"){encoding=Encoding::Utf16BEPlain;return true;}
    if(name=="utf8bom"||name=="utf8sig"||name=="utf8signature"){encoding=Encoding::Utf8Bom;return true;}
    struct Alias { Encoding value; const char* names; };
    static const Alias aliases[]={
#include "FileEncodingAliases.inc"
    };
    static const auto index=[&]{
        std::unordered_map<std::string,Encoding> result;
        for(const auto& entry:aliases){
            std::string_view names=entry.names;
            while(!names.empty()){
                const auto end=names.find(',');const auto item=names.substr(0,end);
                if(!item.empty())result.try_emplace(normalize(std::string(item)),entry.value);
                if(end==std::string_view::npos)break;names.remove_prefix(end+1);
            }
        }
        return result;
    }();
    if(auto found=index.find(name);found!=index.end()){encoding=found->second;return true;}
    if(name.starts_with("cp"))name.erase(0,2);else if(name.starts_with("windows"))name.erase(0,7);
    if(!name.empty() && std::all_of(name.begin(),name.end(),[](unsigned char c){return isdigit(c)!=0;})){
        unsigned long page=strtoul(name.c_str(),nullptr,10);if(page<=65535 && IsValidCodePage(page)){encoding=FromCodePage(page);return true;}
    }
    return false;
}
Text Decode(std::string_view bytes, Encoding encoding, bool stripBom) {
    Text result{{}, encoding};
    bool utf16 = encoding == Encoding::Utf16LE || encoding == Encoding::Utf16BE
        || encoding == Encoding::Utf16LEPlain || encoding == Encoding::Utf16BEPlain;
    if (utf16) {
        bool le = encoding == Encoding::Utf16LE || encoding == Encoding::Utf16LEPlain;
        if (stripBom && ((le && bytes.starts_with("\xFF\xFE")) || (!le && bytes.starts_with("\xFE\xFF")))) bytes.remove_prefix(2);
        if (bytes.size() % 2) Fail("Truncated UTF-16 file.");
        std::wstring wide(bytes.size()/2, L'\0');
        for (size_t i = 0; i < wide.size(); ++i) {
            auto a = static_cast<unsigned char>(bytes[2*i]), b = static_cast<unsigned char>(bytes[2*i+1]);
            wide[i] = static_cast<wchar_t>(le ? a | (b << 8) : (a << 8) | b);
        }
        result.utf8 = ToUtf8(wide);
    } else if (encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom) {
        if (stripBom && bytes.starts_with("\xEF\xBB\xBF")) bytes.remove_prefix(3);
        Validate(bytes); result.utf8.assign(bytes);
    } else if (!bytes.empty()) {
        auto page=CodePage(encoding);CPINFOEXW info{};if(!GetCPInfoExW(page,0,&info))Fail("Encoding is not installed.");
        for(size_t start=0;start<bytes.size();) {
            size_t end=(std::min)(bytes.size(),start+ConversionChunk);
            if(end<bytes.size() && page==CP_UTF7){
                auto plus=bytes.rfind('+',end-1),minus=bytes.rfind('-',end-1);
                if(plus!=std::string::npos && plus>=start && (minus==std::string::npos||plus>minus)) {
                    if(plus>start)end=plus;else{auto close=bytes.find('-',end);if(close!=std::string::npos && close-start<INT_MAX)end=close+1;else Fail("Oversized UTF-7 shift sequence.");}
                }
            } else if(end<bytes.size() && info.MaxCharSize>1) {
                size_t position=start;
                while(position<end) {
                    const auto c=static_cast<BYTE>(bytes[position]);size_t count=1;
                    if(page==54936 && c>=0x81 && c<=0xFE && position+1<bytes.size()) {
                        auto next=static_cast<BYTE>(bytes[position+1]);count=next>=0x30&&next<=0x39?4:2;
                    } else if(IsDBCSLeadByteEx(page,c))count=2;
                    if(position+count>end){end=position;break;}position+=count;
                }
            }
            if(end<=start || end-start>INT_MAX)Fail("Invalid encoded byte sequence.");
            DWORD flags=(page==CP_UTF7||page==42||page==50220||page==50221||page==50222)?0:MB_ERR_INVALID_CHARS;
            auto count=static_cast<int>(end-start);
            int length=MultiByteToWideChar(page,flags,bytes.data()+start,count,nullptr,0);
            if(!length && GetLastError()==ERROR_INVALID_FLAGS){flags=0;length=MultiByteToWideChar(page,flags,bytes.data()+start,count,nullptr,0);}
            if(!length && GetLastError()==ERROR_NO_UNICODE_TRANSLATION)
                throw InvalidByteSequence("Invalid byte sequence for selected encoding.");
            if(!length)Fail("Cannot decode file.");
            std::wstring wide(length,L'\0');
            if(!MultiByteToWideChar(page,flags,bytes.data()+start,count,wide.data(),length))Fail("Cannot decode file.");
            result.utf8+=ToUtf8(wide);start=end;
        }
    }
    return result;
}
namespace {
std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) Fail("Cannot open file.");
    auto length = in.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > std::string().max_size()) Fail("File exceeds available address space.");
    std::string bytes(static_cast<size_t>(length), '\0'); in.seekg(0);
    if (!bytes.empty() && !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) Fail("Cannot read complete file.");
    return bytes;
}
}
Text ReadWithEncoding(const std::filesystem::path& path, Encoding encoding) { return Decode(ReadBytes(path), encoding); }
Text Read(const std::filesystem::path& path) { return Read(path, {}); }
namespace {
void ValidateEncoding(std::string_view bytes, Encoding encoding) {
    const bool utf16 = encoding == Encoding::Utf16LE || encoding == Encoding::Utf16LEPlain || encoding == Encoding::Utf16BE || encoding == Encoding::Utf16BEPlain;
    if (utf16) {
        if (bytes.size() % 2) Fail("Truncated UTF-16 file.");
        const bool little = encoding == Encoding::Utf16LE || encoding == Encoding::Utf16LEPlain;
        auto unit = [&](size_t offset) { const auto a = static_cast<unsigned char>(bytes[offset]), b = static_cast<unsigned char>(bytes[offset + 1]); return little ? a | (b << 8) : (a << 8) | b; };
        for (size_t offset = 0; offset < bytes.size(); offset += 2) {
            const auto value = unit(offset);
            if (value >= 0xD800 && value <= 0xDBFF) {
                if (offset + 3 >= bytes.size() || unit(offset + 2) < 0xDC00 || unit(offset + 2) > 0xDFFF) Fail("Invalid UTF-16 text.");
                offset += 2;
            } else if (value >= 0xDC00 && value <= 0xDFFF) Fail("Invalid UTF-16 text.");
        }
    } else if (!bytes.empty()) {
        const auto page = CodePage(encoding); CPINFOEXW info{}; if (!GetCPInfoExW(page, 0, &info)) Fail("Encoding is not installed.");
        DWORD flags = page == CP_UTF7 || page == 42 || page == 50220 || page == 50221 || page == 50222 ? 0 : MB_ERR_INVALID_CHARS;
        auto length = MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (!length && GetLastError() == ERROR_INVALID_FLAGS) length = MultiByteToWideChar(page, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (!length && GetLastError() == ERROR_NO_UNICODE_TRANSLATION) throw InvalidByteSequence("Invalid byte sequence for selected encoding.");
        if (!length) Fail("Cannot decode file.");
    }
}
Text DetectUsing(std::string_view bytes, const ReadOptions& options, const std::function<Text(Encoding)>& decode, bool allowUtf16 = true) {
    std::vector<std::pair<Encoding, std::exception_ptr>> failures;
    const auto attempt = [&](Encoding encoding) {
        for (const auto& failure : failures) if (failure.first == encoding) std::rethrow_exception(failure.second);
        try { return decode(encoding); }
        catch (const std::runtime_error&) { failures.emplace_back(encoding, std::current_exception()); throw; }
    };
    if (bytes.starts_with("\xEF\xBB\xBF")) {
        try { return attempt(Encoding::Utf8Bom); }
        catch (const InvalidByteSequence&) { return attempt(Latin1); }
    }
    if (bytes.starts_with("\xFF\xFE")) return attempt(Encoding::Utf16LE);
    if (bytes.starts_with("\xFE\xFF")) return attempt(Encoding::Utf16BE);
    if (options.fileVariables) {
        auto first = std::string(bytes.substr(0, 512));
        auto region = filevars::Recognized(first) || bytes.size() <= 512 ? std::move(first) : std::string(bytes.substr(bytes.size() - 512)); std::string name;
        int enabled=1;
        const bool parse= !filevars::Integer(region,"enable-local-variables",enabled)||enabled!=0;
        if(parse)for(auto key:{"encoding","charset","coding","fileencoding","/*!40101 SET NAMES "})if(!(name=filevars::String(region,key)).empty())break;
        Encoding declared = Encoding::Utf8;
        if(!name.empty()&&ParseEncodingName(FromUtf8(name),declared)
            && declared!=Encoding::Utf16LE && declared!=Encoding::Utf16BE && declared!=Encoding::Utf16LEPlain && declared!=Encoding::Utf16BEPlain) {
            try { return attempt(declared); }
            catch (const InvalidByteSequence&) { return attempt(Latin1); }
        }
    }

    bool triedUtf8 = false;
    if (allowUtf16 && bytes.size() >= 2 && bytes.size() % 2 == 0) {
        const int unicode = simd::DetectUtf16(bytes);
        if (unicode) {
            try { return attempt(unicode == 1 ? Encoding::Utf16LEPlain : Encoding::Utf16BEPlain); }
            catch (const std::runtime_error&) {}
        }
        triedUtf8 = true;
        try { return attempt(Encoding::Utf8); } catch(const std::runtime_error&) {}
        int unicodeFlags = IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_REVERSE_STATISTICS |
            IS_TEXT_UNICODE_CONTROLS | IS_TEXT_UNICODE_REVERSE_CONTROLS | IS_TEXT_UNICODE_ILLEGAL_CHARS;
        if(options.unicodeDetection)IsTextUnicode(bytes.data(), static_cast<int>((std::min)(bytes.size(), size_t(4096))), &unicodeFlags);
        else unicodeFlags=0;
        if(!(unicodeFlags & IS_TEXT_UNICODE_ILLEGAL_CHARS)) {
            const bool little=(unicodeFlags & (IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_CONTROLS))!=0;
            const bool big=(unicodeFlags & (IS_TEXT_UNICODE_REVERSE_STATISTICS | IS_TEXT_UNICODE_REVERSE_CONTROLS))!=0;
            if(bytes.size() >= 16 && little != big) {
                try { return attempt(little ? Encoding::Utf16LEPlain : Encoding::Utf16BEPlain); }
                catch(const std::runtime_error&) {} // Invalid UTF-16 falls through to byte-oriented detection.
            }
        }
    }
    if (!triedUtf8) try { return attempt(Encoding::Utf8); } catch(const std::runtime_error&) {}
    try { return attempt(options.fallback); }
    catch (const InvalidByteSequence&) { return attempt(Latin1); }
}
}
Text DetectSample(const std::string& bytes, bool complete, const ReadOptions& options) {
    return DetectUsing(bytes, options, [&](Encoding encoding) {
        return Decode(std::string_view(bytes).substr(0, ChunkEnd(bytes, encoding, complete)), encoding);
    });
}
namespace {
struct ReadCancelled final : std::exception {
    const char* what() const noexcept override { return "File read cancelled."; }
};
}
Encoding DetectEncoding(std::string_view bytes, const ReadOptions& options) {
    return DetectUsing(bytes, options, [&](Encoding encoding) {
        const auto page = CodePage(encoding);
        if (page == CP_UTF7 || page == 50220 || page == 50221 || page == 50222) {
            if (options.cancel && options.cancel->load()) throw ReadCancelled{};
            Decode(bytes, encoding); // Stateful decoders require the complete encoded stream.
            return Text{{}, encoding};
        }
        for (size_t offset = 0; offset < bytes.size();) {
            if (options.cancel && options.cancel->load()) throw ReadCancelled{};
            auto chunk = bytes.substr(offset, ConversionChunk + SampleLookahead);
            auto count = ChunkEnd(chunk, encoding, offset + chunk.size() == bytes.size());
            if (!count) throw InvalidByteSequence("Invalid byte sequence for selected encoding.");
            auto text = chunk.substr(0, count);
            if (encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom) {
                if (!offset && text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
                Validate(text);
            } else Decode(text, encoding, offset == 0);
            offset += count;
        }
        return Text{{}, encoding};
    }).encoding;
}
Text Read(const std::filesystem::path& path, const ReadOptions& options) { return DetectSample(ReadBytes(path), true, options); }
namespace {
class MappedInput {
    ReadStats* stats;
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
    const char* view = nullptr;
    unsigned long long begin = 0;
    size_t mapped = 0;
    std::string readBuffer;
    DWORD granularity = 65536;
    std::shared_ptr<std::mutex> fallbackMutex = std::make_shared<std::mutex>();
    DWORD sharing = FILE_SHARE_READ;
    bool privateFallback = true;
public:
    unsigned long long size = 0;
    explicit MappedInput(const std::filesystem::path& path, ReadStats* trace = nullptr, ReadBackend backend = ReadBackend::Mapped, bool shareDelete = false, bool deleteOnClose = false) : stats(trace) {
        ReadTimer timer(stats ? &stats->mappingMs : nullptr);
        sharing |= shareDelete ? FILE_SHARE_DELETE : 0;
        file = CreateFileW(path.c_str(), GENERIC_READ | (deleteOnClose ? DELETE : 0), sharing, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | (deleteOnClose ? FILE_FLAG_DELETE_ON_CLOSE : 0), nullptr);
        if (file == INVALID_HANDLE_VALUE) Fail("Cannot open file.");
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(file, &length) || length.QuadPart < 0) { CloseHandle(file); Fail("Cannot read file size."); }
        size = static_cast<unsigned long long>(length.QuadPart);
        if (size && backend == ReadBackend::Mapped
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
            && !testing::bufferedSessions.load()
#endif
        ) mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        SYSTEM_INFO info{}; GetSystemInfo(&info); granularity = info.dwAllocationGranularity;
    }
    // Duplicate existing objects; only buffered fallback needs a private file position.
    MappedInput(const MappedInput& source, int) : stats(nullptr), granularity(source.granularity), fallbackMutex(source.fallbackMutex), sharing(source.sharing), privateFallback(false), size(source.size) {
        const auto process = GetCurrentProcess();
        if (!DuplicateHandle(process, source.file, process, &file, 0, FALSE, DUPLICATE_SAME_ACCESS)) Fail("Cannot duplicate source file handle.");
        if (source.mapping && !DuplicateHandle(process, source.mapping, process, &mapping, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            CloseHandle(file); file = INVALID_HANDLE_VALUE; Fail("Cannot duplicate source mapping handle.");
        }
    }
    ~MappedInput() { if (view) UnmapViewOfFile(view); if (mapping) CloseHandle(mapping); if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    MappedInput(const MappedInput&) = delete;
    MappedInput& operator=(const MappedInput&) = delete;
    // The view expires on the next call. Consumers must finish synchronously.
    std::string_view View(unsigned long long offset, size_t count) {
        count = static_cast<size_t>((std::min)(size - offset, static_cast<unsigned long long>(count)));
        if (!count) return {};
        if (stats) stats->sourceBytes += count;
        if (mapping) {
            if (!view || offset < begin || offset + count > begin + mapped) {
                ReadTimer timer(stats ? &stats->mappingMs : nullptr);
                if (stats) ++stats->mappings;
                if (view) UnmapViewOfFile(view);
                view = nullptr; begin = offset / granularity * granularity;
                size_t wanted = static_cast<size_t>((std::min)(size - begin, 64ULL * 1024 * 1024));
                const size_t required = static_cast<size_t>(offset - begin) + count;
                while (wanted >= required) {
                    view = static_cast<const char*>(MapViewOfFile(mapping, FILE_MAP_READ, static_cast<DWORD>(begin >> 32), static_cast<DWORD>(begin), wanted));
                    if (view) { mapped = wanted; break; }
                    if (wanted == required) break;
                    wanted = (std::max)(required, wanted / 2);
                }
            }
            if (view) return std::string_view(view + static_cast<size_t>(offset - begin), count);
        }
        // Mapping failure degrades to bounded ordinary I/O.
        ReadTimer timer(stats ? &stats->copyOrReadMs : nullptr);
        if (!privateFallback) {
            privateFallback = true;
            const auto reader = ReOpenFile(file, GENERIC_READ, sharing, FILE_FLAG_SEQUENTIAL_SCAN);
            if (reader != INVALID_HANDLE_VALUE) {
                CloseHandle(file); file = reader; fallbackMutex = std::make_shared<std::mutex>(); privateFallback = true;
            }
        }
        std::lock_guard guard(*fallbackMutex);
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
        if (const auto gate = testing::nextFallbackGate.exchange(nullptr)) { testing::fallbackEntered = true; WaitForSingleObject(gate, INFINITE); }
#endif
        LARGE_INTEGER position{}; position.QuadPart = offset;
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) Fail("Cannot seek file.");
        readBuffer.resize(count); DWORD read = 0;
        if (!ReadFile(file, readBuffer.data(), static_cast<DWORD>(count), &read, nullptr) || read != count) Fail("Cannot read complete file.");
        return readBuffer;
    }
    std::string Read(unsigned long long offset, size_t count) {
        auto bytes = View(offset, count);
        ReadTimer timer(stats ? &stats->copyOrReadMs : nullptr);
        return std::string(bytes);
    }
};
size_t ChunkEnd(std::string_view bytes, Encoding encoding, bool last) {
    if (last) return bytes.size();
    size_t end = bytes.size() - SampleLookahead;
    if (encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom) {
        while (end && (static_cast<unsigned char>(bytes[end]) & 0xC0) == 0x80) --end;
    } else if (encoding == Encoding::Utf16LE || encoding == Encoding::Utf16BE || encoding == Encoding::Utf16LEPlain || encoding == Encoding::Utf16BEPlain) {
        end &= ~size_t(1);
        bool le = encoding == Encoding::Utf16LE || encoding == Encoding::Utf16LEPlain;
        auto a = static_cast<unsigned char>(bytes[end-2]), b = static_cast<unsigned char>(bytes[end-1]);
        unsigned c = le ? a | (b << 8) : (a << 8) | b;
        if (c >= 0xD800 && c <= 0xDBFF) end -= 2;
    } else {
        auto page = CodePage(encoding); CPINFOEXW info{};
        if (!GetCPInfoExW(page, 0, &info)) Fail("Encoding is not installed.");
        if (page == CP_UTF7 || page == 50220 || page == 50221 || page == 50222)
            Fail("Streaming load does not support stateful encodings. Use a smaller UTF-8 copy.");
        if (info.MaxCharSize > 1) {
            size_t pos = 0;
            while (pos < end) {
                auto c = static_cast<BYTE>(bytes[pos]); size_t length = 1;
                if (page == 54936 && c >= 0x81 && c <= 0xFE) { auto next = static_cast<BYTE>(bytes[pos+1]); length = next >= 0x30 && next <= 0x39 ? 4 : 2; }
                else if (IsDBCSLeadByteEx(page, c)) length = 2;
                if (pos + length > end) { end = pos; break; }
                pos += length;
            }
        }
    }
    return end;
}
}
struct MappedFile::Impl {
    MappedInput input;
    unsigned long long bytesRead = 0;
    explicit Impl(const std::filesystem::path& path, ReadStats* stats, bool shareDelete, bool deleteOnClose) : input(path, stats, ReadBackend::Mapped, shareDelete, deleteOnClose) {}
    explicit Impl(const Impl& source) : input(source.input, 0) {}
};
MappedFile::MappedFile(const std::filesystem::path& path, ReadStats* stats, bool shareDelete, bool deleteOnClose) : impl(std::make_unique<Impl>(path, stats, shareDelete, deleteOnClose)) {}
MappedFile::~MappedFile() = default;
MappedFile::MappedFile(const MappedFile& source, int) : impl(std::make_unique<Impl>(*source.impl)) {}
std::unique_ptr<MappedFile> MappedFile::NewReadSession() const { return std::unique_ptr<MappedFile>(new MappedFile(*this, 0)); }
unsigned long long MappedFile::Size() const { return impl->input.size; }
unsigned long long MappedFile::BytesRead() const { return impl->bytesRead; }
std::string MappedFile::Read(unsigned long long offset, size_t count) {
    if (offset > Size()) Fail("File offset out of range.");
    auto bytes = impl->input.Read(offset, count); impl->bytesRead += bytes.size(); return bytes;
}
void MappedFile::Visit(unsigned long long offset, size_t count, const std::function<void(std::string_view)>& consume) {
    if (offset > Size() || count > Size() - offset) Fail("Mapped range out of bounds.");
    auto view = impl->input.View(offset, count); impl->bytesRead += view.size(); consume(view);
}
Encoding ReadChunks(const std::filesystem::path& path, const ChunkConsumer& consume, const ReadOptions& options, std::optional<Encoding> forced, ReadStats* stats) {
    if (stats) *stats = {};
    ReadFaults faults(stats);
    ReadTimer total(stats ? &stats->totalMs : nullptr);
    const auto probeStart = stats ? ReadClock() : 0;
    MappedInput input(path, stats, options.backend);
    const auto sample = forced ? std::string{} : input.Read(0, 1024 * 1024 + SampleLookahead);
    size_t validatedSample = 0;
    Encoding encoding = forced.value_or(Encoding::Utf8);
    bool declared = false;
    if (!forced) {
        if (sample.starts_with("\xFF\xFE")) encoding = Encoding::Utf16LE;
        else if (sample.starts_with("\xFE\xFF")) encoding = Encoding::Utf16BE;
        else if (sample.starts_with("\xEF\xBB\xBF")) encoding = Encoding::Utf8Bom;
        else {
            if (options.fileVariables) {
                auto firstAndLast = sample.substr(0, 512);
                if (input.size > 512) {
                    const auto tail = (std::max)(512ULL, input.size - (std::min)(input.size, 512ULL));
                    const auto available = tail < sample.size() ? (std::min)(size_t(512), sample.size() - static_cast<size_t>(tail)) : 0;
                    if (available) firstAndLast += sample.substr(static_cast<size_t>(tail), available);
                    if (available < 512) firstAndLast += input.Read(tail + available, 512 - available);
                }
                const auto region = filevars::Region(firstAndLast);
                int enabled = 1;
                if (!filevars::Integer(region, "enable-local-variables", enabled) || enabled) {
                    for (auto key : {"encoding", "charset", "coding", "fileencoding", "/*!40101 SET NAMES "}) {
                        const auto name = filevars::String(region, key);
                        if (!name.empty() && ParseEncodingName(FromUtf8(name), encoding)) {
                            declared = encoding != Encoding::Utf16LE && encoding != Encoding::Utf16BE && encoding != Encoding::Utf16LEPlain && encoding != Encoding::Utf16BEPlain;
                            if (declared) break;
                        }
                    }
                }
            }
            if (!declared) {
                auto detection = options; detection.fileVariables = false;
                encoding = DetectUsing(sample, detection, [&](Encoding candidate) {
                    const auto count = ChunkEnd(sample, candidate, sample.size() == input.size);
                    auto bytes = std::string_view(sample).substr(0, count);
                    if (candidate == Encoding::Utf8 || candidate == Encoding::Utf8Bom) {
                        ReadTimer timer(stats ? &stats->validationMs : nullptr);
                        if (stats) stats->validationBytes += bytes.size();
                        Validate(bytes); validatedSample = count;
                    } else ValidateEncoding(bytes, candidate);
                    return Text{{}, candidate};
                }, input.size % 2 == 0).encoding;
            }
        }
    }
    unsigned workers = options.validationWorkers;
    if (!workers) workers = input.size >= LargeFileThreshold ? (std::min)(4u, work::Concurrency()) : 1;
    if (encoding != Encoding::Utf8 && encoding != Encoding::Utf8Bom) workers = 1;
    Utf8Validator validator(workers);
    if (stats) stats->probeMs = ReadElapsed(probeStart) - stats->mappingMs - stats->copyOrReadMs - stats->validationMs;
    auto validUtf8 = [&](std::string_view text) {
        ReadTimer timer(stats ? &stats->validationMs : nullptr);
        if (stats) stats->validationBytes += text.size();
        return validator.Validate(text);
    };
    auto deliver = [&](std::string_view text) {
        ReadTimer timer(stats ? &stats->callbackMs : nullptr);
        if (stats) stats->deliveredBytes += text.size();
        consume(text);
    };
    // A sample cannot prove UTF-8 validity. Validate the entire byte stream
    // before delivering data so late legacy bytes cannot leave a mixed buffer.
    bool validatedUtf8 = false;
    if (!forced && !declared && (encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom)) {
        validatedUtf8 = true;
        for (unsigned long long offset = validatedSample; offset < input.size;) {
            if (options.cancel && options.cancel->load(std::memory_order_relaxed)) Fail("File read cancelled.");
            auto bytes = input.View(offset, ConversionChunk + SampleLookahead);
            auto count = ChunkEnd(bytes, encoding, offset + bytes.size() == input.size);
            auto text = std::string_view(bytes.data(), count);
            if (!offset && text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
            if (!validUtf8(text)) {
                if (encoding == Encoding::Utf8Bom) Fail("Invalid UTF-8.");
                validatedUtf8 = false; encoding = options.fallback; break;
            }
            offset += count;
        }
    }
    for (unsigned long long offset = 0; offset < input.size;) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) Fail("File read cancelled.");
        auto bytes = input.View(offset, ConversionChunk + SampleLookahead);
        auto count = ChunkEnd(bytes, encoding, offset + bytes.size() == input.size);
        bytes = bytes.substr(0, count);
        if (encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom) {
            std::string_view text(bytes);
            if (!offset && text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
            if (!validatedUtf8 && !validUtf8(text)) Fail("Invalid UTF-8.");
            deliver(text);
        } else {
            Text decoded;
            { ReadTimer timer(stats ? &stats->decodeMs : nullptr);
                if (stats) stats->decodedBytes += bytes.size();
                decoded = Decode(bytes, encoding, offset == 0);
            }
            deliver(decoded.utf8);
        }
        offset += count;
    }
    return encoding;
}
Text ReadForReload(const std::filesystem::path& path,Encoding current,const ReadOptions& options) {
    auto bytes=ReadBytes(path);
    if(bytes.empty())return Decode(bytes,current);
    if(bytes.starts_with("\xEF\xBB\xBF")||bytes.starts_with("\xFF\xFE")||bytes.starts_with("\xFE\xFF"))return DetectSample(bytes,true,options);
    const bool unicode=current==Encoding::Utf16LE||current==Encoding::Utf16BE||current==Encoding::Utf16LEPlain||current==Encoding::Utf16BEPlain;
    const bool ascii=simd::IsAscii(bytes);
    auto actual=options;
    if(!unicode&&current!=Encoding::Utf8&&current!=Encoding::Utf8Bom)actual.fallback=current;
    Text detected;
    try{detected=DetectSample(bytes,true,actual);}catch(const std::runtime_error&){detected=DetectSample(bytes,true,options);}
    if(detected.encoding==Encoding::Utf16LEPlain||detected.encoding==Encoding::Utf16BEPlain)return detected;
    if(ascii&&!unicode)return Decode(bytes,current==Encoding::Utf8Bom?Encoding::Utf8:current);
    try{Validate(bytes);return Decode(bytes,Encoding::Utf8);}catch(const std::runtime_error&){}
    if(!unicode&&current!=Encoding::Utf8&&current!=Encoding::Utf8Bom){try{return Decode(bytes,current);}catch(const std::runtime_error&){}}
    return detected;
}
namespace {
std::string EncodeImpl(const Text& text,bool allowLoss) {
    Validate(text.utf8);
    std::string bytes;
    if (text.encoding == Encoding::Utf8Bom) bytes = "\xEF\xBB\xBF";
    bool utf16 = text.encoding == Encoding::Utf16LE || text.encoding == Encoding::Utf16BE
        || text.encoding == Encoding::Utf16LEPlain || text.encoding == Encoding::Utf16BEPlain;
    if (utf16) {
        bool le = text.encoding == Encoding::Utf16LE || text.encoding == Encoding::Utf16LEPlain;
        if (text.encoding == Encoding::Utf16LE || text.encoding == Encoding::Utf16BE) bytes = le ? "\xFF\xFE" : "\xFE\xFF";
        const auto wide = FromUtf8(text.utf8);
        const auto prefix = bytes.size();
        bytes.resize(prefix + wide.size() * 2);
        for (size_t i = 0; i < wide.size(); ++i) {
            const auto c = wide[i];
            bytes[prefix + i * 2] = static_cast<char>(le ? c & 255 : c >> 8);
            bytes[prefix + i * 2 + 1] = static_cast<char>(le ? c >> 8 : c & 255);
        }
    } else if (text.encoding == Encoding::Utf8 || text.encoding == Encoding::Utf8Bom) bytes += text.utf8;
    else {
        auto wide=FromUtf8(text.utf8);if(wide.empty())return {};
        auto page=CodePage(text.encoding);
        for(size_t start=0;start<wide.size();) {
            auto end=(std::min)(wide.size(),start+ConversionChunk);
            if(end<wide.size() && wide[end-1]>=0xD800 && wide[end-1]<=0xDBFF)--end;
            BOOL usedDefault=FALSE;const bool unicodePage=page==CP_UTF8||page==CP_UTF7||page==54936;
            DWORD flags=page==CP_UTF8?WC_ERR_INVALID_CHARS:unicodePage?0:WC_NO_BEST_FIT_CHARS;
            auto* used=unicodePage?nullptr:&usedDefault;auto count=static_cast<int>(end-start);
            int length=WideCharToMultiByte(page,flags,wide.data()+start,count,nullptr,0,nullptr,used);
            if(!length && GetLastError()==ERROR_INVALID_FLAGS){flags=0;length=WideCharToMultiByte(page,flags,wide.data()+start,count,nullptr,0,nullptr,used);}
            if(!length||(!allowLoss&&usedDefault))Fail("Selected encoding cannot represent every character; original retained.");
            auto offset=bytes.size();bytes.resize(offset+length);
            if(!WideCharToMultiByte(page,flags,wide.data()+start,count,bytes.data()+offset,length,nullptr,used)||(!allowLoss&&usedDefault))Fail("Encoding conversion failed; original retained.");
            start=end;
        }
        if (!allowLoss && Decode(bytes, text.encoding).utf8 != text.utf8) Fail("Encoding conversion would lose information; original retained.");
    }
    return bytes;
}
}
std::string Encode(const Text& text) {return EncodeImpl(text,false);}
std::string EncodeLossy(const Text& text) {return EncodeImpl(text,true);}
void EncodeChunks(const Text& text, const ChunkConsumer& consume) {
    if (text.encoding == Encoding::Utf8 || text.encoding == Encoding::Utf8Bom) {
        Validate(text.utf8);
        if (text.encoding == Encoding::Utf8Bom) consume("\xEF\xBB\xBF");
        consume(text.utf8);
    } else {
        const auto bytes = Encode(text);
        consume(bytes);
    }
}
void WriteNew(const std::filesystem::path& path,const Text& text) {
    const auto bytes=Encode(text);
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE)Fail("Cannot create a new backup file without overwriting existing data.");
    bool ok=true;for(size_t offset=0;offset<bytes.size();) {
        DWORD written=0,count=static_cast<DWORD>((std::min)(ConversionChunk,bytes.size()-offset));
        if(!WriteFile(file,bytes.data()+offset,count,&written,nullptr)||written!=count){ok=false;break;}offset+=written;
    }
    if(ok)ok=FlushFileBuffers(file)!=FALSE;CloseHandle(file);
    if(!ok){DeleteFileW(path.c_str());Fail("Cannot write complete backup file.");}
}
namespace {
bool WriteLinkedFile(const std::filesystem::path& path,const std::function<void(const ChunkConsumer&)>& produce, const std::function<void()>& validate, const std::function<void()>& committed) {
    const auto stream=path.native().find(L':',path.root_path().native().size());
    HANDLE probe=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(probe==INVALID_HANDLE_VALUE)Fail("Cannot inspect destination file identity; original retained.");
    BY_HANDLE_FILE_INFORMATION attributes{};bool inspected=GetFileInformationByHandle(probe,&attributes)!=FALSE;CloseHandle(probe);
    if(!inspected)Fail("Cannot inspect destination hard links; original retained.");
    bool linked=attributes.nNumberOfLinks>1;
    if(!linked && stream==std::wstring::npos)return false;
    HANDLE original=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(original==INVALID_HANDLE_VALUE)Fail("Cannot exclusively update hard-linked file; original retained.");
    HANDLE rollback=INVALID_HANDLE_VALUE;std::filesystem::path backup;
    try {
        GetFileInformationByHandle(original,&attributes);
        if (validate) validate();
        for(unsigned i=0;i<100 && rollback==INVALID_HANDLE_VALUE;++i){backup=stream==std::wstring::npos?path:std::filesystem::path(path.native().substr(0,stream));backup+=L".multipad4-rollback-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(i)+L".bak";
            rollback=CreateFileW(backup.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
            if(rollback==INVALID_HANDLE_VALUE&&GetLastError()!=ERROR_FILE_EXISTS)break;
        }
        if(rollback==INVALID_HANDLE_VALUE)Fail("Cannot create hard-link rollback copy; original retained.");
        std::vector<char> buffer(1024*1024);LARGE_INTEGER zero{};
        auto copy=[&](HANDLE source,HANDLE destination){
            if(!SetFilePointerEx(source,zero,nullptr,FILE_BEGIN)||!SetFilePointerEx(destination,zero,nullptr,FILE_BEGIN))return false;
            for(;;){DWORD read=0,written=0;if(!ReadFile(source,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))return false;if(!read)break;
                if(!WriteFile(destination,buffer.data(),read,&written,nullptr)||written!=read)return false;}
            return SetEndOfFile(destination)&&FlushFileBuffers(destination);
        };
        if(!copy(original,rollback))Fail("Cannot complete hard-link rollback copy; original retained.");
        if (validate) validate();
        bool saved=SetFilePointerEx(original,zero,nullptr,FILE_BEGIN)!=FALSE;
        try {
            produce([&](std::string_view bytes) {
                for(size_t offset=0;saved&&offset<bytes.size();){DWORD written=0,count=static_cast<DWORD>((std::min)(ConversionChunk,bytes.size()-offset));saved=WriteFile(original,bytes.data()+offset,count,&written,nullptr)&&written==count;offset+=written;}
            });
        } catch (...) { saved = false; }
        if(saved)saved=SetEndOfFile(original)&&FlushFileBuffers(original);
        if(!saved){
            bool restored=copy(rollback,original);
            if(restored)restored=SetFileTime(original,&attributes.ftCreationTime,&attributes.ftLastAccessTime,&attributes.ftLastWriteTime)!=FALSE;
            CloseHandle(original);original=INVALID_HANDLE_VALUE;CloseHandle(rollback);rollback=INVALID_HANDLE_VALUE;
            if(restored){DeleteFileW(backup.c_str());Fail("Hard-linked save failed; original restored from rollback copy.");}
            throw std::runtime_error("Save and rollback failed. Recover original bytes from: "+ToUtf8(backup.wstring()));
        }
        CloseHandle(original);original=INVALID_HANDLE_VALUE;CloseHandle(rollback);rollback=INVALID_HANDLE_VALUE;
        if (committed) committed();
        if(!DeleteFileW(backup.c_str()))throw std::runtime_error("Hard-linked file saved, but rollback copy could not be removed: "+ToUtf8(backup.wstring()));
        return true;
    }catch(...){
        if(original!=INVALID_HANDLE_VALUE)CloseHandle(original);
        if(rollback!=INVALID_HANDLE_VALUE){CloseHandle(rollback);DeleteFileW(backup.c_str());}
        throw;
    }
}
}
void WriteAtomic(const std::filesystem::path& path, const Text& text) {
    const auto bytes = Encode(text);
    WriteAtomicChunks(path, [&](const ChunkConsumer& consume) { consume(bytes); });
}
void WriteAtomicChunks(const std::filesystem::path& path, const std::function<void(const ChunkConsumer&)>& produce, const std::function<void()>& validate, const std::function<void()>& committed) {
    if (validate) validate();
    auto absolute = std::filesystem::absolute(path);
    const DWORD originalAttributes = GetFileAttributesW(absolute.c_str());
    const bool stream=absolute.native().find(L':',absolute.root_path().native().size())!=std::wstring::npos;
    if(originalAttributes != INVALID_FILE_ATTRIBUTES && (originalAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)))
        Fail("Destination is read-only or is a directory; original retained.");
    if(originalAttributes != INVALID_FILE_ATTRIBUTES && WriteLinkedFile(absolute,produce,validate,committed))return;

    // CREATE_NEW prevents collisions; the temporary file stays on the same volume.
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned i = 0; i < 100 && file == INVALID_HANDLE_VALUE; ++i) {
        temporary = absolute;
        if (!stream) temporary += L".multipad4-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(i) + L".tmp";
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) Fail("Cannot create temporary save file.");
    }
    if (file == INVALID_HANDLE_VALUE) Fail("Cannot allocate temporary save file.");
    bool ok=true;
    try {
        produce([&](std::string_view bytes) {
            for(size_t offset=0;offset<bytes.size();) {
                DWORD written=0,count=static_cast<DWORD>((std::min)(ConversionChunk,bytes.size()-offset));
                if(!WriteFile(file,bytes.data()+offset,count,&written,nullptr)||written!=count)Fail("Cannot write complete file; original retained.");
                offset+=written;
            }
        });
    } catch (...) { CloseHandle(file); DeleteFileW(temporary.c_str()); throw; }
    if(ok)ok=FlushFileBuffers(file)!=FALSE;
    CloseHandle(file);
    if (!ok) { DeleteFileW(temporary.c_str()); Fail("Cannot write complete file; original retained."); }
    if (stream) { if (committed) committed(); return; } // A newly created ADS has no previous contents to replace.
    try { if (validate) validate(); }
    catch (...) { DeleteFileW(temporary.c_str()); throw; }
    bool exists = GetFileAttributesW(absolute.c_str()) != INVALID_FILE_ATTRIBUTES;
    ok = exists ? ReplaceFileW(absolute.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr) != FALSE
                : MoveFileExW(temporary.c_str(), absolute.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) { DeleteFileW(temporary.c_str()); Fail("Cannot replace destination; original retained."); }
    if (committed) committed();
    if(originalAttributes != INVALID_FILE_ATTRIBUTES) {
        // ReplaceFile merges the destination security descriptor and creation
        // metadata; explicitly preserve user-visible DOS attributes too.
        DWORD attributes = originalAttributes & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
            FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY);
        if(!attributes)attributes=FILE_ATTRIBUTE_NORMAL;
        if(!SetFileAttributesW(absolute.c_str(), attributes)) Fail("File saved, but its attributes could not be restored.");
    }
}
FileTimes GetTimes(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) Fail("Cannot read file times.");
    auto value = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return {value(data.ftCreationTime), value(data.ftLastAccessTime), value(data.ftLastWriteTime)};
}
void SetTimes(const std::filesystem::path& path, const FileTimes& times) {
    HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) Fail("Cannot open file to update timestamps.");
    auto time = [](unsigned long long v) { return FILETIME{static_cast<DWORD>(v), static_cast<DWORD>(v >> 32)}; };
    auto c = time(times.creation), a = time(times.access), w = time(times.write);
    bool ok = SetFileTime(file, &c, &a, &w) != FALSE; CloseHandle(file);
    if (!ok) Fail("Cannot update file timestamps.");
}
void Touch(const std::filesystem::path& path) {
    auto times = GetTimes(path); FILETIME now{}; GetSystemTimeAsFileTime(&now);
    times.write = (static_cast<unsigned long long>(now.dwHighDateTime) << 32) | now.dwLowDateTime; SetTimes(path, times);
}
const wchar_t* EncodingName(Encoding e) {
    switch(e) {
    case Encoding::Utf8Bom: return L"UTF-8 BOM";
    case Encoding::Utf16LE: return L"UTF-16 LE";
    case Encoding::Utf16BE: return L"UTF-16 BE";
    case Encoding::Utf16LEPlain: return L"UTF-16 LE";
    case Encoding::Utf16BEPlain: return L"UTF-16 BE";
    case Encoding::Ansi: return L"ANSI";
    case Encoding::Oem: return L"OEM";
    case Encoding::Utf8: return L"UTF-8";
    default: {
        thread_local wchar_t label[128]; CPINFOEXW info{};
        if (GetCPInfoExW(CodePage(e), 0, &info)) { wcsncpy_s(label, info.CodePageName, _TRUNCATE); return label; }
        return L"Unknown encoding";
    }
    }
}
}


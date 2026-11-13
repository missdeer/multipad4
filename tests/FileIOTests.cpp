#include "FileIO.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    auto dir = std::filesystem::temp_directory_path() / (L"MultiPad4-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(dir);
        const std::string boundary = std::string(16 * 1024 * 1024 - 1, 'a') + "😀";
        Require(files::ToUtf8(files::FromUtf8(boundary)) == boundary, "Chunk boundary split a Unicode character");
        const std::wstring surrogateBoundary = std::wstring(16 * 1024 * 1024 - 1, L'a') + L"😀";
        Require(files::FromUtf8(files::ToUtf8(surrogateBoundary)) == surrogateBoundary, "Chunk boundary split a surrogate pair");
        auto path = dir / L"中文测试.txt";
        if (IsValidCodePage(932)) {
            const auto shiftJis = files::FromCodePage(932);
            const std::string text = std::string(16 * 1024 * 1024 - 1, 'a') + "日本語";
            const auto bytes = files::Encode({text, shiftJis});
            Require(files::Decode(bytes, shiftJis).utf8 == text, "DBCS character split at conversion boundary");
        }
        for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf8Bom, files::Encoding::Utf16LE, files::Encoding::Utf16BE, files::FromCodePage(1252)}) {
            const auto content = std::string(16 * 1024 * 1024 - 1, 'x') + "café\r\n" + "\xEF\xBB\xBF" + "tail";
            // The legacy page cannot represent the embedded U+FEFF.
            auto expected = encoding == files::FromCodePage(1252) ? std::string(16 * 1024 * 1024 - 1, 'x') + "café" : content;
            files::WriteAtomic(path, {expected, encoding});
            std::string actual;
            auto detected = files::ReadChunks(path, [&](std::string_view bytes) { Require(bytes.size() <= 64 * 1024 * 1024, "Unbounded chunk"); actual += bytes; }, {}, encoding);
            Require(actual == expected && detected == encoding, "Mapped streaming conversion changed text");
        }
        {
            std::string actual;
            auto detected = files::ReadChunks(path, [&](std::string_view bytes) { actual += bytes; }, {true, true, files::FromCodePage(1252)});
            Require(detected == files::FromCodePage(1252) && actual.ends_with("café"), "Late non-UTF8 bytes lost during detection");
        }
        files::WriteAtomic(path, {"retained"});
        bool producerFailed = false;
        try { files::WriteAtomicChunks(path, [](const files::ChunkConsumer& consume) { consume("partial"); throw std::runtime_error("injected"); }); } catch (...) { producerFailed = true; }
        Require(producerFailed && files::Read(path).utf8 == "retained", "Streaming failure damaged destination");
        const std::string sample = "hello\r\n中文 😀\nlast\r";
        for (auto encoding : {files::Encoding::Utf8, files::Encoding::Utf8Bom, files::Encoding::Utf16LE, files::Encoding::Utf16BE}) {
            files::WriteAtomic(path, {sample, encoding});
            auto result = files::Read(path);
            Require(result.utf8 == sample && result.encoding == encoding, "Unicode/encoding/EOL round trip failed");
            files::WriteAtomic(path, {"replacement", encoding});
            Require(files::Read(path).utf8 == "replacement", "Existing-file replacement failed");
            files::WriteAtomic(path, {"", encoding});
            Require(files::Read(path).utf8.empty(), "Empty file failed");
        }
        for (auto bytes : {std::string("\xFF\xFE\x00", 3), std::string("\xFF\xFE\x00\xD8", 4)}) {
            { std::ofstream out(path, std::ios::binary); out.write(bytes.data(), bytes.size()); }
            bool rejected = false; try { files::Read(path); } catch (const std::exception&) { rejected = true; }
            Require(rejected, "Malformed or binary input accepted");
        }
        auto cp1252 = files::FromCodePage(1252);
        Require(files::Decode(files::Encode({"plain ASCII", files::Encoding::Ansi}), files::Encoding::Ansi).utf8 == "plain ASCII", "System ANSI code page round trip failed");
        files::Encoding named;
        Require(files::ParseEncodingName(L"iso-ir-100", named) && named == files::FromCodePage(28591), "Upstream encoding alias missing");
        Require(!files::ParseEncodingName(L"unknown-encoding", named), "Unknown encoding alias accepted");
        const std::string cafe = "caf\xC3\xA9";
        files::WriteAtomic(path, {cafe, cp1252});
        Require(files::ReadWithEncoding(path, cp1252).utf8 == cafe, "Legacy round trip failed");
        bool lossy = false; try { files::WriteAtomic(path, {sample, cp1252}); } catch (const std::exception&) { lossy = true; }
        Require(lossy && files::ReadWithEncoding(path, cp1252).utf8 == cafe, "Lossy encoding changed original");
        auto lossyText=files::Decode(files::EncodeLossy({sample,cp1252}),cp1252);
        Require(lossyText.utf8!=sample && lossyText.utf8.find('?')!=std::string::npos,"Explicit lossy conversion did not produce placeholders");
        files::WriteAtomic(path,{"ASCII",cp1252});Require(files::ReadForReload(path,cp1252).encoding==cp1252,"Reload lost explicit code page for ASCII");
        files::WriteAtomic(path,{"",files::Encoding::Utf16BE});Require(files::ReadForReload(path,files::Encoding::Utf16BE).encoding==files::Encoding::Utf16BE,"Reload lost empty Unicode encoding");
        files::WriteAtomic(path,{"中文 UTF-8",files::Encoding::Utf8Bom});Require(files::ReadForReload(path,cp1252).encoding==files::Encoding::Utf8Bom,"Reload ignored new Unicode BOM");
        for (auto encoding : {files::Encoding::Utf16LEPlain, files::Encoding::Utf16BEPlain}) {
            files::WriteAtomic(path, {sample, encoding});
            Require(files::ReadWithEncoding(path, encoding).utf8 == sample, "UTF16 without BOM failed");
            Require(files::Read(path,{false,true,files::Encoding::Ansi}).encoding==encoding,"Skipping statistical Unicode detection disabled deterministic UTF16 detection");
        }
        files::WriteAtomic(path, {std::string("a\0b", 3)});
        Require(files::Read(path).utf8 == std::string("a\0b", 3), "Embedded NUL round trip failed");
        files::WriteAtomic(path, {"# coding: windows-1252\n" + cafe, cp1252});
        Require(files::Read(path).encoding == cp1252, "File variable encoding detection failed");
        files::WriteAtomic(path, {std::string(1024, ' ') + "\n# Local Variables:\n# coding: windows-1252\n# End:\n" + cafe, cp1252});
        Require(files::Read(path).encoding == cp1252, "Tail file variable encoding detection failed");
        auto times = files::GetTimes(path); files::Touch(path); files::SetTimes(path, times);
        Require(files::GetTimes(path).write == times.write, "Timestamp preservation failed");
        files::WriteAtomic(path, {sample});
        bool newRejected=false;try{files::WriteNew(path,{"must not overwrite"});}catch(const std::exception&){newRejected=true;}
        Require(newRejected && files::Read(path).utf8==sample,"Backup CREATE_NEW overwrote existing data");
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE);
        files::WriteAtomic(path, {sample});
        Require((GetFileAttributesW(path.c_str()) & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE)) == (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE), "Replacement lost DOS attributes");
        HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "Cannot acquire sharing lock for regression");
        bool lockedRejected = false; try { files::WriteAtomic(path, {"lost"}); } catch (const std::exception&) { lockedRejected = true; }
        CloseHandle(locked);
        Require(lockedRejected && files::Read(path).utf8 == sample, "Sharing violation damaged original");
        auto alias=dir/L"hard-link.txt";
        Require(CreateHardLinkW(alias.c_str(),path.c_str(),nullptr)!=FALSE,"Cannot create hard link regression fixture");
        files::WriteAtomic(path,{"hard-linked update"});
        Require(std::filesystem::equivalent(path,alias)&&files::Read(alias).utf8=="hard-linked update","Save replaced hard-linked inode");
        files::WriteAtomic(alias,{sample});
        Require(files::Read(path).utf8==sample,"Hard-linked alias update did not reach original");
        files::WriteAtomic(path,{"short"});
        HANDLE rangeLock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(rangeLock!=INVALID_HANDLE_VALUE,"Cannot open hard-link rollback range fixture");
        OVERLAPPED lockRange{};lockRange.Offset=8;
        Require(LockFileEx(rangeLock,LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&lockRange)!=FALSE,"Cannot acquire shared range lock");
        bool restoredFailure=false;try{files::WriteAtomic(alias,{"long replacement touches locked byte"});}catch(const std::exception& error){restoredFailure=std::string(error.what()).find("original restored")!=std::string::npos;}
        UnlockFileEx(rangeLock,0,1,0,&lockRange);CloseHandle(rangeLock);
        Require(restoredFailure&&files::Read(path).utf8=="short"&&files::Read(alias).utf8=="short","Hard-link failed write did not restore original bytes");
        files::WriteAtomic(path,{sample});
        HANDLE aliasLock=CreateFileW(alias.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(aliasLock!=INVALID_HANDLE_VALUE,"Cannot lock linked file");
        bool aliasRejected=false;try{files::WriteAtomic(path,{"lost"});}catch(const std::exception&){aliasRejected=true;}CloseHandle(aliasLock);
        Require(aliasRejected&&files::Read(alias).utf8==sample,"Locked hard-link save damaged original");
        SetFileAttributesW(alias.c_str(),FILE_ATTRIBUTE_READONLY);
        bool readonlyLinkRejected=false;try{files::WriteAtomic(path,{"lost"});}catch(const std::exception&){readonlyLinkRejected=true;}
        SetFileAttributesW(alias.c_str(),FILE_ATTRIBUTE_NORMAL);
        Require(readonlyLinkRejected&&files::Read(path).utf8==sample,"Read-only hard-link save damaged original");
        Require(DeleteFileW(alias.c_str())!=FALSE,"Cannot remove linked regression fixture");
        auto stream=std::filesystem::path(path.wstring()+L":notes");
        files::WriteAtomic(stream,{"first stream content"});
        Require(files::Read(stream).utf8=="first stream content"&&files::Read(path).utf8==sample,"New ADS save changed default stream");
        files::WriteAtomic(stream,{"replacement"});
        Require(files::Read(stream).utf8=="replacement"&&files::Read(path).utf8==sample,"Existing ADS save changed another stream");
        HANDLE streamLock=CreateFileW(stream.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(streamLock!=INVALID_HANDLE_VALUE,"Cannot lock ADS fixture");
        bool streamRejected=false;try{files::WriteAtomic(stream,{"lost"});}catch(const std::exception&){streamRejected=true;}CloseHandle(streamLock);
        Require(streamRejected&&files::Read(stream).utf8=="replacement"&&files::Read(path).utf8==sample,"Locked ADS save damaged data");
        Require(DeleteFileW(stream.c_str())!=FALSE,"Cannot remove ADS fixture");
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        bool rejected = false; try { files::WriteAtomic(path, {"lost"}); } catch (const std::exception&) { rejected = true; }
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        Require(rejected && files::Read(path).utf8 == sample, "Failed save did not preserve original");
        Require(std::distance(std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator{}) == 1, "Temporary save file leaked");
        std::filesystem::remove_all(dir); std::cout << "All file I/O checks passed\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; std::error_code ec; std::filesystem::remove_all(dir, ec); return 1;
    }
}

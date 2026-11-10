#pragma once
#include <filesystem>
#include <string>
namespace files {
enum class Encoding : unsigned { Utf8, Utf8Bom, Utf16LE, Utf16BE, Utf16LEPlain, Utf16BEPlain, Ansi, Oem, CodePageBase = 0x10000 };
struct Text { std::string utf8; Encoding encoding = Encoding::Utf8; };
struct ReadOptions { bool unicodeDetection = true, fileVariables = true; Encoding fallback = Encoding::Ansi; };
Text Read(const std::filesystem::path& path);
Text Read(const std::filesystem::path& path, const ReadOptions& options);
Text ReadWithEncoding(const std::filesystem::path& path, Encoding encoding);
Text ReadForReload(const std::filesystem::path& path, Encoding current, const ReadOptions& options = {});
Encoding FromCodePage(unsigned codePage);
unsigned CodePage(Encoding encoding);
bool ParseEncodingName(const std::wstring& name, Encoding& encoding);
std::string Encode(const Text& text);
std::string EncodeLossy(const Text& text);
Text Decode(const std::string& bytes, Encoding encoding);
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

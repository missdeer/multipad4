#include "FileReplace.h"
#include <windows.h>
#include <array>
#include <cassert>
#include <cstring>
#include <forward_list>
#include <algorithm>
#include "Scintilla.h"
#include "ScintillaTypes.h"
#include "ILoader.h"
#include "ILexer.h"
#include "Debugging.h"
#include "VectorISA.h"
#include "CharacterSet.h"
#include "Position.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
namespace files {
Text ReadReplacement(const std::filesystem::path& path, const std::atomic<bool>& cancel) {
    struct Input {
        HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
        const char* view = nullptr;
        ~Input() { if (view) UnmapViewOfFile(view); if (mapping) CloseHandle(mapping); if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    } input;
    if (cancel.load()) throw std::runtime_error("File read cancelled.");
    input.file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    LARGE_INTEGER size{};
    if (input.file == INVALID_HANDLE_VALUE || !GetFileSizeEx(input.file, &size) || size.QuadPart < 0) throw std::runtime_error("Cannot read replacement file.");
    if (size.QuadPart) {
        input.mapping = CreateFileMappingW(input.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (input.mapping) input.view = static_cast<const char*>(MapViewOfFile(input.mapping, FILE_MAP_READ, 0, 0, 0));
        if (!input.view) throw std::runtime_error("Cannot map replacement input; file retained.");
    }
    ReadOptions options; options.cancel = &cancel;
    const auto encoding = DetectEncoding(std::string_view(input.view ? input.view : "", static_cast<size_t>(size.QuadPart)), options);
    if (cancel.load()) throw std::runtime_error("File read cancelled.");
    const auto page = CodePage(encoding);
    if (page == CP_UTF7 || page == 50220 || page == 50221 || page == 50222) {
        auto decoded = Decode(std::string_view(input.view ? input.view : "", static_cast<size_t>(size.QuadPart)), encoding);
        if (cancel.load()) throw std::runtime_error("File read cancelled.");
        return decoded;
    }
    Text result;
    result.encoding = ReadChunks(path, [&](std::string_view chunk) { result.utf8.append(chunk); }, options, encoding);
    return result;
}
ReplaceResult ReplaceContents(Text text, const ReplacePattern& pattern, const std::atomic<bool>& cancel) {
    using namespace Scintilla;
    using namespace Scintilla::Internal;
    ReplaceResult result;
    result.text.encoding = text.encoding;
    if (text.utf8.find('\0') != std::string::npos || pattern.needle.empty()) return result;
    auto source = std::make_shared<const std::string>(std::move(text.utf8));
    result.hits.snapshot = std::make_shared<const large::Document::Snapshot>(large::Document::FromUtf8(source)->Capture());
    result.hits.archive = std::make_shared<large::HitArchive>();
    Document document(DocumentOption::StylesNone);
    document.SetDBCSCodePage(65001);
    document.SetCaseFolder(std::make_unique<CaseFolderUnicode>());
    document.SetUndoCollection(false);
    document.InsertString(0, *source);
    const auto flags = static_cast<FindOption>(pattern.flags);
    auto search = [&](Sci::Position start) {
        if (cancel.load()) throw std::runtime_error("Replacement cancelled.");
        Sci::Position length = pattern.needle.size();
        auto found = document.FindText(start, document.Length(), pattern.needle.data(), flags, &length);
        if (found == -2) throw std::runtime_error("Invalid regular expression.");
        return std::pair(found, length);
    };
    const bool singlePass = (pattern.flags & (SCFIND_REGEXP | SCFIND_WHOLEWORD | SCFIND_WORDSTART)) == 0;
    // Preserve the original Finder positions even when replacement changes later matches.
    if (!singlePass) for (Sci::Position start = 0; start <= document.Length();) {
        const auto [found, length] = search(start);
        if (found < 0) break;
        const auto line = document.LineFromPosition(found);
        large::SearchHit hit; hit.start = found; hit.end = found + length;
        hit.line = line; hit.lineStart = document.LineStart(line); result.hits.archive->Append(hit);
        if (length) start = found + length;
        else { if (found == document.Length()) break; start = document.MovePositionOutsideChar(found + 1, 1, true); }
    }
    Sci::Position delta = 0;
    size_t originalLine = 0, originalLineStart = 0, nextBreak = source->find_first_of("\r\n");
    for (Sci::Position start = 0; start <= document.Length();) {
        const auto [found, length] = search(start);
        if (found < 0) break;
        if (singlePass) {
            large::SearchHit hit; hit.start = found - delta; hit.end = hit.start + length;
            while (nextBreak < hit.start) {
                const size_t width = (*source)[nextBreak] == '\r' && nextBreak + 1 < source->size() && (*source)[nextBreak + 1] == '\n' ? 2 : 1;
                if (nextBreak + width > hit.start) break;
                ++originalLine; originalLineStart = nextBreak + width;
                nextBreak = source->find_first_of("\r\n", originalLineStart);
            }
            hit.line = originalLine; hit.lineStart = originalLineStart;
            result.hits.archive->Append(hit);
        }
        Sci::Position size = pattern.replacement.size();
        const char* replacement = pattern.replacement.data();
        if (pattern.captures) replacement = document.SubstituteByPosition(replacement, &size);
        // Substitute storage belongs to the matcher, and remains valid through these edits.
        document.DeleteChars(found, length);
        document.InsertString(found, replacement, size);
        delta += size - length;
        ++result.replacements; start = found + size;
        if (!length) { if (start >= document.Length()) break; start = document.MovePositionOutsideChar(start + 1, 1, true); }
    }
    if (cancel.load()) throw std::runtime_error("Replacement cancelled.");
    result.text.utf8.assign(document.RangePointer(0, document.Length()), document.Length());
    return result;
}
ReplaceVersion ReplacementVersion(const std::filesystem::path& path) {
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot inspect replacement file.");
    BY_HANDLE_FILE_INFORMATION info{}; const bool ok = GetFileInformationByHandle(handle, &info) != FALSE; CloseHandle(handle);
    if (!ok || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) throw std::runtime_error("Replacement file changed.");
    auto value = [](DWORD hi, DWORD lo) { return (static_cast<unsigned long long>(hi) << 32) | lo; };
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, info.nNumberOfLinks,
        value(info.nFileSizeHigh, info.nFileSizeLow), value(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime)};
}
void CommitReplacement(const std::filesystem::path& path, const Text& text, const ReplaceVersion& version,
    const std::atomic<bool>& cancel, const std::function<void()>& committed) {
    auto validate = [&] {
        if (cancel.load() || ReplacementVersion(path) != version) throw std::runtime_error("Replacement conflict; file retained.");
    };
    const auto bytes = Encode(text);
    WriteAtomicChunks(path, [&](const ChunkConsumer& consume) { consume(bytes); }, validate, committed);
}
}

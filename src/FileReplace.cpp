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
    if (pattern.needle.empty()) return result;
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
    // The temporary edit archive maps original UTF-8 spans to sequential replacement spans.
    result.edits = std::make_shared<ReplacementEdits>();
    auto& edits = *result.edits;
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
        edits.Append({static_cast<large::Position>(found - delta), static_cast<large::Position>(found - delta + length),
            static_cast<large::Position>(found), static_cast<large::Position>(found + size)});
        delta += size - length;
        ++result.replacements; start = found + size;
        if (!length) { if (start >= document.Length()) break; start = document.MovePositionOutsideChar(start + 1, 1, true); }
    }
    if (cancel.load()) throw std::runtime_error("Replacement cancelled.");
    result.text.utf8.assign(document.RangePointer(0, document.Length()), document.Length());
    result.hits.replacementTargets = std::make_shared<ReplacementTargets>();
    size_t editIndex = 0, editPageStart = 0;
    auto editPage = edits.Page(0, (std::min)(size_t(4096), edits.Count()));
    auto edit = [&]() -> const ReplacementEdit* {
        if (editIndex >= edits.Count()) return nullptr;
        if (editIndex >= editPageStart + editPage.size()) { editPageStart = editIndex; editPage = edits.Page(editIndex, 4096); }
        return &editPage[editIndex - editPageStart];
    };
    Sci::Position shift = 0;
    auto map = [&](large::Position position, bool end, bool zero) {
        auto current = edit();
        while (current && current->sourceEnd <= position && (current->sourceStart < position || (!end && !zero))) {
            shift = static_cast<Sci::Position>(current->resultEnd) - static_cast<Sci::Position>(current->sourceEnd);
            ++editIndex; current = edit();
        }
        if (current && current->sourceStart <= position && position <= current->sourceEnd) {
            if (end && position == current->sourceStart && (!zero || current->sourceStart != current->sourceEnd))
                return static_cast<large::Position>(static_cast<Sci::Position>(position) + shift);
            return end ? current->resultEnd : current->resultStart;
        }
        return static_cast<large::Position>(static_cast<Sci::Position>(position) + shift);
    };
    auto plain = result.text.encoding;
    if (plain == Encoding::Utf8Bom) plain = Encoding::Utf8;
    if (plain == Encoding::Utf16LE) plain = Encoding::Utf16LEPlain;
    if (plain == Encoding::Utf16BE) plain = Encoding::Utf16BEPlain;
    const auto pageCode = CodePage(plain);
    const bool fullEditorOnly = pageCode == CP_UTF7 || pageCode == 50220 || pageCode == 50221 || pageCode == 50222;
    large::Position converted = 0, native = 0;
    auto encodedLength = [&](large::Position first, large::Position last) {
        if (plain == Encoding::Utf8 || fullEditorOnly) return last - first;
        large::Position length = 0;
        while (first < last) {
            if (cancel.load()) throw std::runtime_error("Replacement cancelled.");
            auto end = (std::min)(last, first + 64 * 1024);
            while (end < last && (static_cast<unsigned char>(result.text.utf8[end]) & 0xC0) == 0x80) --end;
            length += Encode({result.text.utf8.substr(first, end - first), plain}).size(); first = end;
        }
        return length;
    };
    ReplacementTarget previous{};
    for (size_t first = 0; first < result.hits.archive->Count();) {
        const auto page = result.hits.archive->Page(first, 4096);
        for (const auto& hit : page) {
            const bool zero = hit.start == hit.end;
            const auto start = map(hit.start, false, zero), end = map(hit.end, true, zero);
            native += encodedLength(converted, start); converted = start;
            const auto nativeEnd = previous.utf8Start == start && previous.utf8End == end ? previous.nativeEnd : native + encodedLength(start, end);
            previous = {start, end, native, nativeEnd};
            result.hits.replacementTargets->Append(previous);
        }
        first += page.size();
    }

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

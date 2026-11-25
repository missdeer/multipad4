#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace packages {
struct FileMove {
    std::filesystem::path destination, source, backup;
    bool installed = false, backedUp = false;
};

std::filesystem::path NewDirectory(const std::filesystem::path& parent, const wchar_t* prefix);
bool DirectoryExists(const std::filesystem::path& path);
std::filesystem::path StageZip(const std::wstring& url, const std::wstring& hash,
    const std::filesystem::path& directory, size_t maximumBytes = SIZE_MAX);
void CommitMoves(std::vector<FileMove>& moves, const std::filesystem::path& staging, bool& keepStaging);
}

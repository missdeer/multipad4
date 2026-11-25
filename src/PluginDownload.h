#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

void DownloadPackage(const std::wstring& url, const std::filesystem::path& destination, size_t maximumBytes);
std::string DownloadReleaseMetadata(const std::wstring& url);

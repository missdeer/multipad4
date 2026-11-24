#pragma once
#include <filesystem>
#include <string>

void DownloadPluginPackage(const std::wstring& url, const std::filesystem::path& destination);

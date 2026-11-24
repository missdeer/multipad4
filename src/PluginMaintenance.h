#pragma once
#include <filesystem>
#include <string>
#include <vector>

enum class PluginOperation { Install, Update, Remove, Disable, Enable };

struct PluginPackage {
    std::wstring folder, url, hash;
    bool disabled = false;
};

struct PluginMaintenance {
    PluginOperation operation;
    std::filesystem::path root, directory;
    std::vector<PluginPackage> packages;
};

bool ValidPluginFolder(const std::wstring& name);
std::filesystem::path CreatePluginTaskDirectory();
void ExecutePluginMaintenance(const PluginMaintenance& task);

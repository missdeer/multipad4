#include "../src/PluginMaintenance.h"
#include "../src/FileIO.h"
#include "../3rdparty/nlohmann-json/json.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <windows.h>

namespace {
namespace fs = std::filesystem;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void ExpectFailure(const PluginMaintenance& task, const char* reason) {
    try {
        ExecutePluginMaintenance(task);
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(reason) == std::string_view::npos) throw;
        return;
    }
    throw std::runtime_error("Plugin operation unexpectedly succeeded.");
}

void TestOffline(const fs::path& root) {
    const auto installed = root / L"Fixture" / L"Fixture.dll";
    const auto disabled = root / L"disabled" / L"Fixture" / L"Fixture.dll";
    fs::create_directories(installed.parent_path());
    std::ofstream(installed) << "original";
    const PluginPackage fixture{L"Fixture", {}, {}, false};
    ExecutePluginMaintenance({PluginOperation::Disable, root, {}, {fixture}});
    Require(fs::exists(disabled) && !fs::exists(installed), "Disable failed.");
    ExecutePluginMaintenance({PluginOperation::Enable, root, {}, {{L"Fixture", {}, {}, true}}});
    Require(fs::exists(installed) && !fs::exists(disabled), "Enable failed.");
    ExpectFailure({PluginOperation::Remove, root, {}, {fixture, {L"Missing", {}, {}, false}}}, "Plugin not found");
    ExpectFailure({PluginOperation::Remove, root, {}, {fixture, {L"fIXTURE", {}, {}, false}}}, "duplicate");
    ExpectFailure({PluginOperation::Remove, root, {}, {{L"../Fixture", {}, {}, false}}}, "Invalid or duplicate");
    ExpectFailure({PluginOperation::Remove, root, {}, {{L"AUX.txt", {}, {}, false}}}, "Invalid or duplicate");
    ExpectFailure({PluginOperation::Install, root, {}, {{L"New", L"https://example.invalid/plugin.zip", L"invalid", false}}}, "Invalid package metadata");
    ExpectFailure({PluginOperation::Install, root, {}, {{L"New", L"https://127.0.0.1:1/plugin.zip", std::wstring(64, L'0'), false}}}, "Host 127.0.0.1");
    Require(!fs::exists(root / L"New"), "Failed download changed the plugin directory.");

    const auto locked = root / L"Locked" / L"Locked.dll";
    fs::create_directories(locked.parent_path());
    std::ofstream(locked) << "locked";
    const HANDLE file = CreateFileW(locked.parent_path().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Could not lock fixture directory.");
    try {
        ExpectFailure({PluginOperation::Remove, root, {}, {fixture, {L"Locked", {}, {}, false}}}, "rename");
    } catch (...) {
        CloseHandle(file);
        throw;
    }
    CloseHandle(file);
    Require(fs::exists(installed) && fs::exists(locked), "Batch rollback lost an installed plugin.");
    ExecutePluginMaintenance({PluginOperation::Remove, root, {}, {fixture}});
    Require(!fs::exists(installed), "Remove failed.");
}

void TestOnline(const fs::path& root, const fs::path& catalogPath) {
    std::ifstream input(catalogPath);
    const auto catalog = nlohmann::json::parse(input);
    for (const auto& entry : catalog.at("npp-plugins")) {
        if (entry.at("folder-name") != "NppExport") continue;
        const PluginPackage package{L"NppExport", files::FromUtf8(entry.at("repository").get<std::string>()),
            files::FromUtf8(entry.at("id").get<std::string>()), false};
        ExecutePluginMaintenance({PluginOperation::Install, root, {}, {package}});
        const auto dll = root / L"NppExport" / L"NppExport.dll";
        Require(fs::exists(dll), "Online installation did not install the DLL.");
        const auto userFile = dll.parent_path() / L"user-settings.ini";
        std::ofstream(userFile) << "keep-me";
        auto invalid = package;
        invalid.hash = std::wstring(64, L'0');
        ExpectFailure({PluginOperation::Update, root, {}, {invalid}}, "SHA-256 mismatch");
        Require(fs::exists(dll), "Hash failure damaged the installed plugin.");
        ExecutePluginMaintenance({PluginOperation::Update, root, {}, {package}});
        std::string setting;
        std::ifstream(userFile) >> setting;
        Require(fs::exists(dll) && setting == "keep-me", "Update lost installed plugin data.");
        ExecutePluginMaintenance({PluginOperation::Remove, root, {}, {package}});
        Require(!fs::exists(dll), "Online plugin was not removed.");
        return;
    }
    throw std::runtime_error("NppExport entry not found.");
}
}

int main(int argc, char** argv) {
    try {
        const auto directory = CreatePluginTaskDirectory();
        const auto root = directory / L"\u6d4b\u8bd5" / L"plugins";
        TestOffline(root);
        if (argc == 2) TestOnline(root, argv[1]);
        fs::remove_all(directory);
        std::cout << "Plugin maintenance checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#pragma once
#include "../src/MainFrame.h"
#include "../src/CommandLine.h"
#include "../src/FileDialogs.h"
#include <source_location>
#include <stdexcept>

inline void RunSystemMRUPolicyTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("System MRU policy assertion at line " + std::to_string(where.line()));
    };
    auto directory = std::filesystem::temp_directory_path() /
        (L"MultiPad4-system-mru-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(directory);
    struct RestoreState {
        MainFrame& frame;
        AppSettings settings;
        int useSystemMRU;
        std::filesystem::path directory;
        ~RestoreState() {
            frame.settings = settings;
            frame.runtime.useSystemMRU = useSystemMRU;
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } restore{frame, frame.settings, frame.runtime.useSystemMRU, directory};
    auto load = [&](const std::filesystem::path& path) {
        frame.settings = AppSettings{};
        frame.settings.Initialize(true);
        frame.settings.path = path;
        frame.settings.persistent = true;
        frame.runtime.useSystemMRU = FrameRuntime{}.useSystemMRU;
    };
    // Cover both stored defaults and an absent key. All saves target temporary
    // profiles; policy checks never invoke SHAddToRecentDocs.
    for (int stored : {-1, 0, 1}) {
        for (int overrideValue : {0, 1}) {
            auto path = directory / (std::to_wstring(stored) + L"-" + std::to_wstring(overrideValue) + L".ini");
            load(path);
            require(frame.runtime.useSystemMRU == -1);
            if (stored >= 0) frame.settings.Set(60050, stored);
            require(frame.settings.Save());
            load(path);
            auto options = ParseCommandLine(L"multipad4.exe /sysmru=" + std::to_wstring(overrideValue));
            require(options.useSystemMRU == overrideValue);
            ApplyCommandLine(frame, options);
            require(frame.runtime.useSystemMRU == overrideValue);
            // Save before querying the default: an absent option must remain
            // absent, proving Apply did not insert it into the settings map.
            require(frame.settings.Save());
            wchar_t value[16]{};
            GetPrivateProfileStringW(L"Options", L"Command60050", L"absent", value, _countof(value), path.c_str());
            require(std::wstring(value) == (stored < 0 ? L"absent" : std::to_wstring(stored)));
            require(ShouldUseSystemMRU(frame) == (overrideValue != 0));
            require(frame.settings.Get(60050, 1) == (stored < 0 ? 1 : stored));
            ApplyCommandLine(frame, ParseCommandLine(L"multipad4.exe"));
            require(frame.runtime.useSystemMRU == overrideValue);
            // A fresh runtime plus a fresh settings cache models next startup.
            load(path);
            ApplyCommandLine(frame, ParseCommandLine(L"multipad4.exe"));
            require(frame.runtime.useSystemMRU == -1);
            require(ShouldUseSystemMRU(frame) == (stored != 0));
        }
    }
    frame.settings = AppSettings{};
    frame.settings.Initialize(true);
    for (int overrideValue : {-1, 0, 1}) {
        frame.runtime.useSystemMRU = overrideValue;
        for (int stored : {0, 1}) {
            frame.settings.Set(60050, stored);
            require(!ShouldUseSystemMRU(frame));
        }
    }
}

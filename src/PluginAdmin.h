#pragma once
#include "App.h"
#include <filesystem>
#include "../3rdparty/notepad-plus-plus/PluginsAdmin/pluginsAdminRes.h"
// Notepad++ menuCmdID.h: IDM_SETTING + 15 / +14. Route before plugin IDs.
constexpr UINT IDM_SETTING_PLUGINADM = 48015;
constexpr UINT IDM_SETTING_OPENPLUGINSDIR = 48014;
constexpr UINT IDR_PLUGIN_CATALOG = 5515;
constexpr UINT IDR_PLUGIN_WORKER = 5516;
class MainFrame;
class PluginAdmin {
public:
    struct Entry {
        std::wstring folder, name, version, installed, description, author, homepage, url, hash;
        bool present = false, loaded = false, disabled = false, compatible = true;
    };
    HWND hwnd = nullptr, list = nullptr, tab = nullptr;
    MainFrame* frame = nullptr;
    std::vector<Entry> entries;
    std::vector<size_t> visible;
    std::wstring catalogVersion;
    int page = 0;
    void Show(MainFrame& owner);
    void Close();
    void Load();
    void Populate();
    void Search();
    void Describe();
    void Apply(int command);
    static INT_PTR CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM);
};

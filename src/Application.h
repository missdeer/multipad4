#pragma once
#include <windows.h>
#include <functional>
#include <span>
#include <string>
#include <string_view>
class MainFrame;
struct ApplicationStartup {
    bool isolated=false,pluginsEnabled=true;
    std::wstring onlyPlugin;
    std::span<const std::wstring_view> bundledModules;
    std::function<int(MainFrame&)> run;
    std::function<int(int)> beforeOleShutdown;
};
int RunApplication(HINSTANCE instance,int show,const ApplicationStartup& startup={});

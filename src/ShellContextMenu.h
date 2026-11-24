#pragma once
#include <windows.h>
#include <filesystem>

UINT TrackTabContextMenu(HWND owner, HMENU menu, POINT point, const std::filesystem::path& path);

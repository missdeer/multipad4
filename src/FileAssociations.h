#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

void ShowFileAssociations(HWND owner);
LSTATUS SetFileAssociation(HKEY root, const std::wstring& extension, const std::filesystem::path& executable, bool enable);

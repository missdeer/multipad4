#pragma once
#include "App.h"
#include "FileIO.h"
class MainFrame;
bool SelectFileEncoding(HWND owner, files::Encoding& encoding);
bool RunFileDialog(MainFrame& frame);
bool OpenWithFileDialog(MainFrame& frame);
void ExecuteExternalCommand(MainFrame& frame, std::wstring command);
bool SelectHistoryFile(MainFrame& frame, bool favorites);
bool AddFavoriteFile(MainFrame& frame);
void ManageFavoriteFiles(MainFrame& frame);
bool ShouldUseSystemMRU(const MainFrame& frame);
void RecordOpenedFile(MainFrame& frame, const std::filesystem::path& path);
void ConfigureDefaultEncoding(MainFrame& frame);
void ConfigureDefaultLineEndings(MainFrame& frame);

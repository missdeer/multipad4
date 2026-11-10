#pragma once
#include "FileIO.h"
#include <optional>
#include <vector>
#include <windows.h>
class MainFrame;
struct ParsedCommandLine {
    std::vector<std::filesystem::path> files;
    std::filesystem::path sessionFile;
    std::optional<files::Encoding> sourceEncoding, targetEncoding;
    std::wstring scheme, match, title, appId, positionFlags;
    std::optional<RECT> position;
    long long line = 0, column = 0;
    int eol = -1, reuseWindow = -1, alwaysOnTop = -1, changeNotify = -1, useSystemMRU = -1;
    bool singleFileInstance = false, quietCreate = false, readOnly = false, fromClipboard = false,
        pasteBoard = false, print = false, printDialog = false, elevate = false, startTray = false,
        help = false, matchRegex = false, matchBackslash = false, matchBackwards = false,
        visualColumn = false, byteColumn = false, maximize = false, multipleFiles = false;
    bool jump = false, notepadReplacement = false;
    unsigned tabWidth = 0;
};
ParsedCommandLine ParseCommandLine(const std::wstring& fullCommandLine);
void ApplyCommandLine(MainFrame& frame, const ParsedCommandLine& options);
bool RelaunchElevatedIfRequested(const ParsedCommandLine& options);
std::wstring GetOriginalCommandLineArguments();

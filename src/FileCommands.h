#pragma once
#include "EditorCommands.h"
class MainFrame;
CommandState FileCommandState(MainFrame& frame, UINT id);
bool ExecuteFileCommand(MainFrame& frame, UINT id);
void ApplyFileVariables(EditorDocument& document);
void RestoreFileVariables(EditorDocument& document);
bool LoadLargeFileForFrame(MainFrame& frame, EditorDocument& document, const std::filesystem::path& path, std::optional<files::Encoding> forced = {}, bool encodingTags = true);
files::Text ReadFileForFrame(MainFrame& frame, const std::filesystem::path& path);
files::Text ReadFileForReload(MainFrame& frame, const EditorDocument& document);
bool SelectDocumentEncoding(MainFrame& frame, EditorDocument& document, files::Encoding encoding);
bool PrepareEncodingSave(MainFrame& frame, EditorDocument& document, files::Text& output, bool copy = false);
void InitializeFileDocument(MainFrame& frame, EditorDocument& document);
void PrepareFileSave(MainFrame& frame, EditorDocument& document);
std::filesystem::path SaveDocumentBackup(MainFrame& frame, EditorDocument& document);
enum class FolderOpenRequest { Browse, OpenFolder, ContainingFolder };
struct FolderOpenPlan {
    std::filesystem::path target, folder;
    std::wstring command;
    bool useMatepath = false, warnIfUnavailable = false, pluginAvailable = false, selectTarget = false;
};
FolderOpenPlan BuildFolderOpenPlan(MainFrame& frame, const std::filesystem::path& target, FolderOpenRequest request);
bool OpenFolderRequest(MainFrame& frame, const std::filesystem::path& target, FolderOpenRequest request);

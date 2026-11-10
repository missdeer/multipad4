#pragma once
#include <filesystem>
class MainFrame;
struct EditorDocument;
// Stores document paths and view configuration only; unsaved text is never persisted.
// allDocuments follows the current tab order; false clones only active.
void SaveRelaunchSession(MainFrame& frame, const std::filesystem::path& path, bool allDocuments, EditorDocument* active = nullptr);
// Missing/unreadable files are reported and skipped, never recreated as blank files.
void RestoreRelaunchSession(MainFrame& frame, const std::filesystem::path& path);

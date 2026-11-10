#pragma once
#include "EditorCommands.h"
CommandState AdvancedEditState(const EditorDocument *document, UINT id);
bool ExecuteAdvancedEdit(EditorDocument &document, UINT id);

void LoadEditPreferences(const std::filesystem::path &path);
void SaveEditPreferences(const std::filesystem::path &path);

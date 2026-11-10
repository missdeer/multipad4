#pragma once
#include "EditorCommands.h"
CommandState ExtraEditState(const EditorDocument *document, UINT id);
bool ExecuteExtraEdit(EditorDocument &document, UINT id, const std::function<bool(bool)> &confirmWrap = {});

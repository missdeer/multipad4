#pragma once
#include "EditorDocument.h"
struct CommandState {
    bool supported = false, enabled = false, checked = false;
};
CommandState EditorCommandState(const EditorDocument *document, UINT id);
bool ExecuteEditorCommand(EditorDocument &document, UINT id, const std::function<bool(bool)> &confirmWrap = {});

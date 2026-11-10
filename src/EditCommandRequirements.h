#pragma once
#include "EditorCommands.h"
// Exact upstream menu prerequisites; apply to document command state before dispatch.
CommandState ConstrainUpstreamEditorState(const EditorDocument *document, UINT command, CommandState state);

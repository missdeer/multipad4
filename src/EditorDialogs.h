#pragma once
#include "EditorDocument.h"
// Dialog callbacks receive the target explicitly. No global current editor.
INT_PTR ShowEditorDialog(HWND owner, EditorDocument& document, UINT resource, bool bottomRight = false);
void ShowAboutDialog(HWND owner);

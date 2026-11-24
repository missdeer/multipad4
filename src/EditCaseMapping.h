#pragma once
#include "EditorDocument.h"
#include "../plugins/PluginApi.h"
void ApplyDocumentCaseMapping(EditorDocument &document, UINT command);
void ApplyDocumentCaseMapping(EditorDocument &document, MultiPadSelectionMapper mapper);

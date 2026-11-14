#pragma once
#include "EditorCommands.h"
struct EDITLEXER;
const EDITLEXER* GetDocumentLexer(const EditorDocument& document);
bool SelectDocumentScheme(EditorDocument& document, const std::wstring& name);
bool IsDocumentEscapeOrFormat(EditorDocument&,LRESULT before,int character,int previous,int style,bool punctuation);
bool ApplyDocumentAutoCloseTag(EditorDocument& document,bool closeTags);
void ApplyDocumentAutoPair(EditorDocument& document,int character,int option);
void ApplyDocumentAutoIndent(EditorDocument& document);
void ApplyDocumentWrapSettings(EditorDocument& document);
void SetDefaultTabsAsSpaces(bool spaces);
void DetectDocumentIndentation(EditorDocument& document);
void ConfigureDocumentStyle(EditorDocument& document);
CommandState StyleCommandState(const EditorDocument* document, UINT id);
bool ExecuteStyleCommand(HWND owner, EditorDocument& document, UINT id);
void HandleStyleNotification(EditorDocument& document, const SCNotification& notification);
void ProcessStyleBackground(EditorDocument& document, unsigned budgetMilliseconds = 8);
void ForgetDocumentStyle(EditorDocument& document);
size_t DocumentSchemeCount();
void LoadStylePreferences(const std::filesystem::path& path);
void SaveStylePreferences(const std::filesystem::path& path);

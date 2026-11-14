#pragma once
#include "EditorDocument.h"
bool ShowThemeDialog(HWND owner,EditorDocument& document);
bool ThemeScopeAll();
void ApplyDefaultDocumentTheme(EditorDocument& document);
void ApplyThemeEditorElements(EditorDocument& document);
void LoadThemePreferences(const std::filesystem::path& path);
void SaveThemePreferences(const std::filesystem::path& path);

#pragma once
#include "EditorCommands.h"
class MainFrame;
CommandState FrameCommandState(MainFrame& frame, UINT id);
bool ExecuteFrameCommand(MainFrame& frame, UINT id);
void InitializeFramePreferences(MainFrame& frame);
void ApplyDocumentPreferences(MainFrame& frame, EditorDocument& document);
void RememberDocumentPreferences(MainFrame& frame, EditorDocument& document, UINT command);
void SaveFramePreferences(MainFrame& frame);
void UpdateToolbarImages(MainFrame& frame);
void TickBackgroundWork(MainFrame& frame);
void SnapshotDocument(EditorDocument& document);
void BackupDocuments(MainFrame& frame, bool force = false);
void RemoveDocumentRecovery(MainFrame& frame, EditorDocument& document, bool keepBackup = false);
void SetFrameTransparency(MainFrame& frame, bool active);
void SetTrayVisible(MainFrame& frame, bool visible);

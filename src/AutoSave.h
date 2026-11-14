#pragma once
#include "EditorDocument.h"
class MainFrame;
DWORD AutoSavePeriodMilliseconds(const MainFrame& frame);
std::filesystem::path AutoSaveDirectory(const MainFrame& frame);
bool SaveDocumentRecovery(MainFrame& frame, EditorDocument& document, bool force = false);
void RemoveDocumentRecoveries(MainFrame& frame, EditorDocument& document, bool keepBackup = false);
void QueueDocumentBackups(MainFrame& frame);
void PumpDocumentBackup(MainFrame& frame);
void CancelDocumentBackup(MainFrame& frame, const EditorDocument& document, bool keepBackup = false);

#pragma once
#include "EditorDocument.h"
class AppSettings;
struct PrintPreferences { int header = 1, footer = 0, color = SC_PRINT_COLOURONWHITE, zoom = 100; };
PrintPreferences CurrentPrintPreferences();
UINT_PTR CALLBACK PrintSetupHook(HWND window, UINT message, WPARAM w, LPARAM l);
void ConfigurePrinting(HWND owner, AppSettings* settings = nullptr);
bool PrintDocument(HWND owner, const EditorDocument& document, AppSettings* settings = nullptr, bool defaultPrinter = false);

// Host services required by the notepad4 Scintilla fork (not upstream Scintilla).
#include <windows.h>
#include <cstddef>
HANDLE g_hDefaultHeap = GetProcessHeap();
UINT g_uSystemDPI = USER_DEFAULT_SCREEN_DPI;
WCHAR defaultTextFontName[LF_FACESIZE] = L"Consolas";
unsigned int dwUrlThreshold = 256;

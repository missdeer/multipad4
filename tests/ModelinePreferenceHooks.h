#pragma once
#include <windows.h>
struct EditorDocument;
class MainFrame;
namespace modelinetest {
inline thread_local void (*applyBaseline)(EditorDocument&) = nullptr;
inline thread_local void (*preferencesBaseline)(MainFrame&, EditorDocument&, UINT) = nullptr;
}

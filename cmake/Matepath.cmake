# Keep the imported snapshot unchanged; apply host adaptations to a build copy.
set(MATEPATH_SOURCE_DIR "${PROJECT_SOURCE_DIR}/3rdparty/matepath/src")
set(MATEPATH_COMPAT_DIR "${PROJECT_BINARY_DIR}/matepath")
file(MAKE_DIRECTORY "${MATEPATH_COMPAT_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MATEPATH_SOURCE_DIR}/Dlapi.cpp")
file(READ "${MATEPATH_SOURCE_DIR}/Dlapi.cpp" matepath_api)
string(REPLACE "#include <windows.h>" "#define STRICT_TYPED_ITEMIDS\n#include <windows.h>" matepath_api "${matepath_api}")
string(REPLACE "#include \"Helpers.h\"" "#include \"MatepathCompat.h\"" matepath_api "${matepath_api}")
string(REPLACE "lpdl->worker.workerThread = CreateThread(nullptr, 0, DirList_IconThread, lpdl, 0, nullptr);" "DirList_IconThread(lpdl);" matepath_api "${matepath_api}")
# LVN_DELETEITEM already carries the item data, including during destruction.
string(REPLACE "if (ListView_GetItem(hwnd, &lvi)) {\n\t\t// Free mem\n\t\tLV_ITEMDATA *lplvid = AsPointer<LV_ITEMDATA *>(lvi.lParam);"
 "if (lpnmlv->lParam) {\n\t\t// Free mem\n\t\tLV_ITEMDATA *lplvid = AsPointer<LV_ITEMDATA *>(lpnmlv->lParam);" matepath_api "${matepath_api}")
# Release enumeration PIDLs rejected by a filter or the filesystem check.
string(REPLACE "lvi.iItem++;" "lvi.iItem++;\n\t\t\t\t\t\t\t\tpidlEntry = nullptr;" matepath_api "${matepath_api}")
string(REPLACE "} // IEnumIDList::Next()" "\tCoTaskMemFree(pidlEntry);\n\t\t\t\t\t} // IEnumIDList::Next()" matepath_api "${matepath_api}")
string(REPLACE "IShellIcon *lpshi;" "IShellIcon *lpshi = nullptr;" matepath_api "${matepath_api}")
string(REPLACE "WIN32_FIND_DATA fd;" "WIN32_FIND_DATA fd{};" matepath_api "${matepath_api}")
file(WRITE "${MATEPATH_COMPAT_DIR}/Dlapi.cpp" "${matepath_api}")

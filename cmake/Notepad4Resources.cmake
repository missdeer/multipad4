# Adapt the pinned UI at configure time; preserve the vendored resources.
# Asset filenames and upstream download URLs retain their original identity.
function(multipad4_adapt_resources variable)
  string(REPLACE "Notepad4" "MultiPad4" content "${${variable}}")
  foreach(asset Notepad4.ico Notepad4_min.ico Notepad4.ver)
    string(REPLACE "Notepad4" "MultiPad4" branded_asset "${asset}")
    string(REPLACE "${branded_asset}" "${asset}" content "${content}")
  endforeach()
  string(REGEX REPLACE "IDD_STYLECONFIG DIALOGEX[^\n]*\n([^E\n][^\n]*\n|\n)*END" "" content "${content}")
  string(REPLACE "Customize Schemes" "Language and Style" content "${content}")
  string(REPLACE "自定义语法高亮" "语言与样式" content "${content}")
  string(REPLACE "自訂語法高亮" "語言與樣式" content "${content}")
  set(${variable} "${content}" PARENT_SCOPE)
endfunction()

set(NOTEPAD4_SOURCE_DIR "${PROJECT_SOURCE_DIR}/3rdparty/notepad4/src")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${NOTEPAD4_SOURCE_DIR}/Notepad4.rc")
file(READ "${NOTEPAD4_SOURCE_DIR}/Notepad4.rc" NOTEPAD4_RESOURCES)
string(REGEX REPLACE "IDR_RT_MANIFEST[^\n]*" "" NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
string(REPLACE "#include \"Notepad4.ver\"" "" NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
string(REPLACE "..\\\\res\\\\" "${PROJECT_SOURCE_DIR}/3rdparty/notepad4/res/"
  NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
string(REGEX REPLACE "IDD_(FIND|REPLACE) DIALOGEX[^\n]*\n([^E\n][^\n]*\n|\n)*END" "" NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
multipad4_adapt_resources(NOTEPAD4_RESOURCES)
file(WRITE "${PROJECT_BINARY_DIR}/Notepad4Resources.rc"
  "${NOTEPAD4_RESOURCES}\nLANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US\n#include \"${PROJECT_BINARY_DIR}/FileIconResources.rcinc\"\n")

# The same pinned revision supplies all language resources; build resource-only
# translation units into the executable so dialogs and menus follow UI language.
set(NOTEPAD4_LOCALE_RESOURCES "")
foreach(locale de fr it ja ko pl pt-BR ru sl zh-Hans zh-Hant)
  set(input "${PROJECT_SOURCE_DIR}/3rdparty/notepad4/locale/${locale}/Notepad4.rc")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
  file(READ "${input}" content)
  string(REPLACE "../../src/" "${NOTEPAD4_SOURCE_DIR}/" content "${content}")
  string(REGEX REPLACE "IDR_RT_MANIFEST[^\n]*" "" content "${content}")
  string(REPLACE "#include \"${NOTEPAD4_SOURCE_DIR}/Notepad4.ver\"" "" content "${content}")
  string(REPLACE "..\\\\..\\\\res\\\\" "${PROJECT_SOURCE_DIR}/3rdparty/notepad4/res/" content "${content}")
  string(REGEX REPLACE "IDD_(FIND|REPLACE) DIALOGEX[^\n]*\n([^E\n][^\n]*\n|\n)*END" "" content "${content}")
  set(output "${PROJECT_BINARY_DIR}/Notepad4Resources-${locale}.rc")
  if(MINGW OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # MS rc accepts the missing comma in the pinned Japanese menu items;
    # GNU windres and llvm-rc require the documented MENUITEM separator.
    string(REGEX REPLACE "(MENUITEM [^\n]*\")[ \t]+((IDM_|CMD_)[A-Z0-9_]+)" "\\1, \\2" content "${content}")
  endif()
  multipad4_adapt_resources(content)
  file(WRITE "${output}" "${content}")
  list(APPEND NOTEPAD4_LOCALE_RESOURCES "${output}")
endforeach()

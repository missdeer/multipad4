# Adapt the pinned UI at configure time; preserve the vendored resources.
include("${PROJECT_SOURCE_DIR}/plugins/WebTools/Resources.cmake")
include("${PROJECT_SOURCE_DIR}/plugins/TextTransliteration/Resources.cmake")
include("${PROJECT_SOURCE_DIR}/plugins/SelectionActions/Resources.cmake")
set(multipad4_icon "${PROJECT_SOURCE_DIR}/res/MultiPad4.ico")
function(multipad4_adapt_resources variable locale)
  string(REPLACE "Notepad4" "MultiPad4" content "${${variable}}")
  string(REGEX REPLACE "\"[^\"\n]*/MultiPad4(_min)?\\.ico\"" "\"${multipad4_icon}\"" content "${content}")
  string(REGEX REPLACE "IDD_STYLECONFIG DIALOGEX[^\n]*\n([^E\n][^\n]*\n|\n)*END" "" content "${content}")
  string(REPLACE "Customize Schemes" "Language and Style" content "${content}")
  string(REPLACE "自定义语法高亮" "语言与样式" content "${content}")
  string(REPLACE "自訂語法高亮" "語言與樣式" content "${content}")
  string(REPLACE "MENUITEM \"&Tab Settings..." "MENUITEM \"&Tabs and Indentation..." content "${content}")
  string(REPLACE "CAPTION \"Tab Settings\"" "CAPTION \"Tabs and Indentation\"" content "${content}")

  # Base64 registers its localized menu through the plugin host.
  string(REGEX REPLACE "\n\t\tPOPUP [^\n]*\n\t\tBEGIN\n([^\n]*IDM_EDIT_BASE64_[^\n]*\n)+\t\tEND" "" content "${content}")
  if(content MATCHES "MENUITEM[^\n]*IDM_EDIT_BASE64_")
    message(FATAL_ERROR "Cannot remove the old Base64 menu from the ${locale} resources")
  endif()

  string(REGEX MATCH "\nLANGUAGE [^\n]+" language "${content}")
  if(NOT language)
    message(FATAL_ERROR "Cannot locate the LANGUAGE declaration for ${locale}")
  endif()
  multipad4_extract_web_tools(content "${locale}" "${language}")
  multipad4_extract_selection_actions(content "${locale}" "${language}")
  multipad4_extract_text_transliteration(content "${locale}" "${language}")
  set(WEB_TOOLS_MENU_STRINGS "${WEB_TOOLS_MENU_STRINGS}" PARENT_SCOPE)
  set(SELECTION_ACTIONS_MENU_STRINGS "${SELECTION_ACTIONS_MENU_STRINGS}" PARENT_SCOPE)
  set(TEXT_TRANSLITERATION_MENU_STRINGS "${TEXT_TRANSLITERATION_MENU_STRINGS}" PARENT_SCOPE)

  # Insert the MDI menu before Help in the localized main menu.
  set(window_menu_file "${PROJECT_SOURCE_DIR}/res/locale/${locale}/WindowMenu.rcinc")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${window_menu_file}")
  file(READ "${window_menu_file}" window_menu)
  string(REGEX MATCH "\n\tPOPUP [^\n]*\n\tBEGIN\n\t\tMENUITEM [^\n]*IDM_HELP_PROJECT_HOME" help_menu "${content}")
  if(NOT help_menu)
    message(FATAL_ERROR "Cannot locate the Help menu in the ${locale} resources")
  endif()
  string(REPLACE "${help_menu}" "\n${window_menu}${help_menu}" content "${content}")
  string(REGEX REPLACE "#include \"[^\"\n]*resource\\.h\""
    "#include \"${PROJECT_SOURCE_DIR}/src/resource.h\"" content "${content}")
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
multipad4_adapt_resources(NOTEPAD4_RESOURCES en-US)
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
  multipad4_adapt_resources(content "${locale}")
  file(WRITE "${output}" "${content}")
  list(APPEND NOTEPAD4_LOCALE_RESOURCES "${output}")
endforeach()
set_property(SOURCE "${PROJECT_BINARY_DIR}/Notepad4Resources.rc" ${NOTEPAD4_LOCALE_RESOURCES}
  APPEND PROPERTY OBJECT_DEPENDS "${multipad4_icon}")

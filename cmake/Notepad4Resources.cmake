# Adapt the pinned UI at configure time; preserve the vendored resources.
# Asset filenames and upstream download URLs retain their original identity.
set(WEB_TOOLS_MENU_STRINGS "#include <windows.h>\n#include \"resource.h\"\n#pragma code_page(65001)\n")
function(multipad4_adapt_resources variable locale)
  string(REPLACE "Notepad4" "MultiPad4" content "${${variable}}")
  foreach(asset Notepad4.ico Notepad4_min.ico Notepad4.ver)
    string(REPLACE "Notepad4" "MultiPad4" branded_asset "${asset}")
    string(REPLACE "${branded_asset}" "${asset}" content "${content}")
  endforeach()
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

  # Reuse the pinned translations in the Web Tools DLL and remove the host menu and shortcuts.
  string(REGEX MATCH "\n\t\tPOPUP \"([^\n]*)\"\n\t\tBEGIN\n\t\t\tMENUITEM [^\n]*IDM_EDIT_CODE_COMPRESS[^\n]*\n(\t\t\tMENUITEM[^\n]*\n)+\t\tEND" web_menu "${content}")
  set(web_title "${CMAKE_MATCH_1}")
  string(REGEX MATCH "\nLANGUAGE [^\n]+" web_language "${content}")
  if(NOT web_menu OR NOT web_language)
    message(FATAL_ERROR "Cannot locate the Web Tools resources for ${locale}")
  endif()
  string(APPEND WEB_TOOLS_MENU_STRINGS "${web_language}\nSTRINGTABLE\nBEGIN\n    1 \"${web_title}\"\n")
  foreach(command IDM_EDIT_CODE_COMPRESS IDM_EDIT_CODE_PRETTY CMD_EVALUATE_JS_EXPR
      IDM_EDIT_XHTML_ESCAPE_CHAR IDM_EDIT_XHTML_UNESCAPE_CHAR
      IDM_EDIT_URLENCODE IDM_EDIT_URLCOMPONENTENCODE IDM_EDIT_URLDECODE)
    string(REGEX MATCH "MENUITEM \"([^\n]*)\",?[ \t]*${command}[\r\n]" item "${web_menu}")
    if(NOT item)
      message(FATAL_ERROR "Cannot locate the Web Tools command ${command} for ${locale}")
    endif()
    string(APPEND WEB_TOOLS_MENU_STRINGS "    ${command} \"${CMAKE_MATCH_1}\"\n")
    string(REGEX REPLACE "\n[ \t]+\"[^\n]+\",[ \t]+${command},[^\n]*" "" content "${content}")
  endforeach()
  string(APPEND WEB_TOOLS_MENU_STRINGS "END\n")
  set(WEB_TOOLS_MENU_STRINGS "${WEB_TOOLS_MENU_STRINGS}" PARENT_SCOPE)
  string(REPLACE "${web_menu}" "" content "${content}")

  # Insert the MDI menu before Help in the localized main menu WTL loads.
  set(window_menu_file "${PROJECT_SOURCE_DIR}/res/locale/${locale}/WindowMenu.rcinc")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${window_menu_file}")
  file(READ "${window_menu_file}" window_menu)
  string(REGEX MATCH "\n\tPOPUP [^\n]*\n\tBEGIN\n\t\tMENUITEM [^\n]*IDM_HELP_PROJECT_HOME" help_menu "${content}")
  if(NOT help_menu)
    message(FATAL_ERROR "Cannot locate the Help menu in the ${locale} resources")
  endif()
  string(REPLACE "${help_menu}" "\n${window_menu}${help_menu}" content "${content}")
  string(REGEX REPLACE "#include \"[^\"\n]*resource\\.h\""
    "#include <atlres.h>\n#include \"${PROJECT_SOURCE_DIR}/src/resource.h\"" content "${content}")
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

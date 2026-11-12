# Compile the pinned UI unchanged except for build-relative asset paths and the
# SDI executable's manifest/version (the MDI executable supplies its own identity).
set(NOTEPAD4_SOURCE_DIR "${PROJECT_SOURCE_DIR}/3rdparty/notepad4/src")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${NOTEPAD4_SOURCE_DIR}/Notepad4.rc")
file(READ "${NOTEPAD4_SOURCE_DIR}/Notepad4.rc" NOTEPAD4_RESOURCES)
string(REGEX REPLACE "IDR_RT_MANIFEST[^\n]*" "" NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
string(REPLACE "#include \"Notepad4.ver\"" "" NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
string(REPLACE "..\\\\res\\\\" "${PROJECT_SOURCE_DIR}/3rdparty/notepad4/res/"
  NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
string(REGEX REPLACE "IDD_(FIND|REPLACE) DIALOGEX[^\n]*\n([^E\n][^\n]*\n|\n)*END" "" NOTEPAD4_RESOURCES "${NOTEPAD4_RESOURCES}")
file(WRITE "${PROJECT_BINARY_DIR}/Notepad4Resources.rc" "${NOTEPAD4_RESOURCES}")

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
  file(WRITE "${output}" "${content}")
  list(APPEND NOTEPAD4_LOCALE_RESOURCES "${output}")
endforeach()

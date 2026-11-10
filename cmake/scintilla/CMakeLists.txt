# The fork includes its lexer catalogue in ScintillaBase; compile every lexer.
set(SCI_ROOT "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla")
file(GLOB SCI_SOURCES CONFIGURE_DEPENDS
  "${SCI_ROOT}/src/*.cxx" "${SCI_ROOT}/win32/*.cxx"
  "${SCI_ROOT}/lexlib/*.cxx" "${SCI_ROOT}/lexers/*.cxx")
add_library(notepad4_scintilla STATIC ${SCI_SOURCES})
target_include_directories(notepad4_scintilla PUBLIC "${SCI_ROOT}/include" PRIVATE "${SCI_ROOT}/src" "${SCI_ROOT}/lexlib")
target_compile_definitions(notepad4_scintilla PRIVATE BOOST_REGEX_STANDALONE _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
target_compile_options(notepad4_scintilla PRIVATE /W3 /EHsc)
target_link_libraries(notepad4_scintilla PUBLIC imm32 ole32 oleaut32 gdi32 user32 shlwapi)

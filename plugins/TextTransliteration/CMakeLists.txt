file(CONFIGURE OUTPUT MenuStrings.rc CONTENT "${TEXT_TRANSLITERATION_MENU_STRINGS}" @ONLY)
add_library(TextTransliteration SHARED Plugin.cpp TextTransliterationVersion.rc "${CMAKE_CURRENT_BINARY_DIR}/MenuStrings.rc")
target_include_directories(TextTransliteration PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad-plus-plus-plugin-api"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla/include")
target_compile_options(TextTransliteration PRIVATE ${MULTIPAD4_WARNINGS})
set_target_properties(TextTransliteration PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/TextTransliteration/$<0:>")
configure_file("${PROJECT_SOURCE_DIR}/3rdparty/notepad4/License.txt" Notepad4.License.txt COPYONLY)
add_dependencies(multipad4 TextTransliteration)
if(BUILD_TESTING)
  target_compile_definitions(multipad4-tests PRIVATE MULTIPAD4_TEST_TEXT_TRANSLITERATION)
endif()
install(TARGETS TextTransliteration RUNTIME DESTINATION plugins/TextTransliteration)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Notepad4.License.txt" DESTINATION plugins/TextTransliteration)

file(CONFIGURE OUTPUT MenuStrings.rc CONTENT "${WEB_TOOLS_MENU_STRINGS}" @ONLY)
add_library(WebTools SHARED Plugin.cpp EditCodeFormat.cpp ../EditExpression.cpp
  WebToolsVersion.rc "${CMAKE_CURRENT_BINARY_DIR}/MenuStrings.rc")
target_include_directories(WebTools PRIVATE
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad-plus-plus-plugin-api"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla/include")
target_compile_options(WebTools PRIVATE ${MULTIPAD4_WARNINGS})
target_link_libraries(WebTools PRIVATE multipad_files ole32 oleaut32 uuid)
set_target_properties(WebTools PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/WebTools/$<0:>")
configure_file("${PROJECT_SOURCE_DIR}/3rdparty/notepad4/License.txt" Notepad4.License.txt COPYONLY)
add_dependencies(multipad4 WebTools)
if(BUILD_TESTING)
  target_compile_definitions(multipad4-tests PRIVATE MULTIPAD4_TEST_WEB_TOOLS)
endif()
install(TARGETS WebTools RUNTIME DESTINATION plugins/WebTools)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Notepad4.License.txt" DESTINATION plugins/WebTools)

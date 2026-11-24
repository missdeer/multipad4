file(CONFIGURE OUTPUT MenuStrings.rc CONTENT "${SELECTION_ACTIONS_MENU_STRINGS}" @ONLY)
add_library(SelectionActions SHARED Plugin.cpp ../EditExpression.cpp SelectionActionsVersion.rc
  "${CMAKE_CURRENT_BINARY_DIR}/MenuStrings.rc")
target_include_directories(SelectionActions PRIVATE
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad-plus-plus-plugin-api"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla/include")
if(MINGW)
  target_include_directories(SelectionActions PRIVATE "${OPENATL_COMPAT_DIR}")
endif()
target_compile_options(SelectionActions PRIVATE ${MULTIPAD4_WARNINGS})
target_link_libraries(SelectionActions PRIVATE multipad_files shell32 ole32 oleaut32 uuid)
set_target_properties(SelectionActions PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/SelectionActions/$<0:>")
configure_file("${PROJECT_SOURCE_DIR}/3rdparty/notepad4/License.txt" Notepad4.License.txt COPYONLY)
add_dependencies(multipad4 SelectionActions)
if(BUILD_TESTING)
  target_compile_definitions(multipad4 PRIVATE MULTIPAD4_TEST_SELECTION_ACTIONS)
endif()
install(TARGETS SelectionActions RUNTIME DESTINATION plugins/SelectionActions)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Notepad4.License.txt" DESTINATION plugins/SelectionActions)

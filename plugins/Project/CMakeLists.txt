add_library(Project SHARED Plugin.cpp ProjectLibrary.cpp Project.rc)
target_include_directories(Project PRIVATE
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad-plus-plus-plugin-api"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla/include")
target_compile_options(Project PRIVATE ${MULTIPAD4_WARNINGS})
target_link_libraries(Project PRIVATE multipad_files ole32 shell32 uuid)
set_target_properties(Project PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/Project/$<0:>")
add_dependencies(multipad4 Project)
if(BUILD_TESTING)
  target_compile_definitions(multipad4-tests PRIVATE MULTIPAD4_TEST_PROJECT)
endif()
install(TARGETS Project RUNTIME DESTINATION plugins/Project)

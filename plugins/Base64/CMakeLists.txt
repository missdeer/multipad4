add_library(Base64 SHARED Plugin.cpp Base64Version.rc MenuStrings.rc)
target_include_directories(Base64 PRIVATE
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad-plus-plus-plugin-api"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla/include")
target_compile_options(Base64 PRIVATE ${MULTIPAD4_WARNINGS})
target_link_libraries(Base64 PRIVATE multipad_files)
set_target_properties(Base64 PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/Base64/$<0:>")
configure_file("${PROJECT_SOURCE_DIR}/3rdparty/notepad4/License.txt" Notepad4.License.txt COPYONLY)
add_dependencies(multipad4 Base64)
if(BUILD_TESTING)
  target_compile_definitions(multipad4-tests PRIVATE MULTIPAD4_TEST_BASE64)
endif()
install(TARGETS Base64 RUNTIME DESTINATION plugins/Base64)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Notepad4.License.txt" DESTINATION plugins/Base64)

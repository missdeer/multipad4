# Plugin releases are versioned independently of the host application.
set(OPEN_DOCUMENTS_VERSION "0.1.0.0")
string(REPLACE "." "," OPEN_DOCUMENTS_VERSION_COMMAS "${OPEN_DOCUMENTS_VERSION}")
configure_file(OpenDocumentsVersion.rc.in "${CMAKE_CURRENT_BINARY_DIR}/OpenDocumentsVersion.rc" @ONLY)
add_library(OpenDocuments SHARED Plugin.cpp DocumentsPane.cpp MenuStrings.rc ../../src/DockLayout.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/OpenDocumentsVersion.rc"
)
target_include_directories(OpenDocuments PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"

  "${PROJECT_SOURCE_DIR}/3rdparty/notepad-plus-plus-plugin-api"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla/include")
target_compile_options(OpenDocuments PRIVATE ${MULTIPAD4_WARNINGS})
if(NOT MSVC)
  target_compile_options(OpenDocuments PRIVATE -Wno-unknown-pragmas)
endif()
target_link_libraries(OpenDocuments PRIVATE comctl32 shlwapi shell32 ole32 oleaut32 uuid uxtheme gdi32)
set_target_properties(OpenDocuments PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/OpenDocuments")
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${configuration}" configuration_upper)
  set_target_properties(OpenDocuments PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${PROJECT_BINARY_DIR}/plugins/OpenDocuments")
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/THIRD_PARTY.md"
  "${PROJECT_BINARY_DIR}/plugins/OpenDocuments/THIRD_PARTY.md" COPYONLY)
add_dependencies(multipad4 OpenDocuments)
if(BUILD_TESTING)
  target_compile_definitions(multipad4 PRIVATE MULTIPAD4_TEST_OPEN_DOCUMENTS)
endif()

include("${PROJECT_SOURCE_DIR}/cmake/Matepath.cmake")
# Plugin releases are versioned independently of the host application.
set(MATEPATH_VERSION "0.1.0.0")
string(REPLACE "." "," MATEPATH_VERSION_COMMAS "${MATEPATH_VERSION}")
configure_file(MatepathVersion.rc.in "${CMAKE_CURRENT_BINARY_DIR}/MatepathVersion.rc" @ONLY)
add_library(Matepath SHARED Plugin.cpp MatepathPane.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/MatepathVersion.rc"
  "${MATEPATH_COMPAT_DIR}/Dlapi.cpp" "${MATEPATH_SOURCE_DIR}/DropSource.cpp")
target_include_directories(Matepath PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${MATEPATH_SOURCE_DIR}" "${MULTIPAD4_WTL_INCLUDE_DIR}"
  "${FRAMEWORK_COMPAT_DIR}/DockingFramework" "${FRAMEWORK_COMPAT_DIR}/TabbingFramework"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad-plus-plus-plugin-api"
  "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla/include")
target_compile_definitions(Matepath PRIVATE _WTL_NO_CSTRING _ATL_NO_AUTOMATIC_NAMESPACE _HAS_AUTO_PTR_ETC=1)
target_compile_options(Matepath PRIVATE ${MULTIPAD4_WARNINGS})
if(MSVC)
  target_compile_options(Matepath PRIVATE /permissive /Zc:twoPhase- /wd4996)
else()
  target_include_directories(Matepath BEFORE PRIVATE "${OPENATL_COMPAT_DIR}" "${PROJECT_SOURCE_DIR}/cmake/openatl")
  target_compile_options(Matepath PRIVATE -Wno-unknown-pragmas)
endif()
target_link_libraries(Matepath PRIVATE comctl32 shlwapi shell32 ole32 oleaut32 uuid uxtheme gdi32)
set_target_properties(Matepath PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/Matepath")
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${configuration}" configuration_upper)
  set_target_properties(Matepath PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${PROJECT_BINARY_DIR}/plugins/Matepath")
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/THIRD_PARTY.md"
  "${PROJECT_BINARY_DIR}/plugins/Matepath/THIRD_PARTY.md" COPYONLY)
configure_file("${PROJECT_SOURCE_DIR}/3rdparty/matepath/doc/License.txt"
  "${PROJECT_BINARY_DIR}/plugins/Matepath/Matepath.License.txt" COPYONLY)
add_dependencies(multipad4 Matepath)

if(BUILD_TESTING)
  target_compile_definitions(multipad4 PRIVATE MULTIPAD4_TEST_MATEPATH)
endif()

# The fork includes its lexer catalogue in ScintillaBase; compile every lexer.
set(SCI_ROOT "${PROJECT_SOURCE_DIR}/3rdparty/notepad4-scintilla/scintilla")
file(GLOB SCI_SOURCES CONFIGURE_DEPENDS
  "${SCI_ROOT}/src/*.cxx" "${SCI_ROOT}/win32/*.cxx"
  "${SCI_ROOT}/lexlib/*.cxx" "${SCI_ROOT}/lexers/*.cxx")
include("${PROJECT_SOURCE_DIR}/cmake/ScintillaInstrumentation.cmake")
add_library(notepad4_scintilla STATIC ${SCI_SOURCES})
target_include_directories(notepad4_scintilla BEFORE PRIVATE "${SCI_INSTRUMENTED_DIR}" "${SCI_LEXLIB_DIR}" "${PROJECT_SOURCE_DIR}/src")
option(MULTIPAD4_LEXER_METRICS "Trace lexer character reads for benchmarks" OFF)
if(DEFINED ENV{MULTIPAD4_LEXER_METRICS})
  set(MULTIPAD4_LEXER_METRICS "$ENV{MULTIPAD4_LEXER_METRICS}" CACHE BOOL "Trace lexer character reads" FORCE)
endif()
if(MULTIPAD4_LEXER_METRICS)
  target_compile_definitions(notepad4_scintilla PUBLIC MULTIPAD4_LEXER_METRICS)
endif()
target_include_directories(notepad4_scintilla PUBLIC "${SCI_ROOT}/include" PRIVATE "${SCI_ROOT}/src" "${SCI_ROOT}/lexlib")

if(MINGW)
  # MinGW's _bittest intrinsics dereference long*, which cannot alias uint32_t*
  # under GCC optimization. Use the upstream type-safe bit operations instead.
  set(SCI_COMPAT_INCLUDE_DIR "${PROJECT_BINARY_DIR}/framework-compat/Scintilla")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SCI_ROOT}/include/VectorISA.h")
  file(READ "${SCI_ROOT}/include/VectorISA.h" vector_isa)
  string(REPLACE "#if NP2_TARGET_ARM\ninline bool bittest" "#if NP2_TARGET_ARM || defined(__GNUC__)\ninline bool bittest" vector_isa "${vector_isa}")
  file(CONFIGURE OUTPUT "${SCI_COMPAT_INCLUDE_DIR}/VectorISA.h" CONTENT "${vector_isa}" @ONLY)
  target_include_directories(notepad4_scintilla BEFORE PUBLIC "${SCI_COMPAT_INCLUDE_DIR}")
endif()
target_compile_definitions(notepad4_scintilla PRIVATE BOOST_REGEX_STANDALONE _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
target_compile_options(notepad4_scintilla PRIVATE ${MULTIPAD4_VENDOR_WARNINGS})
target_link_libraries(notepad4_scintilla PUBLIC imm32 ole32 oleaut32 gdi32 user32 shlwapi)

if(MSVC)
  if(CMAKE_RC_COMPILER MATCHES "llvm-rc")
    # Set UTF-8 explicitly for llvm-rc. CMake's argument filter recognizes
    # uppercase /C and keeps this resource-only option out of preprocessing.
    string(APPEND CMAKE_RC_FLAGS " /C65001")
  endif()
  set(MULTIPAD4_WARNINGS /W4 /EHsc)
  set(MULTIPAD4_VENDOR_WARNINGS /W3 /EHsc)
else()
  add_compile_options("$<$<COMPILE_LANGUAGE:RC>:--use-temp-file>")
  string(APPEND CMAKE_RC_FLAGS " --codepage=65001")
  set(MULTIPAD4_WARNINGS -Wall -Wextra -Wno-unused-parameter)
  set(MULTIPAD4_VENDOR_WARNINGS -Wno-unknown-pragmas)
endif()

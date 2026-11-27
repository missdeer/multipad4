# Test variants retain the product's compiler settings without adding hooks to it.
foreach(product IN ITEMS multipad_files multipad_plugin_maintenance multipad_app)
  get_target_property(sources ${product} SOURCES)
  add_library(${product}_tests STATIC ${sources})
  foreach(property IN ITEMS INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES
      COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS COMPILE_DEFINITIONS
      INTERFACE_COMPILE_DEFINITIONS PRECOMPILE_HEADERS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(value ${product} ${property})
    if(NOT value STREQUAL "value-NOTFOUND")
      string(REPLACE "notepad4_scintilla" "notepad4_scintilla_tests" value "${value}")
      string(REPLACE "multipad_files" "multipad_files_tests" value "${value}")
      string(REPLACE "multipad_plugin_maintenance" "multipad_plugin_maintenance_tests" value "${value}")
      set_property(TARGET ${product}_tests PROPERTY ${property} "${value}")
    endif()
  endforeach()
endforeach()

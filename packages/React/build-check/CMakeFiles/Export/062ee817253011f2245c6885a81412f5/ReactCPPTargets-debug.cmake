#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "React::ReactCPP" for configuration "Debug"
set_property(TARGET React::ReactCPP APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(React::ReactCPP PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libReactCPP.a"
  )

list(APPEND _cmake_import_check_targets React::ReactCPP )
list(APPEND _cmake_import_check_files_for_React::ReactCPP "${_IMPORT_PREFIX}/lib/libReactCPP.a" )

# Import target "React::jsi" for configuration "Debug"
set_property(TARGET React::jsi APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(React::jsi PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libjsi.a"
  )

list(APPEND _cmake_import_check_targets React::jsi )
list(APPEND _cmake_import_check_files_for_React::jsi "${_IMPORT_PREFIX}/lib/libjsi.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

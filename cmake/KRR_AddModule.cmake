include(KRR_Message)

function(krr_add_module project_name module_name)
  # ----------------------------------------------------------
  # Retrieve arguments
  # ----------------------------------------------------------
  set(options "")
  set(one_value_args "")
  set(multi_value_args
    HEADERS
    SOURCES
    HARD_DEPENDENCIES
    CMAKE_SUBDIRS
  )

  cmake_parse_arguments(MODULE "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  krr_message(INFO "Adding module ${project_name}::${module_name}")

  set(HEADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/include")
  set(SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")

  list(TRANSFORM MODULE_HEADERS PREPEND ${HEADER_ROOT}/ OUTPUT_VARIABLE headers)
  list(TRANSFORM MODULE_SOURCES PREPEND ${SOURCE_ROOT}/ OUTPUT_VARIABLE sources)

  # ----------------------------------------------------------
  # Setup module categories
  # ----------------------------------------------------------
  if(NOT MODULE_SOURCES)
    set(module_library_type INTERFACE)
    set(module_link_type INTERFACE)
  else()
    set(module_library_type STATIC)
    set(module_link_type PUBLIC)
  endif()

  krr_message(INFO "    module library type: ${module_library_type}")
  krr_message(INFO "    module link type: ${module_link_type}")

  # ----------------------------------------------------------
  # Do add module
  # ----------------------------------------------------------
  string(TOLOWER ${project_name} project_name_lower)
  string(TOLOWER ${module_name} module_name_lower)
  set(module_base_name ${project_name_lower}_${module_name_lower})
  add_library(${module_base_name}
      ${module_library_type} # STATIC, SHARED or INTERFACE
      ${headers}
      ${sources}
  )
  add_library(${project_name}::${module_name} ALIAS ${module_base_name})

  # ----------------------------------------------------------
  # Do setup dependencies
  # ----------------------------------------------------------
  target_include_directories(${module_base_name}
    ${module_link_type}
      $<BUILD_INTERFACE:${HEADER_ROOT}>
  )
  target_link_libraries(${module_base_name}
    ${module_link_type}
      ${MODULE_HARD_DEPENDENCIES}
  )

  # ----------------------------------------------------------
  # Include subdirectories
  # ----------------------------------------------------------
  foreach(subdir ${MODULE_CMAKE_SUBDIRS})
    add_subdirectory(${subdir})
  endforeach()
endfunction()
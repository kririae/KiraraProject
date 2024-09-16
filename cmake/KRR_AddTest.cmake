include(KRR_Message)

function(krr_add_test project_name module_name test_name)
  # ----------------------------------------------------------
  # Retrieve arguments
  # ----------------------------------------------------------
  set(options INDEPENDENT)
  set(one_value_args "")
  set(multi_value_args
    SOURCES
    HARD_DEPENDENCIES
  )

  cmake_parse_arguments(TEST "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  krr_message(INFO "Adding test ${BoldGreen}${project_name}::${module_name}::${test_name}${ColorReset}")

  # ----------------------------------------------------------
  # Setup test sources
  # ----------------------------------------------------------
  set(test_base_name ${project_name}.${module_name}.${test_name})
  add_executable(${test_base_name} ${TEST_SOURCES})
  target_link_libraries(${test_base_name}
    PRIVATE
      gtest
      gmock
      kira::GTestMain
      ${TEST_HARD_DEPENDENCIES}
  )

  if(NOT TEST_INDEPENDENT)
    add_test(NAME ${test_base_name} COMMAND ${test_base_name})
  endif()

  set_target_properties(${test_base_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endfunction()

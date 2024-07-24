# see https://github.com/STEllAR-GROUP/hpx/blob/master/cmake/HPX_Message.cmake
function(_to_string var)
  set(_var "")

  foreach(_arg ${ARGN})
    string(REPLACE "\\" "/" _arg ${_arg})

    if("${_var}" STREQUAL "")
      set(_var "${_arg}")
    else()
      set(_var "${_var} ${_arg}")
    endif()
  endforeach()

  set(${var}
      ${_var}
      PARENT_SCOPE
  )
endfunction()

function(krr_info)
  set(msg)
  _to_string(msg ${ARGN})
  message(STATUS "KiraraProject: ${msg}")
  unset(args)
endfunction()

function(krr_debug)
  set(msg)
  _to_string(msg ${ARGN})
  message(STATUS "KiraraProject: DEBUG: ${msg}")
endfunction()

function(krr_warn)
  set(msg)
  _to_string(msg ${ARGN})
  message(STATUS "KiraraProject: WARNING: ${msg}")
endfunction()

function(krr_error)
  set(msg)
  _to_string(msg ${ARGN})
  message(FATAL_ERROR "KiraraProject: ERROR: ${msg}")
endfunction()

function(krr_message level)
  if("${level}" MATCHES "ERROR|error|Error")
    krr_error(${ARGN})
  elseif("${level}" MATCHES "WARN|warn|Warn")
    krr_warn(${ARGN})
  elseif("${level}" MATCHES "DEBUG|debug|Debug")
    krr_debug(${ARGN})
  elseif("${level}" MATCHES "INFO|info|Info")
    krr_info(${ARGN})
  else()
    krr_error("message"
      "\"${level}\" is not an KRR configuration logging level."
    )
  endif()
endfunction()

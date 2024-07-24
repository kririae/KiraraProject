set(_kira_modules
  Core
  Geometry
)

set(CMAKE_SHARED_LIBRARY_PREFIX_CXX "")
set(CMAKE_STATIC_LIBRARY_PREFIX_CXX "")

foreach(module ${_kira_modules})
  add_subdirectory(${module})
endforeach()

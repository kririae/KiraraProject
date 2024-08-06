include(CMakePackageConfigHelpers)
project(kira LANGUAGES CXX)

set(_kira_modules
  Core
  GTestMain
  Geometry
  Vecteur
)

set(CMAKE_SHARED_LIBRARY_PREFIX_CXX "")
set(CMAKE_STATIC_LIBRARY_PREFIX_CXX "")

foreach(module ${_kira_modules})
  add_subdirectory(${module})
endforeach()

# ----------------------------------------------------------
# Add install rules
# ----------------------------------------------------------
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE
  $<INSTALL_INTERFACE:${INSTALL_INCLUDEDIR}>
)

install(TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}Targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ${PROJECT_NAME}Targets
  FILE ${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

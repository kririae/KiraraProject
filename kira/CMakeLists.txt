include(CMakePackageConfigHelpers)
project(kira LANGUAGES C CXX)

set(_kira_modules
  Core
  GTestMain
  Vecteur
)

foreach(module ${_kira_modules})
  add_subdirectory(${module})
endforeach()

# ----------------------------------------------------------
# Add install rules
# ----------------------------------------------------------
add_library(kira INTERFACE)
add_library(kira::kira ALIAS kira)

target_include_directories(kira INTERFACE
  $<INSTALL_INTERFACE:${INSTALL_INCLUDEDIR}>)

foreach(module ${_kira_modules})
  target_link_libraries(kira INTERFACE kira::${module})
endforeach()

install(TARGETS kira
  EXPORT kiraTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT kiraTargets
  FILE kiraTargets.cmake
  NAMESPACE kira::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kira)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
  VERSION "${KIRA_VERSION_MAJOR}.${KIRA_VERSION_MINOR}.${KIRA_VERSION_PATCH}"
  COMPATIBILITY SameMajorVersion)

configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kira)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kira)

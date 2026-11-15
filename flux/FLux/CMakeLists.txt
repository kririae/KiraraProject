include(GNUInstallDirs)

# ----------------------------------------------------------
# Program
# ----------------------------------------------------------
add_executable(flux_cli FLux.cpp FLuxCLI.cpp TomlScene.cpp)
set_target_properties(flux_cli PROPERTIES OUTPUT_NAME flux ARCHIVE_OUTPUT_NAME flux_cli)
target_compile_definitions(flux_cli PRIVATE "$<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>")
target_link_libraries(flux_cli PRIVATE argparse::argparse flux flux_mtlx)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(flux_cli PRIVATE /W4)
else()
    target_compile_options(flux_cli PRIVATE -Wall -Wextra)
endif()

# ----------------------------------------------------------
# Layout
# ----------------------------------------------------------
# layoutDir holds flux and its resources as they are installed, and flux finds its resources
# relative to itself. The generator expression keeps a multi-config generator from adding a
# per-configuration directory.
set(layoutDir "${CMAKE_CURRENT_BINARY_DIR}/layout")
set(dataDir "${CMAKE_INSTALL_DATADIR}/flux")
file(RELATIVE_PATH dataFromBin "/${CMAKE_INSTALL_BINDIR}" "/${dataDir}")

set_target_properties(flux_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                                          "$<1:${layoutDir}/${CMAKE_INSTALL_BINDIR}>")
target_compile_definitions(flux_cli PRIVATE FLUX_RELATIVE_MATERIALX_DIR="${dataFromBin}/materialx")

add_custom_target(
    flux_cli_materialx_libraries
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${MATERIALX_STDLIB_DIR}"
            "${layoutDir}/${dataDir}/materialx/libraries"
    VERBATIM)
add_dependencies(flux_cli flux_cli_materialx_libraries)

# ----------------------------------------------------------
# Package
# ----------------------------------------------------------
krr_add_package(flux FILE_NAME "flux-${KRR_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
flux_install_licenses(flux)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(RELATIVE_PATH libFromBin "/${CMAKE_INSTALL_BINDIR}" "/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(flux_cli PROPERTIES INSTALL_RPATH "$ORIGIN/${libFromBin}")
endif()
install(TARGETS flux_cli RUNTIME_DEPENDENCY_SET flux_cli_runtime
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT flux)
install(DIRECTORY "${layoutDir}/${dataDir}/" DESTINATION ${dataDir} COMPONENT flux)

set(runtimeDirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
if(WIN32)
    # NVRTC loads its builtins at run time.
    find_file(nvrtcBuiltins "nvrtc-builtins64_${CUDAToolkit_VERSION_MAJOR}${CUDAToolkit_VERSION_MINOR}.dll"
              PATHS "${CUDAToolkit_BIN_DIR}/x64" "${CUDAToolkit_BIN_DIR}" NO_DEFAULT_PATH NO_CACHE REQUIRED)
    install(FILES "${nvrtcBuiltins}" DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT flux)
    cmake_path(GET nvrtcBuiltins PARENT_PATH nvrtcDir)
    list(APPEND runtimeDirs "${nvrtcDir}")
endif()

# The exclusions leave out the libraries of the system and the NVIDIA driver.
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
install(RUNTIME_DEPENDENCY_SET flux_cli_runtime
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES "[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\]" "^/lib" "^/usr/lib"
        DIRECTORIES ${runtimeDirs}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT flux
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT flux)

if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT flux)
    include(InstallRequiredSystemLibraries)
endif()

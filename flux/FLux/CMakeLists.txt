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
target_compile_definitions(
    flux_cli PRIVATE FLUX_RELATIVE_OPTIX_IR="${dataFromBin}/${flux_optix_ir_name}"
                     FLUX_RELATIVE_MATERIALX_DIR="${dataFromBin}/materialx")

add_custom_command(
    OUTPUT "${layoutDir}/${dataDir}/${flux_optix_ir_name}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_OBJECTS:flux_megakernel_ir>"
            "${layoutDir}/${dataDir}/${flux_optix_ir_name}"
    DEPENDS flux_megakernel_ir "$<TARGET_OBJECTS:flux_megakernel_ir>"
    VERBATIM)
target_sources(flux_cli PRIVATE "${layoutDir}/${dataDir}/${flux_optix_ir_name}")

add_custom_target(
    flux_cli_materialx_libraries
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${MATERIALX_STDLIB_DIR}"
            "${layoutDir}/${dataDir}/materialx/libraries"
    VERBATIM)
add_dependencies(flux_cli flux_cli_materialx_libraries)

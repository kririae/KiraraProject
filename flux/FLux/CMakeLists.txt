add_executable(flux_cli FLux.cpp FLuxCLI.cpp TomlScene.cpp)
set_target_properties(flux_cli PROPERTIES OUTPUT_NAME flux ARCHIVE_OUTPUT_NAME flux_cli)
target_compile_definitions(
    flux_cli PRIVATE FLUX_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>"
                     FLUX_MATERIALX_DIR="${flux_materialx_root}")
target_link_libraries(flux_cli PRIVATE argparse::argparse flux flux_mtlx)
add_dependencies(flux_cli flux_megakernel_ir)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(flux_cli PRIVATE /W4)
else()
    target_compile_options(flux_cli PRIVATE -Wall -Wextra)
endif()

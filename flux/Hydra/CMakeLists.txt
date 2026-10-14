if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    message(FATAL_ERROR "The Hydra delegate supports only x64 Linux")
endif()

# The delegate is a plugin of the USD that Houdini ships, so it builds against that USD.
if(NOT DEFINED ENV{HFS})
    message(FATAL_ERROR "KRR_BUILD_FLUX_HYDRA needs HFS, the Houdini installation to build against")
endif()
set(Houdini_DIR "$ENV{HFS}/toolkit/cmake")
find_package(Houdini CONFIG REQUIRED)

add_library(flux_hydra STATIC AovMapping.cpp Camera.cpp RenderBuffer.cpp RenderDelegate.cpp
                              RenderParam.cpp RenderPass.cpp)

# Houdini's Houdini target serves HDK plugins. It forces -std=c++17, which overrides the C++20 the
# delegate is written in, and adds HDK definitions that the USD headers on Linux do not read. The
# delegate takes only the target's include directories and links the USD libraries it calls.
target_include_directories(flux_hydra SYSTEM
                           PUBLIC "$<TARGET_PROPERTY:Houdini,INTERFACE_INCLUDE_DIRECTORIES>")

target_link_libraries(
    flux_hydra
    PUBLIC flux
           Houdini::Dep::pxr_gf
           Houdini::Dep::pxr_hd
           Houdini::Dep::pxr_hf
           Houdini::Dep::pxr_plug
           Houdini::Dep::pxr_python
           Houdini::Dep::pxr_sdf
           Houdini::Dep::pxr_tf
           Houdini::Dep::pxr_vt
           Houdini::Dep::python${_houdini_python_version})

# tf/hashset.h includes libstdc++'s deprecated <ext/hash_set>.
target_compile_definitions(flux_hydra PUBLIC _GLIBCXX_PERMIT_BACKWARD_HASH)

target_compile_options(flux_hydra PUBLIC -Wall -Wextra)

set_target_properties(flux_hydra PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
                                            VISIBILITY_INLINES_HIDDEN ON)

add_library(hdFlux SHARED RendererPlugin.cpp)
target_link_libraries(hdFlux PRIVATE flux_hydra)

# TF_REGISTRY_FUNCTION registers under the package name.
target_compile_definitions(hdFlux PRIVATE MFB_PACKAGE_NAME=hdFlux MFB_ALT_PACKAGE_NAME=hdFlux)

# The plugin exports no symbol. Plug registers it through static initializers and looks nothing up,
# and an exported symbol could interpose on a library of the host or be preempted by one. Hidden
# visibility also lets the compiler bind the plugin's calls to its own functions directly.
set_target_properties(hdFlux PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

set(exportMap "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
target_link_options(hdFlux PRIVATE "LINKER:--version-script=${exportMap}")
set_property(TARGET hdFlux APPEND PROPERTY LINK_DEPENDS "${exportMap}")

# A symbol that no linked library defines fails the link, not the load.
target_link_options(hdFlux PRIVATE "LINKER:--no-undefined")

# A Houdini package: Houdini loads the plugin from dso/usd and finds its Plug resources under
# dso/usd_plugins. The generator expression keeps a multi-config generator from adding a
# per-configuration directory, which plugInfo.json does not name.
set(packageDir "${CMAKE_CURRENT_BINARY_DIR}/houdini")
set_target_properties(
    hdFlux
    PROPERTIES PREFIX ""
               LIBRARY_OUTPUT_DIRECTORY "$<1:${packageDir}/dso/usd>"
               RUNTIME_OUTPUT_DIRECTORY "$<1:${packageDir}/dso/usd>")

file(GENERATE OUTPUT "${packageDir}/dso/usd_plugins/hdFlux/resources/plugInfo.json"
     INPUT plugInfo.json.in)
file(WRITE "${packageDir}/dso/usd_plugins/plugInfo.json" "{ \"Includes\": [ \"*/resources/\" ] }\n")
configure_file(UsdRenderers.json "${packageDir}/UsdRenderers.json" COPYONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/packages/flux.json" "{ \"hpath\": \"${packageDir}\" }\n")

# The delegate loads its OptiX IR module from the directory of its library.
set(moduleName OptixMegaKernel.optixir)
add_custom_command(
    OUTPUT "${packageDir}/dso/usd/${moduleName}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_OBJECTS:flux_megakernel_ir>"
            "${packageDir}/dso/usd/${moduleName}"
    DEPENDS flux_megakernel_ir "$<TARGET_OBJECTS:flux_megakernel_ir>"
    VERBATIM)

target_sources(hdFlux PRIVATE "${packageDir}/dso/usd/${moduleName}")
target_compile_definitions(hdFlux PRIVATE FLUX_OPTIX_IR_NAME="${moduleName}")

if(KRR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

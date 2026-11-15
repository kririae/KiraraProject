if((NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
    message(FATAL_ERROR "The Hydra delegate requires x64 Windows or Linux")
endif()

set(KRR_HFS
    ""
    CACHE PATH "The Houdini installation whose USD hdFLux builds against")
if(NOT KRR_HFS)
    message(FATAL_ERROR "KRR_BUILD_FLUX_HYDRA needs KRR_HFS")
endif()
set(Houdini_DIR "${KRR_HFS}/toolkit/cmake")
find_package(Houdini CONFIG REQUIRED)

foreach(library MaterialXCore MaterialXFormat pxr_hdMtlx)
    find_library(houdini_${library}
                 NAMES ${library} lib${library}
                 PATHS "${KRR_HFS}/custom/houdini/dsolib" "${KRR_HFS}/dsolib"
                 NO_DEFAULT_PATH NO_CACHE REQUIRED)
    add_library(Houdini::Dep::${library} UNKNOWN IMPORTED)
    set_target_properties(Houdini::Dep::${library} PROPERTIES IMPORTED_LOCATION
                                                              "${houdini_${library}}")
endforeach()

add_library(
    flux_hydra STATIC
    AovMapping.cpp
    ArFileSystem.cpp
    ResolvedPath.cpp
    AreaLight.cpp
    Camera.cpp
    DomeLight.cpp
    Instancer.cpp
    Material.cpp
    MaterialXXml.cpp
    Mesh.cpp
    LightEmission.cpp
    RenderBuffer.cpp
    RenderDelegate.cpp
    RenderParam.cpp
    RenderPass.cpp
    Renderer.cpp
    SampleSchedule.cpp
    Transform.cpp)

# Houdini's Houdini target serves HDK plugins. It forces -std=c++17, which overrides the C++20 the
# delegate is written in, and adds HDK definitions that the USD headers on Linux do not read. The
# delegate takes only the target's include directories and links the USD libraries it calls.
target_include_directories(flux_hydra SYSTEM
                           PUBLIC "$<TARGET_PROPERTY:Houdini,INTERFACE_INCLUDE_DIRECTORIES>")

target_link_libraries(
    flux_hydra
    PUBLIC flux
           flux_mtlx
           Houdini::Dep::MaterialXCore
           Houdini::Dep::MaterialXFormat
           Houdini::Dep::pxr_ar
           Houdini::Dep::pxr_arch
           Houdini::Dep::pxr_gf
           Houdini::Dep::pxr_hd
           Houdini::Dep::pxr_hdMtlx
           Houdini::Dep::pxr_hf
           Houdini::Dep::pxr_plug
           Houdini::Dep::pxr_pxOsd
           Houdini::Dep::pxr_python
           Houdini::Dep::pxr_sdf
           Houdini::Dep::pxr_tf
           Houdini::Dep::pxr_vt
           Houdini::Dep::pxr_work
           Houdini::Dep::python${_houdini_python_version}
    PRIVATE Eigen3::Eigen)

# tf/hashset.h includes libstdc++'s deprecated <ext/hash_set>.
target_compile_definitions(flux_hydra PUBLIC _GLIBCXX_PERMIT_BACKWARD_HASH)

if(WIN32)
    target_compile_definitions(
        flux_hydra PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN HBOOST_ALL_NO_LIB
                          _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
endif()
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(flux_hydra PRIVATE /W4 /bigobj)
else()
    target_compile_options(flux_hydra PRIVATE -Wall -Wextra)
endif()

set_target_properties(flux_hydra PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
                                            VISIBILITY_INLINES_HIDDEN ON)

add_library(hdFLux SHARED RendererPlugin.cpp)
target_link_libraries(hdFLux PRIVATE flux_hydra)

# TF_REGISTRY_FUNCTION registers under the package name.
target_compile_definitions(hdFLux PRIVATE MFB_PACKAGE_NAME=hdFLux MFB_ALT_PACKAGE_NAME=hdFLux)

# The plugin exports no symbol. Plug registers it through static initializers and looks nothing up,
# and an exported symbol could interpose on a library of the host or be preempted by one. Hidden
# visibility also lets the compiler bind the plugin's calls to its own functions directly.
set_target_properties(hdFLux PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(exportMap "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
    target_link_options(hdFLux PRIVATE "LINKER:--version-script=${exportMap}")
    set_property(TARGET hdFLux APPEND PROPERTY LINK_DEPENDS "${exportMap}")
    target_link_options(hdFLux PRIVATE "LINKER:--no-undefined")
endif()

# A Houdini package: Houdini loads the plugin from dso/usd and finds its Plug resources under
# dso/usd_plugins. The generator expression keeps a multi-config generator from adding a
# per-configuration directory, which plugInfo.json does not name.
set(packageDir "${CMAKE_CURRENT_BINARY_DIR}/houdini")
set_target_properties(
    hdFLux
    PROPERTIES PREFIX ""
               LIBRARY_OUTPUT_DIRECTORY "$<1:${packageDir}/dso/usd>"
               RUNTIME_OUTPUT_DIRECTORY "$<1:${packageDir}/dso/usd>")

file(GENERATE OUTPUT "${packageDir}/dso/usd_plugins/hdFLux/resources/plugInfo.json"
     INPUT plugInfo.json.in)
file(WRITE "${packageDir}/dso/usd_plugins/plugInfo.json" "{ \"Includes\": [ \"*/resources/\" ] }\n")
configure_file(UsdRenderers.json "${packageDir}/UsdRenderers.json" COPYONLY)
configure_file(HdFLuxRendererPlugin_Viewport.ds
               "${packageDir}/soho/parameters/HdFLuxRendererPlugin_Viewport.ds" COPYONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/packages/flux.json" "{ \"hpath\": \"${packageDir}\" }\n")

set(resourceDir "${packageDir}/dso/usd_plugins/hdFLux/resources")
add_custom_target(
    hdFLux_materialx_libraries
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${MATERIALX_STDLIB_DIR}"
            "${resourceDir}/libraries"
    VERBATIM)
add_dependencies(hdFLux hdFLux_materialx_libraries)

set(moduleName OptixMegaKernel.optixir)
add_custom_command(
    OUTPUT "${packageDir}/dso/usd/${moduleName}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_OBJECTS:flux_megakernel_ir>"
            "${packageDir}/dso/usd/${moduleName}"
    DEPENDS flux_megakernel_ir "$<TARGET_OBJECTS:flux_megakernel_ir>"
    VERBATIM)

target_sources(hdFLux PRIVATE "${packageDir}/dso/usd/${moduleName}")
target_compile_definitions(hdFLux PRIVATE FLUX_OPTIX_IR_NAME="${moduleName}")

if(KRR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

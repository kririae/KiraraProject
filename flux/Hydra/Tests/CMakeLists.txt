include(KRR_AddTest)

# Adds the test flux.Hydra.<name> from <name>.cpp.
function(add_hydra_test name)
    krr_add_test(
        flux Hydra ${name}
        SOURCES ${name}.cpp
        HARD_DEPENDENCIES flux_hydra)

    # The test builds against Houdini's USD.
    target_include_directories(flux.Hydra.${name} SYSTEM
                               PRIVATE "$<TARGET_PROPERTY:Houdini,INTERFACE_INCLUDE_DIRECTORIES>")
endfunction()

add_hydra_test(AovMappingTests)
add_hydra_test(CameraTests)
add_hydra_test(RenderBufferTests)
add_hydra_test(RenderTests)
add_hydra_test(SampleScheduleTests)

# RenderTests loads the plugin through Plug, so the plugin must be built first.
add_dependencies(flux.Hydra.RenderTests hdFlux)

target_link_libraries(flux.Hydra.RenderTests PRIVATE Houdini::Dep::pxr_cameraUtil)

target_compile_definitions(flux.Hydra.RenderTests
                           PRIVATE HDFLUX_PLUGIN_PATH="${packageDir}/dso/usd_plugins")

set_tests_properties(flux.Hydra.RenderTests PROPERTIES LABELS integration TIMEOUT 120
                                                       RESOURCE_LOCK flux_render)

set_tests_properties(
    flux.Hydra.RenderTests
    PROPERTIES ENVIRONMENT "PXR_MTLX_STDLIB_SEARCH_PATHS=$ENV{HFS}/houdini/materialx/libraries")

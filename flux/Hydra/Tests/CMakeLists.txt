include(KRR_AddTest)

# Adds the unit test flux.Hydra.<name> from <name>.cpp.
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

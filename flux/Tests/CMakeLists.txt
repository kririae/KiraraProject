include(KRR_AddTest)

krr_add_test(
    flux Scene ContextTests
    SOURCES ContextTests.cpp
    HARD_DEPENDENCIES flux)

krr_add_test(
    flux Optix PipelineTests
    SOURCES OptixPipelineTests.cpp
    HARD_DEPENDENCIES flux)
target_compile_definitions(flux.Optix.PipelineTests PRIVATE FLUX_TEST_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>")
target_link_libraries(flux.Optix.PipelineTests PRIVATE CUDA::cudart)
add_dependencies(flux.Optix.PipelineTests flux_megakernel_ir)

krr_add_test(
    flux Optix DeviceBufferTests
    SOURCES DeviceBufferTests.cpp
    HARD_DEPENDENCIES flux)
target_link_libraries(flux.Optix.DeviceBufferTests PRIVATE CUDA::cudart OptiX::OptiX)

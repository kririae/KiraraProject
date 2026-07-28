include(KRR_AddTest)

krr_add_test(
    flux Scene ContextTests
    SOURCES ContextTests.cpp
    HARD_DEPENDENCIES flux)

krr_add_test(
    flux Scene PrimitiveTests
    SOURCES PrimitiveTests.cpp
    HARD_DEPENDENCIES flux)
target_compile_definitions(flux.Scene.PrimitiveTests
                           PRIVATE FLUX_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures")

krr_add_test(
    flux Scene RenderProductTests
    SOURCES RenderProductTests.cpp
    HARD_DEPENDENCIES flux)

krr_add_test(
    flux Sampling SamplerTests
    SOURCES SamplerTests.cu
    HARD_DEPENDENCIES flux)
target_link_libraries(flux.Sampling.SamplerTests PRIVATE CUDA::cudart)

krr_add_test(
    flux Optix PipelineTests
    SOURCES OptixPipelineTests.cpp
    HARD_DEPENDENCIES flux)
target_compile_definitions(flux.Optix.PipelineTests PRIVATE FLUX_TEST_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>")
target_link_libraries(flux.Optix.PipelineTests PRIVATE CUDA::cudart)
add_dependencies(flux.Optix.PipelineTests flux_megakernel_ir)

krr_add_test(
    flux Optix GeometryTests
    SOURCES OptixGeometryTests.cpp
    HARD_DEPENDENCIES flux)
target_compile_definitions(
    flux.Optix.GeometryTests PRIVATE FLUX_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures"
                                     FLUX_TEST_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>")
target_link_libraries(flux.Optix.GeometryTests PRIVATE CUDA::cudart)
add_dependencies(flux.Optix.GeometryTests flux_megakernel_ir)

krr_add_test(
    flux Optix DeviceBufferTests
    SOURCES DeviceBufferTests.cpp
    HARD_DEPENDENCIES flux)
target_link_libraries(flux.Optix.DeviceBufferTests PRIVATE CUDA::cudart OptiX::OptiX)

include(KRR_AddTest)

krr_add_test(
    flux CLI CLITests
    SOURCES FLuxCLITests.cpp ../FLux/FLuxCLI.cpp
    HARD_DEPENDENCIES argparse::argparse flux)

add_test(NAME flux.CLI.CboxSmoke COMMAND $<TARGET_FILE:flux_cli> ${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/CboxMinimal.toml
                                         --backend embree --output ${CMAKE_CURRENT_BINARY_DIR}/FluxCLISmoke/cbox.exr)

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
    flux Scene FilmTests
    SOURCES FilmTests.cu
    HARD_DEPENDENCIES flux)
target_link_libraries(flux.Scene.FilmTests PRIVATE CUDA::cudart)

krr_add_test(
    flux Scene GeometryTests
    SOURCES GeometryTests.cu
    HARD_DEPENDENCIES flux)
target_compile_definitions(flux.Scene.GeometryTests
                           PRIVATE FLUX_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures")
target_link_libraries(flux.Scene.GeometryTests PRIVATE CUDA::cudart)

krr_add_test(
    flux Shading ShadingTests
    SOURCES ShadingTests.cu
    HARD_DEPENDENCIES flux)
target_link_libraries(flux.Shading.ShadingTests PRIVATE CUDA::cudart)

krr_add_test(
    flux Sampling SamplerTests
    SOURCES SamplerTests.cu
    HARD_DEPENDENCIES flux)
target_link_libraries(flux.Sampling.SamplerTests PRIVATE CUDA::cudart)

krr_add_test(
    flux Integrator PathIntegratorTests
    SOURCES PathIntegratorTests.cpp
    HARD_DEPENDENCIES flux)

krr_add_test(
    flux Embree PipelineTests
    SOURCES EmbreePipelineTests.cpp
    HARD_DEPENDENCIES flux)
target_compile_definitions(
    flux.Embree.PipelineTests PRIVATE FLUX_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures"
                                      FLUX_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/EmbreePipelineTests")
target_link_libraries(flux.Embree.PipelineTests PRIVATE OpenImageIO::OpenImageIO)

krr_add_test(
    flux Optix PipelineTests
    SOURCES OptixPipelineTests.cpp
    HARD_DEPENDENCIES flux)
target_compile_definitions(flux.Optix.PipelineTests PRIVATE FLUX_TEST_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>")
target_link_libraries(flux.Optix.PipelineTests PRIVATE CUDA::cudart)
add_dependencies(flux.Optix.PipelineTests flux_megakernel_ir)

krr_add_test(
    flux Optix LaunchParamsTests
    SOURCES OptixLaunchParamsTests.cpp
    HARD_DEPENDENCIES flux)

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

krr_add_test(
    flux Optix KernelUtilsTests
    SOURCES KernelUtilsTests.cu
    HARD_DEPENDENCIES flux)
target_link_libraries(flux.Optix.KernelUtilsTests PRIVATE CUDA::cudart)

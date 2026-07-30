enable_language(CUDA)
find_package(argparse CONFIG REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(embree 4 REQUIRED)
find_package(unofficial-miniply CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)

CPMAddPackage(
    NAME rapidobj
    GITHUB_REPOSITORY guybrush77/rapidobj
    GIT_TAG v1.1
    GIT_SHALLOW TRUE)

CPMAddPackage(
    NAME optix
    GITHUB_REPOSITORY NVIDIA/optix-dev
    GIT_TAG v9.1.0
    GIT_SHALLOW TRUE
    DOWNLOAD_ONLY YES)

add_library(flux_optix_headers INTERFACE)
add_library(OptiX::OptiX ALIAS flux_optix_headers)
target_include_directories(flux_optix_headers SYSTEM INTERFACE "${optix_SOURCE_DIR}/include")

# Isolate OIIO compile definitions in the ImageIO implementation.
add_library(flux_image_io STATIC IO/ImageIO.cpp)
target_include_directories(flux_image_io PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(flux_image_io PRIVATE kira::Core kira::Vecteur OpenImageIO::OpenImageIO)

add_library(
    flux STATIC
    Core/Object.cpp
    Embree/EmbreeContext.cpp
    Embree/EmbreeHandler.cpp
    Embree/EmbreeLightSampler.cpp
    Embree/EmbreeMegaKernel.cpp
    Embree/EmbreeRenderProductPool.cpp
    Integrator/PathIntegrator.cpp
    Optix/OptixAccel.cpp
    Optix/OptixContext.cpp
    Optix/OptixGeometryPool.cpp
    Optix/OptixHandler.cu
    Optix/OptixLightSampler.cpp
    Optix/OptixProgram.cpp
    Optix/OptixRenderProductPool.cpp
    Optix/OptixSbt.cpp
    Sampling/Sampler.cpp
    Scene/Camera.cpp
    Scene/Context.cpp
    Scene/Film.cpp
    Scene/Geometry.cpp
    Scene/Light.cpp
    Scene/Primitive.cpp
    Scene/RenderObject.cpp
    Scene/RenderProduct.cpp
    Scene/TriangleMesh.cpp
    Scene/TXContext.cpp
    Shading/BSDF.cpp)

target_include_directories(flux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(
    flux
    PUBLIC CUDA::cudart
           embree
           kira::Core
           kira::Vecteur
           OptiX::OptiX)
target_link_libraries(
    flux
    PRIVATE Eigen3::Eigen
            flux_image_io
            rapidobj::rapidobj
            TBB::tbb
            unofficial::miniply::miniply)

add_library(flux_megakernel_ir OBJECT Optix/OptixMegaKernel.cu)
set_target_properties(flux_megakernel_ir PROPERTIES CUDA_OPTIX_COMPILATION ON)
target_include_directories(flux_megakernel_ir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(flux_megakernel_ir PRIVATE kira::Vecteur OptiX::OptiX)

add_executable(flux_cli FLux/FLux.cpp FLux/FLuxCLI.cpp FLux/TomlScene.cpp)
set_target_properties(flux_cli PROPERTIES OUTPUT_NAME flux)
target_compile_definitions(flux_cli PRIVATE FLUX_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>")
target_link_libraries(flux_cli PRIVATE argparse::argparse flux)
add_dependencies(flux_cli flux_megakernel_ir)

if(MSVC)
    target_compile_options(flux PRIVATE /W4)
    target_compile_options(flux_cli PRIVATE /W4)
    target_compile_options(flux_image_io PRIVATE /W4)
else()
    target_compile_options(flux PRIVATE -Wall -Wextra)
    target_compile_options(flux_cli PRIVATE -Wall -Wextra)
    target_compile_options(flux_image_io PRIVATE -Wall -Wextra)
endif()

if(KRR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

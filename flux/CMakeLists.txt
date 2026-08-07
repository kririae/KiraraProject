enable_language(CUDA)
find_package(argparse CONFIG REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(embree 4 REQUIRED)
find_package(unofficial-miniply CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
add_compile_definitions(
    # CUDA 12.9 does not propagate CCCL's public int128 switch to this libcudacxx path.
    $<$<COMPILE_LANGUAGE:CUDA>:_CCCL_DISABLE_INT128>
    $<$<COMPILE_LANGUAGE:CUDA>:_LIBCUDACXX_HAS_NO_INT128>
    # NVCC needs toml++'s fallback for compilers that cannot parse bool-returning for_each visitors.
    $<$<COMPILE_LANGUAGE:CUDA>:TOML_RETURN_BOOL_FROM_FOR_EACH_BROKEN=1>
    $<$<COMPILE_LANGUAGE:CUDA>:TOML_RETURN_BOOL_FROM_FOR_EACH_BROKEN_ACKNOWLEDGED>)

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

# Compile OIIO-backed image sources as host C++.
add_library(flux_oiio STATIC Embree/EmbreeImageTexturePool.cpp IO/ImageIO.cpp Optix/OptixImageTexturePool.cpp
                             Scene/ImageAsset.cpp)
target_include_directories(flux_oiio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(
    flux_oiio
    PRIVATE CUDA::cudart
            kira::Core
            kira::Vecteur
            OpenImageIO::OpenImageIO
            OptiX::OptiX)

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
    Sampling/LightSampler.cpp
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
    Shading/BSDF.cpp
    Shading/EDF.cpp
    Shading/Texture.cpp)

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
            flux_oiio
            rapidobj::rapidobj
            TBB::tbb
            unofficial::miniply::miniply)

add_library(flux_megakernel_ir OBJECT Optix/OptixMegaKernel.cu)
set_target_properties(flux_megakernel_ir PROPERTIES CUDA_OPTIX_COMPILATION ON)
target_include_directories(flux_megakernel_ir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(flux_megakernel_ir PRIVATE kira::Vecteur OptiX::OptiX)
target_compile_options(flux_megakernel_ir PRIVATE --use_fast_math)

add_executable(flux_cli FLux/FLux.cpp FLux/FLuxCLI.cpp FLux/TomlScene.cpp)
set_target_properties(flux_cli PROPERTIES OUTPUT_NAME flux)
target_compile_definitions(flux_cli PRIVATE FLUX_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>")
target_link_libraries(flux_cli PRIVATE argparse::argparse flux)
add_dependencies(flux_cli flux_megakernel_ir)

if(MSVC)
    target_compile_options(flux PRIVATE /W4 $<$<COMPILE_LANGUAGE:CXX>:/fp:fast>)
    target_compile_options(flux_cli PRIVATE /W4)
    target_compile_options(flux_oiio PRIVATE /W4)
else()
    target_compile_options(flux PRIVATE -Wall -Wextra $<$<COMPILE_LANGUAGE:CXX>:-ffast-math>)
    target_compile_options(flux_cli PRIVATE -Wall -Wextra)
    target_compile_options(flux_oiio PRIVATE -Wall -Wextra)
endif()
target_compile_options(flux PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>)

if(KRR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

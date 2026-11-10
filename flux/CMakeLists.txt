# ----------------------------------------------------------
# Language and CUDA runtime
# ----------------------------------------------------------
enable_language(CUDA)
# With a static CUDA runtime, a plugin carries its own copy, which the runtime of a host application
# can neither replace nor conflict with.
set(CMAKE_CUDA_RUNTIME_LIBRARY Static)

# ----------------------------------------------------------
# Dependencies
# ----------------------------------------------------------
find_package(argparse CONFIG REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(embree 4 REQUIRED)
find_package(MaterialX 1.39.5 CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(unofficial-miniply CONFIG REQUIRED)

# ----------------------------------------------------------
# Packages built from source
# ----------------------------------------------------------
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

# ----------------------------------------------------------
# Compiler options
# ----------------------------------------------------------
add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
add_compile_definitions(
    # CUDA 12.9 does not propagate CCCL's public int128 switch to this libcudacxx path.
    $<$<COMPILE_LANGUAGE:CUDA>:_CCCL_DISABLE_INT128>
    $<$<COMPILE_LANGUAGE:CUDA>:_LIBCUDACXX_HAS_NO_INT128>
    # NVCC needs toml++'s fallback for compilers that cannot parse bool-returning for_each visitors.
    $<$<COMPILE_LANGUAGE:CUDA>:TOML_RETURN_BOOL_FROM_FOR_EACH_BROKEN=1>
    $<$<COMPILE_LANGUAGE:CUDA>:TOML_RETURN_BOOL_FROM_FOR_EACH_BROKEN_ACKNOWLEDGED>)

# ----------------------------------------------------------
# Embedded files
# ----------------------------------------------------------
# Writes output, under the generated directory, with a {path, text} entry for each file, read at
# configure time. The path is the one that an #include names.
function(flux_embed output)
    set(entries "")
    foreach(file IN LISTS ARGN)
        file(READ ${file} text)
        string(APPEND entries "{\"flux/${file}\", R\"flux(${text})flux\"},\n")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARGN})
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${output} CONTENT "@entries@" @ONLY)
endfunction()

# ----------------------------------------------------------
# Libraries
# ----------------------------------------------------------
# Compile OIIO-backed image sources as host C++.
add_library(flux_oiio STATIC Embree/EmbreeImageTexturePool.cpp IO/ImageIO.cpp Optix/OptixImagePool.cpp
                             Scene/ImageAsset.cpp)
target_include_directories(flux_oiio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(
    flux_oiio
    PRIVATE CUDA::cudart_static
            kira::Core
            kira::Vecteur
            OpenImageIO::OpenImageIO
            OptiX::OptiX)

add_library(
    flux STATIC
    Core/Object.cpp
    Embree/EmbreeAccumulationBuffer.cpp
    Embree/EmbreeContext.cpp
    Embree/EmbreeHandler.cpp
    Embree/EmbreeLightSampler.cpp
    Embree/EmbreeMegaKernel.cpp
    Integrator/PathIntegrator.cpp
    Optix/DeletionQueue.cpp
    Optix/OptixAccumulationBuffer.cu
    Optix/OptixShaderModule.cpp
    Optix/OptixShaderPool.cpp
    Optix/OptixShaderCompiler.cpp
    Optix/OptixContext.cpp
    Optix/OptixDeviceSession.cpp
    Optix/OptixGas.cpp
    Optix/OptixGeometryPool.cpp
    Optix/OptixHandler.cu
    Optix/OptixIas.cpp
    Optix/OptixLightSampler.cu
    Optix/OptixMegaKernelModule.cpp
    Optix/OptixProgram.cpp
    Optix/OptixSbt.cpp
    Optix/OptixTextureObject.cpp
    Optix/SlangCompiler.cpp
    Sampling/Distribution2D.cpp
    Sampling/LightPowerDistribution.cpp
    Sampling/Sampler.cpp
    Scene/Camera.cpp
    Scene/Context.cpp
    Scene/ContextIndexMap.cpp
    Scene/EnvMapLight.cpp
    Scene/Film.cpp
    Scene/Geometry.cpp
    Scene/Light.cpp
    Scene/LightTableData.cpp
    Scene/Primitive.cpp
    Scene/RenderObject.cpp
    Scene/SceneTableData.cpp
    Scene/ShaderAsset.cpp
    Scene/TriangleMesh.cpp
    Scene/TXContext.cpp
    Shading/BSDF.cpp
    Shading/EDF.cpp
    Shading/Texture.cpp)

flux_embed(flux/Optix/OptixSlangModules.inc Shading/flux-material.slang Optix/flux-context.slang
           Optix/flux-callable.slang)
flux_embed(
    flux/Optix/OptixNvrtcHeaders.inc
    Optix/OptixShaderCallable.h
    Optix/OptixCallableLaunchParams.h
    Optix/OptixImageTexture.cuh
    Optix/OptixImageTexture.h
    Scene/ImageComponentMapping.h)

target_include_directories(flux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(flux PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(
    flux
    PUBLIC CUDA::cudart_static
           embree
           kira::Core
           kira::Vecteur
           OptiX::OptiX)
target_link_libraries(
    flux
    PRIVATE CUDA::cuda_driver
            CUDA::nvrtc
            Eigen3::Eigen
            flux_oiio
            rapidobj::rapidobj
            slang::slang
            TBB::tbb
            unofficial::miniply::miniply)

# ----------------------------------------------------------
# MaterialX
# ----------------------------------------------------------
flux_embed(flux/Mtlx/MtlxLibraries.inc Mtlx/libraries/genflux.mtlx
           Mtlx/libraries/stdlib_genflux_impl.mtlx
           Mtlx/libraries/pbrlib_genflux_impl.mtlx)
flux_embed(flux/Mtlx/MtlxSlangLibraries.inc Mtlx/libraries/mx-prelude.slang
           Mtlx/libraries/mx-ggx-reflection-sampler.slang
           Mtlx/libraries/mx-ggx-refraction-sampler.slang Mtlx/libraries/mx-burley-diffuse-bsdf.slang
           Mtlx/libraries/mx-conductor-bsdf.slang Mtlx/libraries/mx-dielectric-bsdf.slang
           Mtlx/libraries/mx-generalized-schlick-bsdf.slang
           Mtlx/libraries/mx-oren-nayar-diffuse-bsdf.slang Mtlx/libraries/mx-sheen-bsdf.slang
           Mtlx/libraries/mx-subsurface-bsdf.slang Mtlx/libraries/mx-translucent-bsdf.slang)

add_library(
    flux_mtlx STATIC
    Mtlx/FluxShaderGenerator.cpp Mtlx/FluxShaderGraphRefactor.cpp Mtlx/FluxSourceCodeNode.cpp
    Mtlx/MtlxShaderGenerator.cpp Mtlx/MtlxEmission.cpp)
target_include_directories(flux_mtlx PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
                                             ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(
    flux_mtlx
    PUBLIC kira::Core kira::Vecteur
    PRIVATE MaterialXCore MaterialXFormat MaterialXGenShader MaterialXGenSlang)
get_filename_component(flux_materialx_root "${MATERIALX_STDLIB_DIR}" DIRECTORY)

# ----------------------------------------------------------
# Programs
# ----------------------------------------------------------
add_library(flux_megakernel_ir OBJECT Optix/OptixMegaKernel.cu)
set_target_properties(flux_megakernel_ir PROPERTIES CUDA_OPTIX_COMPILATION ON)
target_include_directories(flux_megakernel_ir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(flux_megakernel_ir PRIVATE kira::Vecteur OptiX::OptiX)
target_compile_options(flux_megakernel_ir PRIVATE --use_fast_math)

add_executable(flux_cli FLux/FLux.cpp FLux/FLuxCLI.cpp FLux/TomlScene.cpp)
set_target_properties(flux_cli PROPERTIES OUTPUT_NAME flux)
target_compile_definitions(
    flux_cli PRIVATE FLUX_OPTIX_IR="$<TARGET_OBJECTS:flux_megakernel_ir>"
                     FLUX_MATERIALX_DIR="${flux_materialx_root}")
target_link_libraries(flux_cli PRIVATE argparse::argparse flux flux_mtlx)
add_dependencies(flux_cli flux_megakernel_ir)

# ----------------------------------------------------------
# Warnings
# ----------------------------------------------------------
if(MSVC)
    target_compile_options(flux PRIVATE /W4 $<$<COMPILE_LANGUAGE:CXX>:/fp:fast>)
    target_compile_options(flux_cli PRIVATE /W4)
    target_compile_options(flux_mtlx PRIVATE /W4)
    target_compile_options(flux_oiio PRIVATE /W4)
else()
    target_compile_options(flux PRIVATE -Wall -Wextra $<$<COMPILE_LANGUAGE:CXX>:-ffast-math>)
    target_compile_options(flux_cli PRIVATE -Wall -Wextra)
    target_compile_options(flux_mtlx PRIVATE -Wall -Wextra)
    target_compile_options(flux_oiio PRIVATE -Wall -Wextra)
endif()
target_compile_options(flux PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>)

# ----------------------------------------------------------
# Subdirectories
# ----------------------------------------------------------
if(KRR_BUILD_FLUX_HYDRA)
    add_subdirectory(Hydra)
endif()

if(KRR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

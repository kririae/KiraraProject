enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

CPMAddPackage(
    NAME optix
    GITHUB_REPOSITORY NVIDIA/optix-dev
    GIT_TAG v9.1.0
    GIT_SHALLOW TRUE
    DOWNLOAD_ONLY YES)

add_library(flux_optix_headers INTERFACE)
add_library(OptiX::OptiX ALIAS flux_optix_headers)
target_include_directories(flux_optix_headers SYSTEM INTERFACE "${optix_SOURCE_DIR}/include")

add_library(
    flux STATIC
    Core/Object.cpp
    Optix/OptixContext.cpp
    Optix/OptixHandler.cpp
    Scene/Context.cpp
    Scene/RenderObject.cpp
    Scene/TXContext.cpp)

target_include_directories(flux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(
    flux
    PUBLIC kira::Core
    PRIVATE CUDA::cudart OptiX::OptiX)

add_library(flux_megakernel_ir OBJECT Optix/Megakernel.cu)
set_target_properties(flux_megakernel_ir PROPERTIES CUDA_OPTIX_COMPILATION ON)
target_link_libraries(flux_megakernel_ir PRIVATE OptiX::OptiX)

if(MSVC)
    target_compile_options(flux PRIVATE /W4)
else()
    target_compile_options(flux PRIVATE -Wall -Wextra)
endif()

if(KRR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

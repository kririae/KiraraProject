project(kirara-dance LANGUAGES C CXX)

include(KRR_AddModule)
include(KRR_Message)
include(KRR_AddTest)

# ----------------------------------------------------------
# Dependency setup
# ----------------------------------------------------------
CPMAddPackage(
    NAME glfw
    GITHUB_REPOSITORY glfw/glfw
    GIT_TAG 3.4
    OPTIONS "GLFW_BUILD_EXAMPLES OFF" "GLFW_BUILD_TESTS OFF" "GLFW_BUILD_DOCS OFF")

#
# Currently assimp don't have good support for OpenUSD. We might load directly from `USDZ` file in the future.
#

CPMAddPackage(
    NAME assimp
    URL "https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz"
    VERSION 5.4.3
    OPTIONS "ASSIMP_BUILD_TESTS OFF" "ASSIMP_BUILD_USD_IMPORTER OFF"
            # see https://github.com/assimp/assimp/issues/5908
            "ASSIMP_WARNINGS_AS_ERRORS OFF")

krr_message(INFO "Pulling slang binaries... This might take long")

# Boost the version if needed
set(KRD_SLANG_VERSION 2025.6.4)

if(UNIX AND NOT APPLE)
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        krr_message(ERROR "Kirara Project only supports x86_64 architecture on Linux")
    endif()
    CPMAddPackage(
        NAME slang
        URL "https://github.com/shader-slang/slang/releases/download/v${KRD_SLANG_VERSION}/slang-${KRD_SLANG_VERSION}-linux-x86_64.tar.gz"
        VERSION ${KRD_SLANG_VERSION}
        DOWNLOAD_ONLY)
elseif(WIN32)
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        krr_message(ERROR "Kirara Project only supports x86_64 architecture on Windows")
    endif()
    CPMAddPackage(
        NAME slang
        URL "https://github.com/shader-slang/slang/releases/download/v${KRD_SLANG_VERSION}/slang-${KRD_SLANG_VERSION}-windows-x86_64.tar.gz"
        VERSION ${KRD_SLANG_VERSION}
        DOWNLOAD_ONLY)
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        CPMAddPackage(
            NAME slang
            URL "https://github.com/shader-slang/slang/releases/download/v${KRD_SLANG_VERSION}/slang-${KRD_SLANG_VERSION}-macos-aarch64.tar.gz"
            VERSION ${KRD_SLANG_VERSION}
            DOWNLOAD_ONLY)
    else()
        krr_message(ERROR "Kirara Project only supports arm64 architecture on MacOS")
    endif()
else()
    krr_message(ERROR "Unsupported platform: Only Linux x64/Windows x64/MacOS is supported by Kirara Project")
endif()

if(slang_ADDED)
    set(SLANG_ROOT ${slang_SOURCE_DIR})

    add_library(slang::slang SHARED IMPORTED)
    add_library(slang::gfx SHARED IMPORTED)

    if(UNIX AND NOT APPLE)
        set_target_properties(slang::slang PROPERTIES IMPORTED_LOCATION "${SLANG_ROOT}/lib/libslang.so"
                                                      INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
        set_target_properties(slang::gfx PROPERTIES IMPORTED_LOCATION "${SLANG_ROOT}/lib/libgfx.so"
                                                    INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
    elseif(WIN32)
        set_target_properties(
            slang::slang
            PROPERTIES IMPORTED_LOCATION "${SLANG_ROOT}/bin/slang.dll"
                       IMPORTED_IMPLIB "${SLANG_ROOT}/lib/slang.lib"
                       INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
        set_target_properties(
            slang::gfx
            PROPERTIES IMPORTED_LOCATION "${SLANG_ROOT}/bin/gfx.dll"
                       IMPORTED_IMPLIB "${SLANG_ROOT}/lib/gfx.lib"
                       INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
    elseif(APPLE)
        set_target_properties(slang::slang PROPERTIES IMPORTED_LOCATION "${SLANG_ROOT}/lib/libslang.dylib"
                                                      INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
        set_target_properties(slang::gfx PROPERTIES IMPORTED_LOCATION "${SLANG_ROOT}/lib/libgfx.dylib"
                                                    INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
    endif()
endif()

CPMAddPackage(
    NAME range-v3
    GITHUB_REPOSITORY ericniebler/range-v3
    GIT_TAG 0.12.0)

# Include the oneTBB library
CPMAddPackage(
    NAME oneTBB
    GITHUB_REPOSITORY uxlfoundation/oneTBB
    VERSION 2022.1.0
    OPTIONS "TBB_TEST OFF" "BUILD_SHARED_LIBS ON")

# ----------------------------------------------------------
# Executable setup
# ----------------------------------------------------------
add_library(
    kirara-backend STATIC
    #
    #
    Core/DeviceUtils.cpp
    Core/ProgramBuilder.cpp
    Core/ShaderCursor.cpp
    Core/SlangContext.cpp
    Core/Window.cpp
    #
    #
    FacadeRender/SlangGraphicsContext.cpp
    FacadeRender/TriMeshResource.cpp
    #
    #
    # Instant/InstantCamera.cpp
    # Instant/InstantObject.cpp
    # Instant/InstantScene.cpp
    # Instant/InstantTriangleMesh.cpp
    # Instant/SlangGraphicsContext.cpp
    #
    #
    # Scene/Camera.cpp
    # Scene/Scene.cpp
    # Scene/SceneBuilder.cpp
    # Scene/SceneGraph.cpp
    # Scene/SceneObject.cpp
    # Scene/Transform.cpp
    # Scene/TriangleMesh.cpp
    #
    #
    Scene/Animation.cpp
    Scene/Camera.cpp
    Scene/Geometry.cpp
    Scene/SceneBuilder.cpp
    Scene/SceneRoot.cpp
    Scene/Transform.cpp
    Scene/TriangleMesh.cpp
    #
    #
    SceneGraph/Node.cpp
    SceneGraph/Visitors.cpp)

target_link_libraries(
    kirara-backend
    PUBLIC assimp
           glfw
           igl::core
           kira::kira
           slang::gfx
           slang::slang
           range-v3::range-v3
           TBB::tbb)

# Include towards current directory.
target_include_directories(kirara-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#
add_executable(kirara-dance #
               KiraraDance/Backward.cpp KiraraDance/Editor.cpp KiraraDance/KiraraDance.cpp)
target_link_libraries(kirara-dance PRIVATE kirara-backend Backward::Interface)

# ----------------------------------------------------------
# Additionally, copy auxiliary files to the output directory
# ----------------------------------------------------------
file(
    GLOB_RECURSE SHADER_FILES
    LIST_DIRECTORIES false
    ${CMAKE_CURRENT_SOURCE_DIR}/*.slang ${CMAKE_CURRENT_SOURCE_DIR}/*.slangh ${CMAKE_CURRENT_SOURCE_DIR}/*.hlsli
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl)

foreach(SHADER_FILE ${SHADER_FILES})
    file(RELATIVE_PATH REL_SHADER_FILE ${CMAKE_CURRENT_SOURCE_DIR} ${SHADER_FILE})
    get_filename_component(SHADER_DIR ${REL_SHADER_FILE} DIRECTORY)

    add_custom_command(
        TARGET kirara-dance
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:kirara-dance>/${SHADER_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_FILE} $<TARGET_FILE_DIR:kirara-dance>/${REL_SHADER_FILE})

    krr_message(INFO "Copying shader file: ${REL_SHADER_FILE}")
endforeach()

if(WIN32)
    # Copy DLLs to the output directory
    add_custom_command(
        TARGET kirara-dance
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:kirara-dance> $<TARGET_FILE_DIR:kirara-dance>
        COMMAND_EXPAND_LISTS)

    krr_message(INFO "Copy DLLs to the output directory")
endif()

# ----------------------------------------------------------
# Setup UnitTests
# ----------------------------------------------------------
if(KRR_BUILD_TESTS AND FALSE)
    krr_add_test(
        krd Backend AnimationTests
        SOURCES UnitTests/AnimationTests.cpp
        HARD_DEPENDENCIES kirara-backend)
endif()

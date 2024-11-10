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
    OPTIONS "ASSIMP_BUILD_TESTS OFF" "ASSIMP_BUILD_USD_IMPORTER OFF")

krr_message(INFO "Pulling slang binaries... This might take long")

if(UNIX AND NOT APPLE)
    CPMAddPackage(
        NAME slang
        URL "https://github.com/shader-slang/slang/releases/download/v2024.14.5/slang-2024.14.5-linux-x86_64.tar.gz"
        VERSION 2024.14.5
        DOWNLOAD_ONLY)
elseif(WIN32)
    CPMAddPackage(
        NAME slang
        URL "https://github.com/shader-slang/slang/releases/download/v2024.14.5/slang-2024.14.5-windows-x86_64.tar.gz"
        VERSION 2024.14.5
        DOWNLOAD_ONLY)
else()
    krr_message(ERROR "Unsupported platform: Only Linux/Windows x64 is supported by Kirara Project")
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
    endif()
endif()

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
    Instant/InstantCamera.cpp
    Instant/InstantObject.cpp
    Instant/InstantScene.cpp
    Instant/InstantTriangleMesh.cpp
    Instant/SlangGraphicsContext.cpp
    #
    #
    Scene/AnimatableObject.cpp
    Scene/Animation.cpp
    Scene/Camera.cpp
    Scene/Scene.cpp
    Scene/SceneBuilder.cpp
    Scene/SceneGraph.cpp
    Scene/SceneObject.cpp
    Scene/Transform.cpp
    Scene/TriangleMesh.cpp)

target_link_libraries(
    kirara-backend
    PUBLIC assimp
           glfw
           igl::core
           kira::kira
           slang::gfx
           slang::slang)

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
if(KRR_BUILD_TESTS)
    krr_add_test(
        krd Backend AnimationTests
        SOURCES UnitTests/AnimationTests.cpp
        HARD_DEPENDENCIES kirara-backend)
endif()

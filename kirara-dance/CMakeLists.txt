project(kirara-dance LANGUAGES C CXX)

include(KRR_AddModule)
include(KRR_Message)
include(KRR_AddTest)

# ----------------------------------------------------------
# Dependency setup
# ----------------------------------------------------------
find_package(glfw3 CONFIG REQUIRED)

#
# Currently assimp don't have good support for OpenUSD. We might load directly from `USDZ` file in the future.
#
find_package(assimp CONFIG REQUIRED)

find_package(range-v3 CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)

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

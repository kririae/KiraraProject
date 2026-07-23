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
find_package(Eigen3 CONFIG REQUIRED)

# ----------------------------------------------------------
# Executable setup
# ----------------------------------------------------------

add_library(kirara-backend STATIC IPC/CCDPrimitives.cpp)
target_include_directories(kirara-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kirara-backend PUBLIC Eigen3::Eigen kira::Core)

#
add_executable(kirara-dance #
               KiraraDance/Backward.cpp KiraraDance/KiraraDance.cpp)
target_link_libraries(kirara-dance PRIVATE kirara-backend Backward::Interface)

# ----------------------------------------------------------
# Setup UnitTests
# ----------------------------------------------------------
if(KRR_BUILD_TESTS)
    krr_add_test(
        krd IPC CCDTests
        SOURCES Tests/Unit/CCDTests.cpp
        HARD_DEPENDENCIES kirara-backend)

    krr_add_test(
        krd Core MathTests
        SOURCES Tests/Unit/MathTests.cpp
        HARD_DEPENDENCIES kirara-backend)
endif()

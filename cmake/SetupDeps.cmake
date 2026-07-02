include(KRR_Message)

# ----------------------------------------------------------
# cmake-scripts
# ----------------------------------------------------------
CPMAddPackage(
    NAME cmake-scripts
    GITHUB_REPOSITORY StableCoder/cmake-scripts
    GIT_TAG "25.08")

list(APPEND CMAKE_MODULE_PATH "${cmake-scripts_SOURCE_DIR}")

# ----------------------------------------------------------
# Backtrace utils
# ----------------------------------------------------------
find_package(Backward CONFIG REQUIRED)

if(KRR_BUILD_TESTS)
    # ----------------------------------------------------------
    # GTest
    # ----------------------------------------------------------
    find_package(GTest CONFIG REQUIRED)
endif()

if(KRR_BUILD_COMPTIME_TESTS)
    # ----------------------------------------------------------
    # ut2
    # ----------------------------------------------------------
    CPMAddPackage(
        NAME ut
        GITHUB_REPOSITORY qlibs/ut
        GIT_TAG v2.1.2
        DOWNLOAD_ONLY YES)

    if(ut_ADDED)
        add_library(ut INTERFACE)
        target_include_directories(ut INTERFACE ${ut_SOURCE_DIR})
    endif()
endif()

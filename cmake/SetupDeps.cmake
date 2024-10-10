include(KRR_Message)

# ----------------------------------------------------------
# cmake-scripts
# ----------------------------------------------------------
CPMAddPackage(
  NAME cmake-scripts
  GITHUB_REPOSITORY StableCoder/cmake-scripts
  GIT_TAG "24.08.1"
)

list(APPEND CMAKE_MODULE_PATH "${cmake-scripts_SOURCE_DIR}")

# ----------------------------------------------------------
# Backtrace utils
# ----------------------------------------------------------
CPMAddPackage(
  NAME backward
  GITHUB_REPOSITORY bombela/backward-cpp
  GIT_TAG master
)

CPMAddPackage(
  NAME Eigen3
  URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip
  OPTIONS
    "BUILD_TESTING OFF"
    "EIGEN_BUILD_DOC OFF"
    "EIGEN_BUILD_DEMOS OFF")

if(KRR_BUILD_TESTS)
  # ----------------------------------------------------------
  # GTest
  # ----------------------------------------------------------
  CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG main)
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

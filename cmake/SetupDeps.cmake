include(KRR_Message)

# ----------------------------------------------------------
# cmake-scripts
# ----------------------------------------------------------
CPMAddPackage(
  NAME cmake-scripts
  GITHUB_REPOSITORY StableCoder/cmake-scripts
  GIT_TAG "24.04"
)

list(APPEND CMAKE_MODULE_PATH ${cmake-scripts_SOURCE_DIR})

# ----------------------------------------------------------
# Stacktrace
# ----------------------------------------------------------
CPMAddPackage(
  NAME backward-cpp
  GITHUB_REPOSITORY bombela/backward-cpp
  GIT_TAG v1.6
  DOWNLOAD_ONLY YES
)

if(backward-cpp_ADDED)
  list(APPEND CMAKE_PREFIX_PATH "${backward-cpp_SOURCE_DIR}")
  find_package(Backward REQUIRED)
else()
  krr_message(ERROR "Failed to add backward-cpp package, consider setting `KRR_WITH_BACKTRACE=OFF`")
endif()

if(KRR_BUILD_TESTS)
  # ----------------------------------------------------------
  # GTest
  # ----------------------------------------------------------
  CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG main
  )

  # ----------------------------------------------------------
  # ut2
  # ----------------------------------------------------------
  CPMAddPackage(
    NAME ut
    GITHUB_REPOSITORY qlibs/ut
    GIT_TAG v2.1.1
    DOWNLOAD_ONLY YES
  )

  if(ut_ADDED)
    add_library(ut INTERFACE)
    target_include_directories(ut INTERFACE ${ut_SOURCE_DIR})
  endif()
endif()

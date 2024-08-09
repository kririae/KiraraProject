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

if(KRR_BUILD_TESTS)
  # ----------------------------------------------------------
  # GTest
  # ----------------------------------------------------------
  CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG main
  )
endif()

if(KRR_BUILD_COMPTIME_TESTS)
  # ----------------------------------------------------------
  # ut2
  # ----------------------------------------------------------
  CPMAddPackage(
    NAME ut
    GITHUB_REPOSITORY qlibs/ut
    GIT_TAG v2.1.2
    DOWNLOAD_ONLY YES
  )

  if(ut_ADDED)
    add_library(ut INTERFACE)
    target_include_directories(ut INTERFACE ${ut_SOURCE_DIR})
  endif()
endif()
include(KRR_AddModule)
include(KRR_EnableClangTidy)

configure_file(include/kira/CommitHash.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/kira/CommitHash.h @ONLY)
configure_file(include/kira/Version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/kira/Version.h @ONLY)

# ----------------------------------------------------------
# Setup module-specific dependencies
# ----------------------------------------------------------
CPMAddPackage(
  NAME fmt
  URL "https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.tar.gz"
)

CPMAddPackage(
  NAME spdlog
  URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz"
  OPTIONS
    "SPDLOG_BUILD_SHARED OFF"
    "SPDLOG_INSTALL ON"
    "SPDLOG_FMT_EXTERNAL_HO ON"
)

# We do need a typed-array constexpr container. So let's use mp~!
CPMAddPackage(
  NAME mp
  GITHUB_REPOSITORY qlibs/mp
  GIT_TAG v2.0.4
)

if(mp_ADDED)
  add_library(mp INTERFACE)
  target_include_directories(mp SYSTEM INTERFACE ${mp_SOURCE_DIR})
  add_library(mp::mp ALIAS mp)
endif()

CPMAddPackage(
  NAME reflect-cpp
  URL "https://github.com/getml/reflect-cpp/archive/refs/tags/v0.14.0.tar.gz"
  OPTIONS
    "REFLECTCPP_BUILD_SHARED OFF"
)

# ----------------------------------------------------------
# Do add module
# ----------------------------------------------------------
krr_add_module(kira Core
  HEADERS
    kira/detail/Logger.h
    kira/Anyhow.h
    kira/Assertions.h
    kira/CommitHash.h
    kira/Compiler.h
    kira/FileResolver.h
    kira/KIRA.h
    kira/Logger.h
    kira/Macros.h
    kira/Reflection.h
    kira/SmallVector.h
    kira/Types.h
    kira/Utils.h
    kira/Version.h
  SOURCES
    FileResolver.cpp
    KIRA.cpp
    Logger.cpp
    SmallVector.cpp
  HARD_DEPENDENCIES
    fmt::fmt
    reflectcpp
    spdlog::spdlog
  CMAKE_SUBDIRS
    tests
)

if(KRR_ENABLE_CLANG_TIDY)
  krr_enable_clang_tidy(kira::Core)
endif()

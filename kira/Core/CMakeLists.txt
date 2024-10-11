include(KRR_AddModule)
include(KRR_EnableClangTidy)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/kira/CommitHash.h.in
               ${CMAKE_CURRENT_SOURCE_DIR}/include/kira/CommitHash.h @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/kira/Version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/kira/Version.h
               @ONLY)

# ----------------------------------------------------------
# Setup module-specific dependencies
# ----------------------------------------------------------
CPMAddPackage(NAME fmt URL "https://github.com/fmtlib/fmt/archive/refs/tags/11.0.2.tar.gz")

CPMAddPackage(
    NAME spdlog
    URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz"
    OPTIONS "SPDLOG_BUILD_SHARED OFF" "SPDLOG_INSTALL ON" "SPDLOG_FMT_EXTERNAL_HO ON")

# We do need a typed-array constexpr container. So let's use mp~!
CPMAddPackage(
    NAME mp
    GITHUB_REPOSITORY qlibs/mp
    VERSION 2.0.11)

if(mp_ADDED)
    add_library(mp INTERFACE)
    target_include_directories(mp SYSTEM INTERFACE ${mp_SOURCE_DIR})
    add_library(mp::mp ALIAS mp)
endif()

CPMAddPackage(
    NAME reflect-cpp
    URL "https://github.com/getml/reflect-cpp/archive/refs/tags/v0.14.1.tar.gz"
    OPTIONS "REFLECTCPP_BUILD_SHARED OFF")

CPMAddPackage(
    NAME tomlplusplus
    URL "https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz"
    # This enables top-level installation on `tomlplusplus` as subdirectory
    PATCHES "${KRR_ROOT_DIR}/cmake/patches/tomlplusplus.patch")

CPMAddPackage(
    NAME magic_enum
    GITHUB_REPOSITORY Neargye/magic_enum
    VERSION 0.9.6
    OPTIONS "MAGIC_ENUM_OPT_INSTALL ON")

# ----------------------------------------------------------
# Do add module
# ----------------------------------------------------------
krr_add_module(
    kira Core
    HEADERS kira/detail/Logger.h
            kira/detail/PropertiesMixins.h
            kira/Anyhow.h
            kira/Assertions.h
            kira/CommitHash.h
            kira/Compiler.h
            kira/Core.h
            kira/FileResolver.h
            kira/Logger.h
            kira/Macros.h
            kira/Properties.h
            kira/Reflection.h
            kira/SmallVector.h
            kira/Types.h
            kira/Utils.h
            kira/Version.h
    SOURCES Core.cpp
            FileResolver.cpp
            Logger.cpp
            Properties.cpp
            SmallVector.cpp
    HARD_DEPENDENCIES
        fmt::fmt
        magic_enum
        reflectcpp
        spdlog::spdlog
        tomlplusplus::tomlplusplus
    CMAKE_SUBDIRS tests)

if(KRR_ENABLE_CLANG_TIDY)
    krr_enable_clang_tidy(kira::Core)
endif()

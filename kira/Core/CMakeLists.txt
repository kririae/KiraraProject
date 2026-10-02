include(KRR_AddModule)
include(KRR_EnableClangTidy)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/kira/CommitHash.h.in
               ${CMAKE_CURRENT_SOURCE_DIR}/include/kira/CommitHash.h @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/kira/Version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/kira/Version.h
               @ONLY)

# ----------------------------------------------------------
# Setup module-specific dependencies
# ----------------------------------------------------------
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

find_package(tomlplusplus CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

# ----------------------------------------------------------
# Do add module
# ----------------------------------------------------------
krr_add_module(
    kira Core
    HEADERS kira/detail/Logger.h
            kira/Anyhow.h
            kira/Assertions.h
            kira/CommitHash.h
            kira/Compiler.h
            kira/Core.h
            kira/FileResolver.h
            kira/Logger.h
            kira/Macros.h
            kira/Properties.h
            kira/SmallVector.h
            kira/Types.h
            kira/Utils.h
            kira/Version.h
    SOURCES Core.cpp
            FileResolver.cpp
            Logger.cpp
            Properties.cpp
            SmallVector.cpp
    HARD_DEPENDENCIES fmt::fmt magic_enum::magic_enum spdlog::spdlog tomlplusplus::tomlplusplus
    CMAKE_SUBDIRS tests)

if(KRR_ENABLE_CLANG_TIDY)
    krr_enable_clang_tidy(kira::Core)
endif()

cmake_minimum_required(VERSION 3.27.4)
project(kirara LANGUAGES C CXX)

set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_PCH_WARN_INVALID ON)

set(KRR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
mark_as_advanced(KRR_ROOT_DIR)

if(MSVC)
  add_compile_options(/Zc:preprocessor)
endif()

execute_process(
  WORKING_DIRECTORY ${KRR_ROOT_DIR}
  COMMAND git rev-parse --short HEAD
  RESULT_VARIABLE SHORT_HASH_RESULT
  OUTPUT_VARIABLE KRR_COMMIT_SHORT_HASH)
execute_process(
  WORKING_DIRECTORY ${KRR_ROOT_DIR}
  COMMAND git rev-parse HEAD
  RESULT_VARIABLE SHORT_HASH_RESULT
  OUTPUT_VARIABLE KRR_COMMIT_HASH)
string(STRIP ${KRR_COMMIT_HASH} KRR_COMMIT_HASH)
string(STRIP ${KRR_COMMIT_SHORT_HASH} KRR_COMMIT_SHORT_HASH)

set(KRR_VERSION_MAJOR 0)
set(KRR_VERSION_MINOR 0)
set(KRR_VERSION_PATCH 1)

message("╔════════════════════════════════════════════════════════════════════════════╗")
message("║                               Kirara Project                               ║")
message("╚════════════════════════════════════════════════════════════════════════════╝")
message("Version:     ${KRR_VERSION_MAJOR}.${KRR_VERSION_MINOR}.${KRR_VERSION_PATCH}")
message("Commit Hash: ${KRR_COMMIT_HASH}")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of the CMake build" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${KRR_ROOT_DIR}/cmake" "${CMAKE_BINARY_DIR}")

# ----------------------------------------------------------
# Platform detection
# ----------------------------------------------------------
include(KRR_Message)

if(WIN32)
  set(KRR_BUILD_FOR_WINDOWS ON)
  krr_message(INFO "Configured to build for Windows")
else()
  set(KRR_BUILD_FOR_WINDOWS OFF)
endif()

if(UNIX AND NOT APPLE)
  set(KRR_BUILD_FOR_LINUX ON)
  krr_message(INFO "Configured to build for Linux")
else()
  set(KRR_BUILD_FOR_LINUX OFF)
endif()

# ----------------------------------------------------------
# User options
# ----------------------------------------------------------
include(CMakeDependentOption)

option(KRR_BUILD_TESTS "Build tests" ON)
cmake_dependent_option(KRR_BUILD_COMPTIME_TESTS "Build compile-time tests, this requires new versions of compiler" OFF "KRR_BUILD_TESTS" OFF)
option(KRR_BUILD_FOR_NATIVE "Build with -march=native -mtune=native" OFF)

option(KRR_USE_MOLD "Use mold as the linker" OFF)

option(KRR_ENABLE_LSAN "Enable LSAN (leak sanitizer)" OFF)
option(KRR_ENABLE_ASAN "Enable ASAN (address sanitizer)" OFF)
option(KRR_ENABLE_UBSAN "Enable UBSAN (undefined behavior sanitizer)" OFF)

option(KRR_ENABLE_CLANG_TIDY "Enable clang-tidy for analysis" OFF)

# Optional modules
option(KRR_BUILD_EXAMPLES "Build the examples" OFF)
option(KRR_BUILD_KIRARA_DANCE "Build the KiraraDance application" OFF)

# ----------------------------------------------------------
# Dependencies
# ----------------------------------------------------------

# Using local packages is not recommended
set(CPM_USE_LOCAL_PACKAGES OFF)
include(GetCPM)
include(SetupDeps)

# ----------------------------------------------------------
# Responses for the user options
# ----------------------------------------------------------
if(KRR_BUILD_TESTS)
  enable_testing()
endif()

if(KRR_BUILD_FOR_NATIVE)
  include(EnableNative)
endif()

if(KRR_USE_MOLD)
  include(EnableMoldLinker)
endif()

include(sanitizers)

if(KRR_ENABLE_LSAN)
  krr_message(INFO "LeakSanitizer is enabled")
  add_sanitizer_support(leak)
endif()

if(KRR_ENABLE_ASAN)
  krr_message(WARN "AddressSanitizer is enabled. 
  It intends to detect common issues like out of bounds/use after free/use after
  return/use after scope, etc. It will also utilize LeakSanitizer to check
  memory leak. Sometimes AddressSanitizer is aggressive that the program can fail
  mysteriously, one might consider enabling UBSAN or LSAN only")
  add_sanitizer_support(address)
endif()

if(KRR_ENABLE_UBSAN)
  krr_message(INFO "UndefinedBehaviourSanitizer is enabled")
  add_sanitizer_support(undefined)
endif()

# ----------------------------------------------------------
# Setup projects
# ----------------------------------------------------------
add_subdirectory(kira)

if(KRR_BUILD_KIRARA_DANCE)
  add_subdirectory(kirara-dance)
endif()

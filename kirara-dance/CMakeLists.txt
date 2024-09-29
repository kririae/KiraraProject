project(kirara-dance LANGUAGES C CXX)

include(KRR_AddModule)
include(KRR_Message)

CPMAddPackage(
  NAME glfw
  GITHUB_REPOSITORY glfw/glfw

  GIT_TAG 3.4
  OPTIONS
    "GLFW_BUILD_EXAMPLES OFF"
    "GLFW_BUILD_TESTS OFF"
    "GLFW_BUILD_DOCS OFF"
)

krr_message(INFO "Pulling slang binaries... This might take long")

if(UNIX AND NOT APPLE)
  CPMAddPackage(
    NAME slang
    URL "https://github.com/shader-slang/slang/releases/download/v2024.11/slang-2024.11-linux-x86_64.tar.gz"
    VERSION 2024.11
    DOWNLOAD_ONLY)
elseif(WIN32)
  CPMAddPackage(
    NAME slang
    URL "https://github.com/shader-slang/slang/releases/download/v2024.11/slang-2024.11-windows-x86_64.tar.gz"
    VERSION 2024.11
    DOWNLOAD_ONLY)
else()
  krr_message(ERROR "Unsupported platform: Only Linux/Windows x64 is supported by Kirara Project")
endif()

if(slang_ADDED)
  set(SLANG_ROOT ${slang_SOURCE_DIR})

  add_library(slang::slang SHARED IMPORTED)
  add_library(slang::gfx SHARED IMPORTED)

  if(UNIX AND NOT APPLE)
    set_target_properties(slang::slang PROPERTIES
      IMPORTED_LOCATION "${SLANG_ROOT}/lib/libslang.so"
      INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
    set_target_properties(slang::gfx PROPERTIES
      IMPORTED_LOCATION "${SLANG_ROOT}/lib/libgfx.so"
      INTERFACE_INCLUDE_DIRECTORIES "${SLANG_ROOT}/include")
  elseif(WIN32)
    # TODO(krr)
  endif()
endif()

add_executable(kirara-dance
  Core/GLFW.h
  Core/KIRA.h
  Core/Object.h
  Core/ProgramBuilder.cpp
  Core/ProgramBuilder.h
  Core/ShaderCursor.cpp
  Core/ShaderCursor.h
  Core/SlangContext.cpp
  Core/SlangContext.h
  Core/SlangUtils.h
  Core/Window.cpp
  Core/Window.h

  KiraraDance/Backward.cpp
  KiraraDance/KiraraDance.cpp
)

target_link_libraries(kirara-dance PRIVATE
  Backward::Interface
  glfw
  kira::kira
  slang::gfx
  slang::slang
)

# Link towards current directory.
target_include_directories(kirara-dance PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})

if(false)
  target_precompile_headers(kirara-dance PRIVATE
    [[Core/GLFW.h]]
    [[Core/KIRA.h]]
  )
endif()
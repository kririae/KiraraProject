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

krr_message(INFO "Downloading slang... This might take long")

CPMAddPackage(
  NAME slang
  VERSION 2024.11
  OPTIONS
    "SLANG_ENABLE_SLANGD OFF"
    "SLANG_ENABLE_TESTS OFF"
)

add_executable(kirara-dance src/main.cpp)
target_link_libraries(kirara-dance PRIVATE slang-gfx kira::Core)

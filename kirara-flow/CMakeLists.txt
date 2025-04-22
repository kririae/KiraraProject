project(kirara-flow LANGUAGES C CXX CUDA)

include(KRR_AddModule)
include(KRR_Message)
include(KRR_AddTest)

# ----------------------------------------------------------
# Dependency setup
# ----------------------------------------------------------
CPMAddPackage(
    NAME cutlass
    GITHUB_REPOSITORY NVIDIA/cutlass
    GIT_TAG v3.8.0
    OPTIONS "CUTLASS_ENABLE_HEADERS_ONLY ON")

CPMAddPackage(
    NAME mfem
    GITHUB_REPOSITORY mfem/mfem
    GIT_TAG v4.7)

# ----------------------------------------------------------
# Executable setup
# ----------------------------------------------------------

add_executable(flco src/flco.cpp)
target_link_libraries(flco PRIVATE kira::Core mfem nvidia::cutlass::cutlass)

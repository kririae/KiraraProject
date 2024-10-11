cmake_minimum_required(VERSION 3.27.4)
project(kirara-example LANGUAGES C CXX)

include(FetchContent)

set(KRR_BUILD_TESTS OFF)
FetchContent_Declare(
    kira
    GIT_REPOSITORY https://github.com/kririae/KiraraProject.git
    GIT_TAG krr/graphics)
FetchContent_MakeAvailable(kira)

add_executable(kirara-example main.cpp)
target_link_libraries(kirara-example PRIVATE kira::kira)

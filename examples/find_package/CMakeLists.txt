cmake_minimum_required(VERSION 3.27.4)
project(kirara-example LANGUAGES C CXX)

find_package(kira CONFIG REQUIRED)

add_executable(kirara-example main.cpp)
target_link_libraries(kirara-example PRIVATE kira::kira)

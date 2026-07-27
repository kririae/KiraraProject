add_library(
    flux STATIC
    Core/Object.cpp
    Scene/Context.cpp
    Scene/RenderObject.cpp
    Scene/TXContext.cpp)

target_include_directories(flux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(flux PUBLIC kira::Core)

if(MSVC)
    target_compile_options(flux PRIVATE /W4)
else()
    target_compile_options(flux PRIVATE -Wall -Wextra)
endif()

if(KRR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

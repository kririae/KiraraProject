// see https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/Falcor/Core/GLFW.h
#pragma once

#define GLFW_INCLUDE_NONE

// https://www.glfw.org/docs/3.3/group__native.html
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#error "Platform not supported"
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#undef None
#undef Bool
#undef Status
#undef Always

namespace krd {}

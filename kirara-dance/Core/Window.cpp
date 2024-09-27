#include "Core/Window.h"
#include <kira/Anyhow.h>

#include "Core/GLFW.h"
#include "Core/KIRA.h"

namespace krd {
Window::Window(Desc const &desc) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (glfwInit() == GLFW_FALSE)
        throw kira::Anyhow("Failed to initialize GLFW");

    glfwSetErrorCallback([](int error, char const *description) {
        krd::LogError("glfw error {:d}: {:s}", error, description);
    });

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // No support for other platforms yet, which, is supposed to be easy to add.
    width = desc.width, height = desc.height;
    window = glfwCreateWindow(width, height, desc.title.c_str(), nullptr, nullptr);
    handle = gfx::WindowHandle::FromXWindow(glfwGetX11Display(), glfwGetX11Window(window));

    glfwShowWindow(window);
    glfwFocusWindow(window);
}

Window::~Window() {
    if (window != nullptr)
        glfwDestroyWindow(window);
    glfwTerminate();
}
} // namespace krd

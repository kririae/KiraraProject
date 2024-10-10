#include "Core/Window.h"
#include <kira/Anyhow.h>

#include "Core/GLFW.h"
#include "Core/KIRA.h"

namespace krd {
Window::Window(Desc const &desc) {
    if (glfwInit() == GLFW_FALSE)
        throw kira::Anyhow("Failed to initialize GLFW");

    glfwSetErrorCallback([](int error, char const *description) {
        krd::LogError("glfw error {:d}: {:s}", error, description);
    });

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // No support for other platforms yet, which, is supposed to be easy to add.
    width = desc.width, height = desc.height;
    window = glfwCreateWindow(width, height, desc.title.c_str(), nullptr, nullptr);

#if _WIN32
    handle = gfx::WindowHandle::FromHwnd(glfwGetWin32Window(window));
#elif __linux__
    handle = gfx::WindowHandle::FromXWindow(glfwGetX11Display(), glfwGetX11Window(window));
#else
#error "Platform not supported"
#endif

    glfwShowWindow(window);
    glfwFocusWindow(window);
    glfwSetWindowUserPointer(window, this);

    // Setup callbacks on Framebuffer
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *window, int width, int height) {
        try {
            if (auto *const windowPtr = static_cast<Window *>(glfwGetWindowUserPointer(window));
                windowPtr)
                for (auto *const controller : windowPtr->controllers)
                    controller->onResize(width, height);
        } catch (std::exception const &e) { LogError("{:s}", e.what()); }
    });

    // Setup callbacks on keyboard
    glfwSetKeyCallback(window, [](GLFWwindow *window, int key, int scancode, int action, int mods) {
        try {
            if (auto *const windowPtr = static_cast<Window *>(glfwGetWindowUserPointer(window));
                windowPtr) {
                // Default controller
                if (key == GLFW_KEY_ESCAPE)
                    glfwSetWindowShouldClose(window, true);

                for (auto *const controller : windowPtr->controllers)
                    controller->onKeyboard(key, scancode, action, mods);
            }
        } catch (std::exception const &e) { LogError("{:s}", e.what()); }
    });
}

Window::~Window() {
    if (window != nullptr)
        glfwDestroyWindow(window);
    glfwTerminate();
    LogTrace("Window: destructed");
}

void Window::mainLoop(std::function<void(float)> const &onNewFrame) const {
    while (!glfwWindowShouldClose(window)) {
        // (1)
        glfwPollEvents();

        // (2)
        for (auto *const controller : this->controllers)
            controller->tick(1 / 60.0f);

        // (3)
        onNewFrame(1 / 60.0f);
    }
}
} // namespace krd

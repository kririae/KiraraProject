#pragma once

#include <slang-gfx.h>

#include <string>

struct GLFWwindow;

namespace krd {
class Window {
public:
    struct Desc {
        int width;
        int height;
        std::string title;
    };

public:
    ///
    Window(Desc const &desc);

    ///
    ~Window();

    ///
    [[nodiscard]] int getWidth() const { return width; }

    ///
    [[nodiscard]] int getHeight() const { return height; }

    ///
    [[nodiscard]] gfx::WindowHandle getWindowHandle() { return handle; }

private:
    int width, height;
    GLFWwindow *window = nullptr;
    gfx::WindowHandle handle{};
};
} // namespace krd

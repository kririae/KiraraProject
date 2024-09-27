#pragma once

#include <slang-gfx.h>

#include <string>

#include "Core/Object.h"

struct GLFWwindow;

namespace krd {
class Window : public Object {
public:
    struct Desc {
        int width;
        int height;
        std::string title;
    };

protected:
    Window(Desc const &desc);

public:
    ///
    static Ref<Window> create(Desc const &desc) { return {new Window(desc)}; }

    ///
    virtual ~Window();

    ///
    [[nodiscard]] int getWidth() const { return width; }

    ///
    [[nodiscard]] int getHeight() const { return height; }

    ///
    [[nodiscard]] gfx::WindowHandle getWindowHandle() const { return handle; }

private:
    int width, height;
    GLFWwindow *window = nullptr;
    gfx::WindowHandle handle{};
};
} // namespace krd

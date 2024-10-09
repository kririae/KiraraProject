#pragma once

#include <kira/SmallVector.h>

#include <slang-gfx.h>

#include <string>

#include "Core/Object.h"

struct GLFWwindow;

namespace krd {
class Window final : public Object {
public:
    ///
    struct Desc {
        int width;
        int height;
        std::string title;
    };

    ///
    class Controller {
    public:
        virtual ~Controller() = default;

        /// Some of the controller needs to be ticked, e.g., the \c CameraController.
        virtual void tick(float deltaTime) { (void)(deltaTime); }

        /// \c glfwSetFramebufferSizeCallback
        virtual void onResize(int width, int height) {}

        /// \c glfwSetKeyCallback
        virtual void onKeyboard(int key, int scancode, int action, int mods) {}
    };

protected:
    explicit Window(Desc const &desc);

public:
    ///
    static Ref<Window> create(Desc const &desc) { return {new Window(desc)}; }

    ///
    ~Window() override;

    ///
    [[nodiscard]] int getWidth() const { return width; }

    ///
    [[nodiscard]] int getHeight() const { return height; }

    ///
    [[nodiscard]] gfx::WindowHandle getWindowHandle() const { return handle; }

    ///
    void attachController(Controller *controller) { controllers.push_back(controller); }

    /// Launch the main loop of an application.
    ///
    /// \remark Resource captured by the function should stay valid until the function returns.
    void mainLoop(std::function<void(float)> const &onNewFrame) const;

private:
    int width, height;
    GLFWwindow *window = nullptr;
    gfx::WindowHandle handle{};

    kira::SmallVector<Controller *> controllers{};
};
} // namespace krd

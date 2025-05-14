#pragma once

#include "Core/Math.h"
#include "Core/ProgramBuilder.h"
#include "Core/SlangContext.h"
#include "Core/Window.h"

namespace krd {
class Node;
class Camera;
class SlangGraphicsContext;

///
class SlangGraphicsContextController final : public Window::Controller {
public:
    explicit SlangGraphicsContextController(SlangGraphicsContext *context) : context(context) {}

    ///
    void onResize(int width, int height) override;

private:
    SlangGraphicsContext *const context;
};

///
class SlangGraphicsContext final : public SlangContext {
public:
    struct Desc {
        //
        int swapchainImageCnt = 2;
        //
        bool enableVSync = true;
        // https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#VUID-vkAcquireNextImageKHR-surface-07783
        bool enableGFXFix_07783 = false;
    };

private:
    ///
    SlangGraphicsContext(Desc const &desc, Ref<Window> const &window);

public:
    ///
    static Ref<SlangGraphicsContext> create(Desc const &desc, Ref<Window> const &window) {
        return {new SlangGraphicsContext(desc, window)};
    }

    ~SlangGraphicsContext() override {
        // This is only invoked when *one constructor succeed*. So `gQueue` are guaranteed to be
        // complete.
        //
        // Wait for the queue to finish, i.e., the work submitted by the last frame.
        // This implicitly waits for
        // 1. all the fences to be signaled
        // 2. transient heaps are cleared, because heap usage is issued before the queue submission
        gQueue->waitOnHost();
        LogTrace("SlangGraphicsContext: destructed");
    }

public:
    void onResize(int newWidth, int newHeight);

    /// Set the clear value of the render target.
    ///
    /// The effect will take place in the next frame.
    void setClearValue(float4 const &clearValue) { gClearValue = clearValue; }

    ///
    [[nodiscard]] auto *getController() { return &controller; }

public:
    ///
    void renderFrame(Ref<Node> sceneRoot, Ref<Camera> camera);

    /// Finalize the graphics context.
    ///
    /// \remark This function must be called if \c SlangGraphicsContext is successfully constructed,
    /// i.e., should not be invoked when the construction of the context is failed.
    ///
    /// \remark This function must be called
    /// before the context is destroyed. This function can be called multiple times.
    void synchronize() const;

private:
    void setupFramebufferLayout(); // (1)
    void setupRenderPassLayout();  // (2)
    void setupPipelineState();     // (3)
    void setupSkelPipelineState(); // (3)
    void setupSwapchain();         // (4)
    void setupFramebuffer();       // (5)
    void setupTransientHeap();     // (6)
    void setupFrameFence();        // (7)

protected:
    SlangGraphicsContextController controller;

    Ref<Window>
        window; // (0) don't change the order, in fact SlangGraphicsContext does not own the window
    int width, height;              // (0)
    gfx::WindowHandle windowHandle; // (0)

    gfx::Format pixFormat = gfx::Format::B8G8R8A8_UNORM; // (1)
    ComPtr<gfx::IFramebufferLayout> gFramebufferLayout;  // (1)
    ComPtr<gfx::IRenderPassLayout> gRenderPassLayout;    // (2)

    //
    // TODO(krr): modify the the RenderPass scheme
    //

    ProgramBuilder programBuilder;              // (3)
    Ref<Program> shaderProgram;                 // (3)
    ComPtr<gfx::IPipelineState> gPipelineState; // (3)

    ProgramBuilder skelProgramBuilder;             // (3)
    Ref<Program> skelShaderProgram;                // (3)
    ComPtr<gfx::IPipelineState> skelPipelineState; // (3)

    int swapchainImageCnt;              // (4)
    bool enableVSync;                   // (4)
    ComPtr<gfx::ISwapchain> gSwapchain; // (4)

    kira::SmallVector<ComPtr<gfx::IFramebuffer>> gFrameBuffer;             // (5)
    kira::SmallVector<ComPtr<gfx::ITransientResourceHeap>> gTransientHeap; // (6)

    bool enableGFXFix_07783 = false; // (7)
    ComPtr<gfx::IFence> gFrameFence; // (7)

    uint64_t gFrameIndex = 0;
    float4 gClearValue{20.0f / 255, 19.0f / 255, 20.0f / 255, 1.0f};

    ComPtr<gfx::IBufferResource> gSkeletonBuffer; // (dyn)
};
} // namespace krd

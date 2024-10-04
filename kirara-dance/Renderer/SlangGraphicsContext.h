#pragma once

#include "Core/ProgramBuilder.h"
#include "Core/SlangContext.h"
#include "Core/Window.h"
#include "Renderer/RenderScene.h"

namespace krd {
///
class SlangGraphicsContext : public SlangContext {
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
    SlangGraphicsContext(
        Desc const &desc, Ref<Window> const &window, Ref<RenderScene> const &renderScene
    );

public:
    ///
    static Ref<SlangGraphicsContext>
    create(Desc const &desc, Ref<Window> const &window, Ref<RenderScene> const &renderScene) {
        return {new SlangGraphicsContext(desc, window, renderScene)};
    }

    ~SlangGraphicsContext() override {
        // Wait for the queue to finish, i.e., the work submitted by the last frame.
        // This implicitly waits for
        // 1. all the fences to be signaled
        // 2. transient heaps are cleared, because heap usage is issued before the queue submission
        if (gQueue)
            gQueue->waitOnHost();
    }

public:
    void setSwapchainImageCnt(int cnt);
    void resize(std::shared_ptr<Window> const &window);

    /// Get the render scene that this graphics context is associated with.
    auto getRenderScene() const {
        KRD_ASSERT(renderScene, "SlangGraphicsContext: RenderScene is not set");
        return renderScene;
    }

public:
    ///
    void renderFrame();

private:
    void setupFramebufferLayout(); // (1)
    void setupPipelineState();     // (2)
    void setupSwapchain();         // (3)
    void setupFramebuffer();       // (4)
    void setupTransientHeap();     // (5)
    void setupRenderPassLayout();  // (6)
    void setupFrameFence();        // (7)

protected:
    RenderScene *renderScene;       // (0)
    int width, height;              // (0)
    gfx::WindowHandle windowHandle; // (0)

    gfx::Format pixFormat = gfx::Format::B8G8R8A8_UNORM; // (1)
    ComPtr<gfx::IFramebufferLayout> gFramebufferLayout;  // (1)

    ProgramBuilder programBuilder;              // (2)
    ComPtr<gfx::IPipelineState> gPipelineState; // (2)

    int swapchainImageCnt;              // (3)
    bool enableVSync;                   // (3)
    ComPtr<gfx::ISwapchain> gSwapchain; // (3)

    kira::SmallVector<ComPtr<gfx::IFramebuffer>> gFrameBuffer;             // (4)
    kira::SmallVector<ComPtr<gfx::ITransientResourceHeap>> gTransientHeap; // (5)

    ComPtr<gfx::IRenderPassLayout> gRenderPassLayout; // (6)

    bool enableGFXFix_07783 = false; // (7)
    ComPtr<gfx::IFence> gFrameFence; // (7)

    uint64_t gFrameIndex = 0;
};
} // namespace krd

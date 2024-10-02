#pragma once

#include "Core/SlangContext.h"
#include "Core/Window.h"

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
    SlangGraphicsContext(Desc const &desc, Ref<Window> const &window);

public:
    ///
    static Ref<SlangGraphicsContext> create(Desc const &desc, Ref<Window> const &window) {
        return {new SlangGraphicsContext(desc, window)};
    }

    ///
    ~SlangGraphicsContext() = default;

public:
    void setSwapchainImageCnt(int cnt);
    void resize(std::shared_ptr<Window> const &window);

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
    int width, height;              // (0)
    gfx::WindowHandle windowHandle; // (0)

    gfx::Format pixFormat = gfx::Format::B8G8R8A8_UNORM; // (1)
    ComPtr<gfx::IFramebufferLayout> gFramebufferLayout;  // (1)

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

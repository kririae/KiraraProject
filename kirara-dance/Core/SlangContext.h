#pragma once

#include <array>

#include "Core/Window.h"

using Slang::ComPtr;

namespace krd {
class SlangContext {
public:
    /// Setup necessary GFX global objects above.
    SlangContext();

    /// Slang's workaround makes it not necessary to define a destructor like OptiX: any destruction
    /// order is allowed by double reference counting.
    ///
    /// \see https://github.com/shader-slang/slang/pull/1788
    virtual ~SlangContext() = default;

public:
    /// \see gDevice->createGraphicsPipelineState
    ComPtr<gfx::IPipelineState> createGraphicsPipelineState();

    ///
    void mainLoop();

    ///
    void renderFrame(int frameIndex);

protected:
    ///
    std::unique_ptr<Window> window;

    ComPtr<gfx::IDevice> gDevice;
    ComPtr<gfx::ICommandQueue> gQueue;

    ComPtr<gfx::ISwapchain> gSwapchain;
    ComPtr<gfx::IFramebufferLayout> gFramebufferLayout;
    ComPtr<gfx::IRenderPassLayout> gRenderPassLayout;
    ComPtr<gfx::IPipelineState> gPipelineState;
    ComPtr<gfx::IBufferResource> gVertexBuffer;

    std::array<ComPtr<gfx::ITransientResourceHeap>, 2> gTransientHeapBuffer;
    std::array<ComPtr<gfx::IFramebuffer>, 2> gFramebuffer;
    ComPtr<gfx::IFence> gFrameFence;

    uint64_t gFrameIndex = 0;

    ComPtr<gfx::IRenderPassLayout> gRenderPass;
};

///
class SlangGraphicsContext : public SlangContext {
public:
    ///
    SlangGraphicsContext();

    ///
    ~SlangGraphicsContext();
};
} // namespace krd

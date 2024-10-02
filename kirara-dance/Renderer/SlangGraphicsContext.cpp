#include "Renderer/SlangGraphicsContext.h"

#include "Core/ProgramBuilder.h"
#include "Core/ShaderCursor.h"
#include "Core/SlangUtils.h"

namespace krd {
namespace {
struct Vertex {
    float position[3];
    float color[3];
};

static int const VertexCount = 3;
static Vertex const VertexData[VertexCount] = {
    {{0, 0, 0.5}, {1, 0, 0}},
    {{0, 1, 0.5}, {0, 0, 1}},
    {{1, 0, 0.5}, {0, 1, 0}},
};
} // namespace

SlangGraphicsContext::SlangGraphicsContext(Desc const &desc, Ref<Window> const &window)
    : SlangContext() {
    width = window->getWidth(), height = window->getHeight(); // (0)
    windowHandle = window->getWindowHandle();                 // (0)
    swapchainImageCnt = desc.swapchainImageCnt;               // (3)
    enableVSync = desc.enableVSync;                           // (3)
    enableGFXFix_07783 = desc.enableGFXFix_07783;             // (7)

    setupFramebufferLayout(); // (1)
    setupPipelineState();     // (2)
    setupSwapchain();         // (3)
    setupFramebuffer();       // (4)
    setupTransientHeap();     // (5)
    setupRenderPassLayout();  // (6)
    setupFrameFence();        // (7)
}

void SlangGraphicsContext::renderFrame() {
    if (enableGFXFix_07783) {
        // Because of the bug in GFX, we need to wait for the fence to be signaled.
        gfx::IFence *fences[] = {gFrameFence.get()};
        uint64_t fenceValues[] = {this->gFrameIndex};
        gDevice->waitForFences(1, fences, fenceValues, true, UINT64_MAX);
    }

    auto const swapchainImageIdx = gSwapchain->acquireNextImage();
    if (swapchainImageIdx == -1)
        throw kira::Anyhow("Swapchain is invalid or out-of-date");

    auto transientHeap = gTransientHeap[swapchainImageIdx];

    // Make sure the transient heap is ready for the next frame.
    transientHeap->synchronizeAndReset();

    auto commandBuffer = gTransientHeap[swapchainImageIdx]->createCommandBuffer();
    auto *renderEncoder =
        commandBuffer->encodeRenderCommands(gRenderPassLayout, gFrameBuffer[swapchainImageIdx]);

    // Set the viewport and scissor rect.
    gfx::Viewport viewport{};
    viewport.maxZ = 1.0f;
    viewport.extentX = static_cast<float>(this->width);
    viewport.extentY = static_cast<float>(this->height);
    renderEncoder->setViewportAndScissor(viewport);

    //! Bind the pipeline state to shader objects.
    {
        auto *rootObject = renderEncoder->bindPipeline(gPipelineState);
        gfx::ShaderCursor rootCursor{rootObject};

        std::array<float, 16> identityMatrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        slangCheck(rootCursor["Uniforms"]["modelViewProjection"].setData(
            identityMatrix.data(), sizeof(float) * 4 * 4
        ));
    }

    //! Set the vertex buffer and render the triangles.
    {
        // renderEncoder->setVertexBuffer(0, gVertexBuffer);
        // renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::TriangleList);
    }

    // Clean up the rendering of this frame and dispatch the rendering commands.
    renderEncoder->endEncoding();
    commandBuffer->close();
    gQueue->executeCommandBuffer(commandBuffer, gFrameFence.get(), this->gFrameIndex + 1);
    gSwapchain->present();
    transientHeap->finish();
    ++this->gFrameIndex;
}

void SlangGraphicsContext::setupFramebufferLayout() {
    gfx::IFramebufferLayout::TargetLayout renderTargets{.format = pixFormat, .sampleCount = 1};
    gfx::IFramebufferLayout::TargetLayout depthStencil{
        .format = gfx::Format::D32_FLOAT, .sampleCount = 1
    };
    gfx::IFramebufferLayout::Desc framebufferLayoutDesc{
        .renderTargetCount = 1, .renderTargets = &renderTargets, .depthStencil = &depthStencil
    };
    slangCheck(
        gDevice->createFramebufferLayout(framebufferLayoutDesc, gFramebufferLayout.writeRef())
    );
}

void SlangGraphicsContext::setupPipelineState() {
    auto program = //
        ProgramBuilder{}
            .addSlangModuleFromPath(
                "/home/krr/Projects/KiraraProject/kirara-dance/Renderer/shaders.slang"
            )
            .addEntryPoint("vertexMain")
            .addEntryPoint("fragmentMain")
            // .addSlangModuleFromPath(
            //     "/home/krr/Projects/KiraraProject/kirara-dance/KiraraDance/null.slang"
            // )
            .link(gDevice);

    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, position)},
        {"COLOR", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, color)},
    };
    ComPtr<gfx::IInputLayout> inputLayout;
    slangCheck(gDevice->createInputLayout(sizeof(Vertex), inputElements, 2, inputLayout.writeRef())
    );

    gfx::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.program = program->getShaderProgram();
    pipelineDesc.framebufferLayout = gFramebufferLayout;
    slangCheck(gDevice->createGraphicsPipelineState(pipelineDesc, gPipelineState.writeRef()));
}

void SlangGraphicsContext::setupSwapchain() {
    gfx::ISwapchain::Desc swapchainDesc{
        .format = pixFormat,
        .width = this->width,
        .height = this->height,
        .imageCount = swapchainImageCnt,
        .queue = gQueue.get(),
        .enableVSync = enableVSync,
    };
    slangCheck(gDevice->createSwapchain(swapchainDesc, windowHandle, gSwapchain.writeRef()));
}

void SlangGraphicsContext::setupFramebuffer() {
    this->gFrameBuffer.clear();
    for (int frameBufferId = 0; frameBufferId < swapchainImageCnt; ++frameBufferId) {
        ComPtr<gfx::ITextureResource> colorBuffer;
        slangCheck(gSwapchain->getImage(frameBufferId, colorBuffer.writeRef()));

        gfx::ITextureResource::Desc depthBufferDesc;
        depthBufferDesc.type = gfx::IResource::Type::Texture2D;
        depthBufferDesc.size.width = this->width;
        depthBufferDesc.size.height = this->height;
        depthBufferDesc.size.depth = 1;
        depthBufferDesc.format = gfx::Format::D32_FLOAT;
        depthBufferDesc.defaultState = gfx::ResourceState::DepthWrite;
        depthBufferDesc.allowedStates = gfx::ResourceStateSet(gfx::ResourceState::DepthWrite);
        gfx::ClearValue depthClearValue = {};
        depthBufferDesc.optimalClearValue = &depthClearValue;
        ComPtr<gfx::ITextureResource> depthBufferResource =
            gDevice->createTextureResource(depthBufferDesc, nullptr);

        gfx::IResourceView::Desc colorBufferViewDesc{};
        colorBufferViewDesc.format = pixFormat;
        colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
        ComPtr<gfx::IResourceView> rtv =
            gDevice->createTextureView(colorBuffer.get(), colorBufferViewDesc);

        gfx::IResourceView::Desc depthBufferViewDesc{};
        depthBufferViewDesc.format = gfx::Format::D32_FLOAT;
        depthBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        depthBufferViewDesc.type = gfx::IResourceView::Type::DepthStencil;
        ComPtr<gfx::IResourceView> dsv =
            gDevice->createTextureView(depthBufferResource.get(), depthBufferViewDesc);

        gfx::IFramebuffer::Desc framebufferDesc{};
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = dsv.get();
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = gFramebufferLayout;

        ComPtr<gfx::IFramebuffer> framebuffer;
        slangCheck(gDevice->createFramebuffer(framebufferDesc, framebuffer.writeRef()));

        this->gFrameBuffer.push_back(framebuffer);
    }

    KRD_ASSERT(this->gFrameBuffer.size() == swapchainImageCnt);
}

void SlangGraphicsContext::setupTransientHeap() {
    gTransientHeap.clear();
    for (int frameBufferId = 0; frameBufferId < swapchainImageCnt; ++frameBufferId) {
        // TODO(krr): Might require a different constantBufferSize?
        gfx::ITransientResourceHeap::Desc transientHeapDesc{.constantBufferSize = 4096 * 1024};

        ComPtr<gfx::ITransientResourceHeap> transientHeap;
        slangCheck(gDevice->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef())
        );
        gTransientHeap.push_back(transientHeap);
    }
}

void SlangGraphicsContext::setupRenderPassLayout() {
    gfx::IRenderPassLayout::Desc renderPassDesc{};
    renderPassDesc.framebufferLayout = gFramebufferLayout;
    renderPassDesc.renderTargetCount = 1;
    gfx::IRenderPassLayout::TargetAccessDesc renderTargetAccess{};
    gfx::IRenderPassLayout::TargetAccessDesc depthStencilAccess{};
    renderTargetAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;
    renderTargetAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store;
    renderTargetAccess.initialState = gfx::ResourceState::Undefined;
    renderTargetAccess.finalState = gfx::ResourceState::Present;
    depthStencilAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;
    depthStencilAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store;
    depthStencilAccess.initialState = gfx::ResourceState::DepthWrite;
    depthStencilAccess.finalState = gfx::ResourceState::DepthWrite;
    renderPassDesc.renderTargetAccess = &renderTargetAccess;
    renderPassDesc.depthStencilAccess = &depthStencilAccess;
    slangCheck(gDevice->createRenderPassLayout(renderPassDesc, gRenderPassLayout.writeRef()));
}

void SlangGraphicsContext::setupFrameFence() {
    gfx::IFence::Desc fenceDesc{};
    slangCheck(gDevice->createFence(fenceDesc, gFrameFence.writeRef()));
}
} // namespace krd

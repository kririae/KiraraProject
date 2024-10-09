#include "FLux/SlangGraphicsContext.h"

#include "Core/ProgramBuilder.h"
#include "Core/ShaderCursor.h"
#include "Core/SlangUtils.h"
#include "FLux/RenderCamera.h"
#include "FLux/RenderTriangleMesh.h"

namespace krd {
void SlangGraphicsContextController::onResize(int width, int height) {
    context->onResize(width, height);
}

SlangGraphicsContext::SlangGraphicsContext(
    Desc const &desc, Ref<Window> const &window, Ref<FLuxScene> const &renderScene
)
    : controller(this) {
    // TODO(krr): is this robust enough?.. IDK but anyway
    //
    // This limits the `RenderScene` to operate only on a single SlangGraphicsContext, while putting
    // the `SlangGraphicsContext` is a passive place. I cannot get any reference on how to design
    // this for now, but this really simplifies coding.
    //
    // Can we just assume that the `RenderScene` and `SlangGraphicsContext` operates on a single
    // thread?

    this->renderScene = renderScene;
    // Take the reference to the window to ensure that, window is destructed after the swapchain.
    this->window = window;

    width = window->getWidth(), height = window->getHeight(); // (0)
    windowHandle = window->getWindowHandle();                 // (0)
    programBuilder = renderScene->createProgramBuilder();     // (2)
    swapchainImageCnt = desc.swapchainImageCnt;               // (3)
    enableVSync = desc.enableVSync;                           // (3)
    enableGFXFix_07783 = desc.enableGFXFix_07783;             // (7)

    setupFramebufferLayout(); // (1)
    setupRenderPassLayout();  // (2)
    setupPipelineState();     // (3)
    setupSwapchain();         // (4)
    setupFramebuffer();       // (5)
    setupTransientHeap();     // (6)
    setupFrameFence();        // (7)

    // Only bind it to renderScene when all setup processes are succeed.
    // This means that we cannot depend on getGraphicsContext() in initialization.
    this->renderScene->bindGraphicsContext(this);
}

void SlangGraphicsContext::setSwapchainImageCnt(int cnt) { throw kira::Anyhow("not implemented"); }

void SlangGraphicsContext::onResize(int newWidth, int newHeight) {
    // Wait until the rendering is done.
    gQueue->waitOnHost();

    LogInfo("SlangGraphicsContext: resizing to width={}, height={}...", newWidth, newHeight);
    this->width = newWidth, this->height = newHeight;

    gFrameBuffer.clear();   // (4)
    gTransientHeap.clear(); // (5)

    // We don't have to re-create the swapchain.
    gSwapchain->resize(newWidth, newHeight);

    setupFramebuffer();   // (4)
    setupTransientHeap(); // (5)
    LogInfo("SlangGraphicsContext: resized to width={}, height={}", newWidth, newHeight);
}

void SlangGraphicsContext::renderFrame() {
    renderScene->pull();

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
    auto *rootObject = renderEncoder->bindPipeline(gPipelineState);
    gfx::ShaderCursor rootCursor{rootObject};

    auto const camera = getRenderScene()->getActiveCamera();
    krd::float4x4 viewProjection = krd::mul(
        camera->getProjectionMatrix(static_cast<float>(width) / static_cast<float>(height)),
        camera->getViewMatrix()
    );

    auto deviceInfo = gDevice->getDeviceInfo();
    krd::float4x4 correctionMatrix;
    std::memcpy(&correctionMatrix, deviceInfo.identityProjectionMatrix, sizeof(correctionMatrix));

    viewProjection = krd::mul(correctionMatrix, viewProjection);

    // https://github.com/shader-slang/slang/blob/master/docs/user-guide/a1-01-matrix-layout.md
    // "Default matrix layout in memory for Slang is row-major"
    // "GLM is column-major"
    viewProjection = krd::transpose(viewProjection);
    slangCheck(rootCursor["Uniforms"]["modelViewProjection"].setData(
        &viewProjection, sizeof(float) * 4 * 4
    ));

    //! Set the vertex buffer and render the triangles.
    for (auto const &rObj : getRenderScene()->getRenderObjectOfType<RenderTriangleMesh>()) {
        auto const resource = rObj->getResource();

        renderEncoder->setVertexBuffer(0, resource->vertexBuffer);
        renderEncoder->setIndexBuffer(resource->indexBuffer, gfx::Format::R32_UINT);
        renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::TriangleList);

        renderEncoder->drawIndexed(rObj->getNumIndices(), 0);
    }

    // Clean up the rendering of this frame and dispatch the rendering commands.
    renderEncoder->endEncoding();
    commandBuffer->close();
    if (enableGFXFix_07783)
        gQueue->executeCommandBuffer(commandBuffer, gFrameFence.get(), this->gFrameIndex + 1);
    else
        gQueue->executeCommandBuffer(commandBuffer, nullptr, 0);
    gSwapchain->present();
    transientHeap->finish();
    ++this->gFrameIndex;
}

void SlangGraphicsContext::finalize() const { gQueue->waitOnHost(); }

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

void SlangGraphicsContext::setupPipelineState() {
    auto const program = this->programBuilder.link(gDevice);

    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, position)},
        {"NORMAL", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, normal)},
    };

    ComPtr<gfx::IInputLayout> inputLayout;
    slangCheck(gDevice->createInputLayout(sizeof(Vertex), inputElements, 2, inputLayout.writeRef())
    );

    gfx::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.program = program->getShaderProgram();
    pipelineDesc.framebufferLayout = gFramebufferLayout;
    pipelineDesc.primitiveType = gfx::PrimitiveType::Triangle;
    pipelineDesc.depthStencil.depthFunc = gfx::ComparisonFunc::LessEqual;
    pipelineDesc.depthStencil.depthTestEnable = true;

    // Setup backspace culling
    pipelineDesc.rasterizer.cullMode = gfx::CullMode::Back;
    pipelineDesc.rasterizer.frontFace = gfx::FrontFaceMode::CounterClockwise;
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
        gfx::ITransientResourceHeap::Desc transientHeapDesc{
            .constantBufferSize = static_cast<gfx::Size>(4096) * 1024
        };

        ComPtr<gfx::ITransientResourceHeap> transientHeap;
        slangCheck(gDevice->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef())
        );
        gTransientHeap.push_back(transientHeap);
    }
}

void SlangGraphicsContext::setupFrameFence() {
    gfx::IFence::Desc fenceDesc{};
    slangCheck(gDevice->createFence(fenceDesc, gFrameFence.writeRef()));
}
} // namespace krd
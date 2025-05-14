#include "SlangGraphicsContext.h"

#include "Core/ProgramBuilder.h"
#include "Core/ShaderCursor.h"
#include "Core/SlangUtils.h"
#include "FacadeRender/Visitors/ExtractDebugSkeleton.h"
#include "FacadeRender/Visitors/ExtractDrawCommand.h"
#include "Scene/Camera.h"
#include "Scene/SceneRoot.h"
#include "TriMeshResource.h"

namespace krd {
void SlangGraphicsContextController::onResize(int width, int height) {
    context->onResize(width, height);
}

SlangGraphicsContext::SlangGraphicsContext(Desc const &desc, Ref<Window> const &window)
    : controller(this) {
    // TODO(krr): is this robust enough?.. IDK but anyway
    //
    // This limits the `InstantScene` to operate only on a single SlangGraphicsContext, while
    // putting the `SlangGraphicsContext` is a passive place. I cannot get any reference on how to
    // design this for now, but this really simplifies coding.
    //
    // Can we just assume that the `InstantScene` and `SlangGraphicsContext` operates on a single
    // thread?

    // Take the reference to the window to ensure that, window is destructed after the swapchain.
    this->window = window;

#if 1
    ProgramBuilder programBuilder;
    std::filesystem::path const shadersPath = R"(FacadeRender/VFMain.slang)";
    programBuilder.addSlangModuleFromPath(shadersPath)
        .addEntryPoint("vertexMain")
        .addEntryPoint("fragmentMain");

    ProgramBuilder skelProgramBuilder;
    std::filesystem::path const skelShadersPath = R"(FacadeRender/SKMain.slang)";
    skelProgramBuilder.addSlangModuleFromPath(skelShadersPath)
        .addEntryPoint("vertexMain")
        .addEntryPoint("fragmentMain");
#endif

    width = window->getWidth(), height = window->getHeight(); // (0)
    windowHandle = window->getWindowHandle();                 // (0)
    this->programBuilder = programBuilder;                    // (3)
    this->skelProgramBuilder = skelProgramBuilder;            // (3)
    swapchainImageCnt = desc.swapchainImageCnt;               // (4)
    enableVSync = desc.enableVSync;                           // (4)
    enableGFXFix_07783 = desc.enableGFXFix_07783;             // (7)

    setupFramebufferLayout(); // (1)
    setupRenderPassLayout();  // (2)

    setupPipelineState();     // (3)
    setupSkelPipelineState(); // (3)

    setupSwapchain();     // (4)
    setupFramebuffer();   // (5)
    setupTransientHeap(); // (6)
    setupFrameFence();    // (7)
}

void SlangGraphicsContext::onResize(int newWidth, int newHeight) {
    // Wait until the rendering is done.
    gQueue->waitOnHost();

    LogTrace("SlangGraphicsContext: resizing to width={}, height={}...", newWidth, newHeight);
    this->width = newWidth, this->height = newHeight;

    gFrameBuffer.clear();   // (5)
    gTransientHeap.clear(); // (6)

    // We don't have to re-create the swapchain.
    gSwapchain->resize(newWidth, newHeight);

    setupFramebuffer();   // (5)
    setupTransientHeap(); // (6)
    LogTrace("SlangGraphicsContext: resized to width={}, height={}", newWidth, newHeight);
}

void SlangGraphicsContext::renderFrame(SceneRoot *sceneRoot, Camera *camera) {
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
    {
        gfx::Viewport viewport{};
        viewport.maxZ = 1.0f;
        viewport.extentX = static_cast<float>(this->width);
        viewport.extentY = static_cast<float>(this->height);
        renderEncoder->setViewportAndScissor(viewport);
    }

    // Perform a clear operation on the color buffer.
    {
        ComPtr<gfx::ITextureResource> colorBuffer;
        slangCheck(gSwapchain->getImage(swapchainImageIdx, colorBuffer.writeRef()));

        gfx::IResourceView::Desc colorBufferViewDesc{};
        colorBufferViewDesc.format = pixFormat;
        colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
        ComPtr<gfx::IResourceView> rtv =
            gDevice->createTextureView(colorBuffer.get(), colorBufferViewDesc);

        gfx::ClearValue clearValue{};
        clearValue.color = {gClearValue.x, gClearValue.y, gClearValue.z, gClearValue.w};

        // TODO(krr): not yet supported on Metal
#if !defined(__APPLE__)
        renderEncoder->clearResourceView(rtv, &clearValue, gfx::ClearResourceViewFlags::None);
#endif
    }

    float4x4 viewProjection =
        mul(camera->getProjectionMatrix(static_cast<float>(width) / static_cast<float>(height)),
            camera->getViewMatrix());

    auto deviceInfo = gDevice->getDeviceInfo();
    float4x4 correctionMatrix;
    std::memcpy(&correctionMatrix, deviceInfo.identityProjectionMatrix, sizeof(correctionMatrix));

    viewProjection = mul(correctionMatrix, viewProjection);

    // https://github.com/shader-slang/slang/blob/master/docs/user-guide/a1-01-matrix-layout.md
    // "Default matrix layout in memory for Slang is row-major"
    // "GLM is column-major"
    viewProjection = transpose(viewProjection);

#if 1
    [&] {
        ExtractDebugSkeleton::Desc desc{.startingDepth = 2};
        ExtractDebugSkeleton eDbgSkeleton(desc);
        sceneRoot->accept(eDbgSkeleton);

        if (eDbgSkeleton.empty())
            return;

        ComPtr<gfx::IBufferResource> skeletonBuffer;

        gfx::IBufferResource::Desc skeletonBufferDesc;
        skeletonBufferDesc.type = gfx::IResource::Type::Buffer;
        skeletonBufferDesc.sizeInBytes = eDbgSkeleton.size() * sizeof(SkelVertex) * 2;
        skeletonBufferDesc.defaultState = gfx::ResourceState::VertexBuffer;
        gSkeletonBuffer.setNull();
        getDevice()->createBufferResource(
            skeletonBufferDesc, eDbgSkeleton.data(), gSkeletonBuffer.writeRef()
        );

        auto *slangReflection = skelShaderProgram->getReflection();
        auto *perView = slangReflection->findTypeByName("PerView");

        auto viewShaderObject = gDevice->createShaderObject(perView);
        {
            gfx::ShaderCursor cursor(viewShaderObject);
            slangCheck(cursor["viewProjection"].setData(&viewProjection, sizeof(float) * 4 * 4));
        }

        auto *rootObject = renderEncoder->bindPipeline(skelPipelineState);
        gfx::ShaderCursor rootCursor{rootObject};
        slangCheck(rootCursor["gViewParams"].setObject(viewShaderObject));

        renderEncoder->setVertexBuffer(0, gSkeletonBuffer);
        renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::LineList);
        renderEncoder->draw(static_cast<gfx::GfxCount>(eDbgSkeleton.size() * 2), 0);
    }();
#endif

#if 1
    {
        ///
        /// Create the shader object for the view.
        ///
        auto *slangReflection = shaderProgram->getReflection();
        auto *perView = slangReflection->findTypeByName("PerView");
        auto *perModel = slangReflection->findTypeByName("PerModel");

        auto viewShaderObject = gDevice->createShaderObject(perView);
        {
            gfx::ShaderCursor cursor(viewShaderObject);
            slangCheck(cursor["viewProjection"].setData(&viewProjection, sizeof(float) * 4 * 4));
        }

        ///
        /// Issue the draw command for the geometry.
        ///
        ExtractDrawCommand eDrawCmd{
            [&](TriMeshResource const *triMeshResource, float4x4 modelMatrix) -> void {
            // Implement the callback
            auto const deviceData = triMeshResource->getDeviceData();

            std::array<float, 16> modelMatrixPtr;
            std::array<float, 16> iModelMatrixPtr;

            auto iModelMatrix = inverse(modelMatrix);
            modelMatrix = transpose(modelMatrix);
            iModelMatrix = transpose(iModelMatrix);
            std::memcpy(modelMatrixPtr.data(), &modelMatrix, sizeof(modelMatrixPtr));
            std::memcpy(iModelMatrixPtr.data(), &iModelMatrix, sizeof(iModelMatrixPtr));

            auto modelShaderObject = gDevice->createShaderObject(perModel);
            gfx::ShaderCursor cursor(modelShaderObject);
            slangCheck(cursor["modelMatrix"].setData(modelMatrixPtr.data(), sizeof(float) * 4 * 4));
            slangCheck(cursor["inverseTransposedModelMatrix"].setData(
                iModelMatrixPtr.data(), sizeof(float) * 4 * 4
            ));

            // Bind the pipeline state to shader objects.
            auto *rootObject = renderEncoder->bindPipeline(gPipelineState);
            gfx::ShaderCursor rootCursor{rootObject};

            slangCheck(rootCursor["gViewParams"].setObject(viewShaderObject));
            slangCheck(rootCursor["gModelParams"].setObject(modelShaderObject));

            renderEncoder->setVertexBuffer(0, deviceData->vertexBuffer);
            renderEncoder->setIndexBuffer(deviceData->indexBuffer, gfx::Format::R32_UINT);
            renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::TriangleList);

            if (triMeshResource->getNumIndices() == 0 ||
                triMeshResource->getNumIndices() > std::numeric_limits<gfx::GfxCount>::max())
                throw kira::Anyhow(
                    "SlangGraphicsContext: invalid number of indices {:d}",
                    triMeshResource->getNumIndices()
                );
            renderEncoder->drawIndexed(
                static_cast<gfx::GfxCount>(triMeshResource->getNumIndices()), 0
            );
        }
        };
        sceneRoot->accept(eDrawCmd);
    }
#endif

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

void SlangGraphicsContext::synchronize() const { gQueue->waitOnHost(); }

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
    shaderProgram = this->programBuilder.link(this);

    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, position)},
        {"NORMAL", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, normal)},
    };

    ComPtr<gfx::IInputLayout> inputLayout;
    slangCheck(gDevice->createInputLayout(sizeof(Vertex), inputElements, 2, inputLayout.writeRef())
    );

    gfx::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.program = shaderProgram->getShaderProgram();
    pipelineDesc.framebufferLayout = gFramebufferLayout;
    pipelineDesc.primitiveType = gfx::PrimitiveType::Triangle;
    pipelineDesc.depthStencil.depthFunc = gfx::ComparisonFunc::LessEqual;
    pipelineDesc.depthStencil.depthTestEnable = true;

    // Setup backspace culling
    pipelineDesc.rasterizer.cullMode = gfx::CullMode::Back;
    pipelineDesc.rasterizer.frontFace = gfx::FrontFaceMode::CounterClockwise;
    slangCheck(gDevice->createGraphicsPipelineState(pipelineDesc, gPipelineState.writeRef()));
}

void SlangGraphicsContext::setupSkelPipelineState() {
    skelShaderProgram = this->skelProgramBuilder.link(this);

    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT, offsetof(SkelVertex, position)},
    };

    ComPtr<gfx::IInputLayout> inputLayout;
    slangCheck(
        gDevice->createInputLayout(sizeof(SkelVertex), inputElements, 1, inputLayout.writeRef())
    );

    gfx::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.program = skelShaderProgram->getShaderProgram();
    pipelineDesc.framebufferLayout = gFramebufferLayout;
    pipelineDesc.primitiveType = gfx::PrimitiveType::Line;
    pipelineDesc.depthStencil.depthFunc = gfx::ComparisonFunc::LessEqual;
    pipelineDesc.depthStencil.depthTestEnable = true;
    pipelineDesc.rasterizer.antialiasedLineEnable = true;

    slangCheck(gDevice->createGraphicsPipelineState(pipelineDesc, skelPipelineState.writeRef()));
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
        gfx::ClearValue depthClearValue{};
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

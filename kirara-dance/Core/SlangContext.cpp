#include "Core/SlangContext.h"
#include <kira/SmallVector.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Core/ShaderCursor.h"
#include "Core/SlangUtils.h"

using Slang::ComPtr;

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

ComPtr<gfx::IPipelineState> SlangContext::createGraphicsPipelineState() {
    ComPtr<slang::ISession> slangSession = gDevice->getSlangSession();

    // Load the Slang module from source. This is to be extended to support loading from a directory
    // later.
    ComPtr<slang::IBlob> diagnostics;
    slang::IModule *module = slangSession->loadModule(
        "/home/krr/Projects/KiraraProject/kirara-dance/Shader/shaders.slang", diagnostics.writeRef()
    );
    slangDiagnostic(diagnostics);
    if (!module)
        throw kira::Anyhow("Failed to load the Slang module");

    // Find the entry points for the vertex and fragment shaders.
    ComPtr<slang::IEntryPoint> vertexEntryPoint;
    slangCheck(module->findEntryPointByName("vertexMain", vertexEntryPoint.writeRef()));
    ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    slangCheck(module->findEntryPointByName("fragmentMain", fragmentEntryPoint.writeRef()));

    kira::SmallVector<slang::IComponentType *> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(vertexEntryPoint);
    componentTypes.push_back(fragmentEntryPoint);

    ComPtr<slang::IComponentType> linkedProgram;
    SlangResult result = slangSession->createCompositeComponentType(
        componentTypes.data(), static_cast<SlangInt>(componentTypes.size()),
        linkedProgram.writeRef(), diagnostics.writeRef()
    );
    slangDiagnostic(diagnostics);
    slangCheck(result);

    // Turns the platform-independent Slang program into a GFX shader program.
    ComPtr<gfx::IShaderProgram> program;
    gfx::IShaderProgram::Desc shaderDesc{
        .slangGlobalScope = linkedProgram.get(),
    };
    slangCheck(gDevice->createProgram(shaderDesc, program.writeRef()));

    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, position)},
        {"COLOR", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, color)},
    };
    auto inputLayout = gDevice->createInputLayout(sizeof(Vertex), inputElements, 2);
    if (!inputLayout)
        throw kira::Anyhow("Failed to create the input layout");

    gfx::IBufferResource::Desc bufferDesc{};
    bufferDesc.sizeInBytes = VertexCount * sizeof(Vertex);
    bufferDesc.defaultState = gfx::ResourceState::VertexBuffer;
    slangCheck(gDevice->createBufferResource(bufferDesc, VertexData, gVertexBuffer.writeRef()));

    ComPtr<gfx::IPipelineState> pipelineState;
    gfx::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.program = program;
    pipelineDesc.framebufferLayout = gFramebufferLayout;
    slangCheck(gDevice->createGraphicsPipelineState(pipelineDesc, pipelineState.writeRef()));

    return pipelineState;
}

SlangContext::SlangContext() {
    auto windowDesc = Window::Desc{.width = 800, .height = 600, .title = "SlangContext"};
    window = std::make_unique<Window>(windowDesc);

    gfx::gfxEnableDebugLayer();
    slangCheck(gfx::gfxSetDebugCallback(&krd::gfxDebugCallback));

    gfx::IDevice::Desc deviceDesc{.deviceType = gfx::DeviceType::Default};
    slangCheck(gfx::gfxCreateDevice(&deviceDesc, gDevice.writeRef()));

    gfx::ICommandQueue::Desc queueDesc{.type = gfx::ICommandQueue::QueueType::Graphics};
    slangCheck(gDevice->createCommandQueue(queueDesc, gQueue.writeRef()));

    gfx::IFramebufferLayout::TargetLayout renderTargetLayout{
        .format = gfx::Format::B8G8R8A8_UNORM, .sampleCount = 1
    };
    gfx::IFramebufferLayout::TargetLayout depthLayout{
        .format = gfx::Format::D32_FLOAT, .sampleCount = 1
    };
    gfx::IFramebufferLayout::Desc framebufferLayoutDesc{
        .renderTargetCount = 1, .renderTargets = &renderTargetLayout, .depthStencil = &depthLayout
    };
    gDevice->createFramebufferLayout(framebufferLayoutDesc, gFramebufferLayout.writeRef());

    gPipelineState = createGraphicsPipelineState();

    gfx::ISwapchain::Desc swapchainDesc{
        .format = gfx::Format::B8G8R8A8_UNORM,
        .width = 800,
        .height = 600,
        .imageCount = 2,
        .queue = gQueue.get(),
    };
    slangCheck(
        gDevice->createSwapchain(swapchainDesc, window->getWindowHandle(), gSwapchain.writeRef())
    );

    auto format = gSwapchain->getDesc().format;
    for (int i = 0; i < 2; ++i) {
        gfx::ITextureResource::Desc depthBufferDesc;
        depthBufferDesc.type = gfx::IResource::Type::Texture2D;
        depthBufferDesc.size.width = window->getWidth();
        depthBufferDesc.size.height = window->getHeight();
        depthBufferDesc.size.depth = 1;
        depthBufferDesc.format = gfx::Format::D32_FLOAT;
        depthBufferDesc.defaultState = gfx::ResourceState::DepthWrite;
        depthBufferDesc.allowedStates = gfx::ResourceStateSet(gfx::ResourceState::DepthWrite);
        gfx::ClearValue depthClearValue = {};
        depthBufferDesc.optimalClearValue = &depthClearValue;
        ComPtr<gfx::ITextureResource> depthBufferResource =
            gDevice->createTextureResource(depthBufferDesc, nullptr);

        ComPtr<gfx::ITextureResource> colorBuffer;
        slangCheck(gSwapchain->getImage(i, colorBuffer.writeRef()));

        /////////////////////////////////////////////////////////////////////////////////////
        gfx::IResourceView::Desc colorBufferViewDesc{};
        colorBufferViewDesc.format = format;
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
        slangCheck(gDevice->createFramebuffer(framebufferDesc, gFramebuffer[i].writeRef()));
    }

    for (int i = 0; i < 2; ++i) {
        gfx::ITransientResourceHeap::Desc transientHeapDesc{.constantBufferSize = 4096 * 1024};
        slangCheck(gDevice->createTransientResourceHeap(
            transientHeapDesc, gTransientHeapBuffer[i].writeRef()
        ));
    }

    gfx::IFence::Desc fenceDesc{};
    slangCheck(gDevice->createFence(fenceDesc, gFrameFence.writeRef()));

    gfx::IRenderPassLayout::Desc renderPassDesc{};
    renderPassDesc.framebufferLayout = gFramebufferLayout;
    renderPassDesc.renderTargetCount = 1;
    gfx::IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
    gfx::IRenderPassLayout::TargetAccessDesc depthStencilAccess = {};
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

void SlangContext::mainLoop() {
#if 0
    gfx::IFence *fences[] = {gFrameFence.get()};
    uint64_t fenceValues[] = {this->gFrameIndex};
    gDevice->waitForFences(1, fences, fenceValues, true, UINT64_MAX);
#endif

    auto frameIndex = gSwapchain->acquireNextImage();
    if (frameIndex == -1)
        throw kira::Anyhow("Swapchain is invalid or out-of-date");

    LogInfo("Frame index: {:d}", frameIndex);
    gTransientHeapBuffer[frameIndex]->synchronizeAndReset();
    renderFrame(frameIndex);
    gTransientHeapBuffer[frameIndex]->finish();
    ++this->gFrameIndex;
}

void SlangContext::renderFrame(int frameIndex) {
    ComPtr<gfx::ICommandBuffer> commandBuffer =
        gTransientHeapBuffer[frameIndex]->createCommandBuffer();
    KIRA_ASSERT(gRenderPassLayout != nullptr);
    KIRA_ASSERT(gFramebuffer[frameIndex] != nullptr);
    auto *renderEncoder =
        commandBuffer->encodeRenderCommands(gRenderPassLayout, gFramebuffer[frameIndex]);

    gfx::Viewport viewport{};
    viewport.maxZ = 1.0f;
    viewport.extentX = (float)window->getWidth();
    viewport.extentY = (float)window->getHeight();
    renderEncoder->setViewportAndScissor(viewport);

    auto *rootObject = renderEncoder->bindPipeline(gPipelineState);
    gfx::ShaderCursor rootCursor{rootObject};

    std::array<float, 16> identityMatrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    slangCheck(rootCursor["Uniforms"]["modelViewProjection"].setData(
        identityMatrix.data(), sizeof(float) * 4 * 4
    ));

    renderEncoder->setVertexBuffer(0, gVertexBuffer);
    renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::TriangleList);

    renderEncoder->draw(3);
    renderEncoder->endEncoding();
    commandBuffer->close();
    gQueue->executeCommandBuffer(commandBuffer, gFrameFence.get(), gFrameIndex + 1);
    gSwapchain->present();
}
} // namespace krd

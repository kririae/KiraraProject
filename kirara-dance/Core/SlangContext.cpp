#include "Core/SlangContext.h"
#include <kira/SmallVector.h>

#include <slang-com-ptr.h>
#include <slang-gfx.h>
#include <slang.h>

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

struct SlangContext::Impl {
    ComPtr<gfx::IDevice> gDevice;
    ComPtr<gfx::ICommandQueue> gQueue;
    ComPtr<gfx::ISwapchain> gSwapchain;

    std::array<ComPtr<gfx::ITransientResourceHeap>, 2> gTransientHeapBuffer;

public:
    /// Setup necessary GFX global objects above.
    Impl();

    /// Slang's workaround makes it not necessary to define a destructor like OptiX: any destruction
    /// order is allowed by double reference counting.
    ///
    /// \see https://github.com/shader-slang/slang/pull/1788
    ~Impl() = default;

public:
    /// \see gDevice->createGraphicsPipelineState
    ComPtr<gfx::IPipelineState> createGraphicsPipelineState();

    /// Launch the shader program.
    void renderFrame();
};

SlangContext::Impl::Impl() {
    gfx::gfxEnableDebugLayer();
    slangCheck(gfx::gfxSetDebugCallback(&krd::gfxDebugCallback));

    gfx::IDevice::Desc deviceDesc{};
    slangCheck(gfx::gfxCreateDevice(&deviceDesc, gDevice.writeRef()));

    gfx::ICommandQueue::Desc queueDesc{.type = gfx::ICommandQueue::QueueType::Graphics};
    slangCheck(gDevice->createCommandQueue(queueDesc, gQueue.writeRef()));
}

ComPtr<gfx::IPipelineState> SlangContext::Impl::createGraphicsPipelineState() {
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

    // TODO(krr): create input layout, etc.
    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, position)},
        {"COLOR", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, color)},
    };
    auto inputLayout = gDevice->createInputLayout(sizeof(Vertex), inputElements, 2);
    if (!inputLayout)
        throw kira::Anyhow("Failed to create the input layout");

    // TODO(krr): allocate buffer.
    gfx::IBufferResource::Desc bufferDesc{};
    bufferDesc.sizeInBytes = VertexCount * sizeof(Vertex);
    bufferDesc.defaultState = gfx::ResourceState::VertexBuffer;

    ComPtr<gfx::IPipelineState> pipelineState;
    gfx::GraphicsPipelineStateDesc pipelineDesc{
        .program = program,
    };

    return pipelineState;
}

SlangContext::SlangContext() { pImpl = new Impl(); }
SlangContext::~SlangContext() { delete pImpl; }
} // namespace krd

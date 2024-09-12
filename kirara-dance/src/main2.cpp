#include <kira/Anyhow.h>
#include <kira/Logger.h>
#include <kira/SmallVector.h>
#include <slang-com-ptr.h>
#include <slang-gfx.h>
#include <slang.h>

using Slang::ComPtr;

template <bool bShouldThrow = true>
inline void slangCheck(
    SlangResult result, std::source_location loc = std::source_location::current()
) noexcept(not bShouldThrow) {
    if (result != SLANG_OK) [[unlikely]] {
        auto const str = fmt::format(
            "slangCheck(): Slang API call error {:d} at {:s}:{:d}", static_cast<int>(result),
            loc.file_name(), loc.line()
        );

        if constexpr (bShouldThrow)
            throw kira::Anyhow("{}", str);
        else
            kira::LogError("{}", str);
    }
}

struct DebugCallback : public gfx::IDebugCallback {
    std::string trim(std::string const &str) {
        if (str.empty())
            return {};
        auto start = str.find_first_not_of('\n');
        auto end = str.find_last_not_of('\n');
        return str.substr(start, end - start + 1);
    }

    void SLANG_NO_THROW handleMessage(
        gfx::DebugMessageType type, gfx::DebugMessageSource source, char const *message
    ) override {
        (void)(source);
        auto trimedMessage = trim(message);
        switch (type) {
        case gfx::DebugMessageType::Info: kira::LogInfo<"gfx">("{}", trimedMessage); break;
        case gfx::DebugMessageType::Warning: kira::LogWarn<"gfx">("{}", trimedMessage); break;
        case gfx::DebugMessageType::Error: kira::LogError<"gfx">("{}", trimedMessage); break;
        }
    }
};

gfx::IDevice *gDevice = nullptr;
DebugCallback gCallback;
gfx::ICommandQueue *gQueue;
gfx::ITransientResourceHeap *gTransientHeap;

SlangResult createComputePipelineFromShader(gfx::IPipelineState *&outPipelineState) {
    // Slang shader compilation requires the creation of session
    ComPtr<slang::ISession> session;
    {
        ComPtr<slang::IGlobalSession> slangGlobalSession;
        slangCheck(slang::createGlobalSession(slangGlobalSession.writeRef()));

        // Next we create a compilation session to generate SPIRV code from Slang source.
        slang::SessionDesc sessionDesc = {};
        slang::TargetDesc targetDesc = {};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = slangGlobalSession->findProfile("spirv_1_5");
        targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        slangCheck(slangGlobalSession->createSession(sessionDesc, session.writeRef()));
    }

    slang::IModule *slangModule = nullptr;
    slangModule =
        session->loadModule("/home/krr/Projects/KiraraProject/kirara-dance/src/shaders.slang");
    if (!slangModule) {
        kira::LogError("failed to create module");
        return SLANG_FAIL;
    }

    ComPtr<slang::IEntryPoint> entryPoint;
    slangModule->findEntryPointByName("computeMain", entryPoint.writeRef());
    std::array<slang::IComponentType *, 1> entryPoints{entryPoint.get()};

    kira::SmallVector<slang::IComponentType *> componentTypes;
    componentTypes.push_back(slangModule);
    componentTypes.push_back(entryPoint);

    ComPtr<slang::IComponentType> composedProgram;
    slangCheck(session->createCompositeComponentType(
        componentTypes.data(), componentTypes.size(), composedProgram.writeRef()
    ));

    // Let's turn back to gfx
    gfx::IShaderProgram::Desc shaderDesc{
        .slangGlobalScope = composedProgram.get(),
    };
    ComPtr<gfx::IShaderProgram> shaderProgram;
    slangCheck(gDevice->createProgram(shaderDesc, shaderProgram.writeRef()));

    gfx::ComputePipelineStateDesc pipelineDesc{.program = shaderProgram};
    slangCheck(gDevice->createComputePipelineState(pipelineDesc, &outPipelineState));

    return SLANG_OK;
}

int main() try {
    gfx::gfxEnableDebugLayer();
    slangCheck(gfx::gfxSetDebugCallback(&gCallback));

    // Create GFX device.
    gfx::IDevice::Desc deviceDesc{};
    slangCheck(gfxCreateDevice(&deviceDesc, &gDevice));

    // Create GFX CommandQueue.
    gfx::ICommandQueue::Desc queueDesc{.type = gfx::ICommandQueue::QueueType::Graphics};
    gDevice->createCommandQueue(queueDesc, &gQueue);

    // Allocate GFX CommandBuffer.
    gfx::ITransientResourceHeap::Desc transientHeapDesc{.constantBufferSize = 4096};
    gDevice->createTransientResourceHeap(transientHeapDesc, &gTransientHeap);

    gfx::ICommandBuffer *commandBuffer;
    gTransientHeap->createCommandBuffer(&commandBuffer);

    // Create BufferResource to upload.
    constexpr int numElt = 32;
    std::array<float, numElt> initData;
    for (int i = 0; i < numElt; ++i)
        initData[i] = float(i);
    gfx::IBufferResource::Desc bufferDesc{
        .sizeInBytes = numElt * sizeof(float),
        .elementSize = sizeof(float),
        .format = gfx::Format::Unknown,
    };
    bufferDesc.sizeInBytes = numElt * sizeof(float);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.format = gfx::Format::Unknown;
    bufferDesc.defaultState = gfx::ResourceState::ShaderResource;
    bufferDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::ShaderResource, gfx::ResourceState::UnorderedAccess,
        gfx::ResourceState::CopyDestination, gfx::ResourceState::CopySource
    );

    gfx::IBufferResource *inputBuffer0;
    gfx::IBufferResource *inputBuffer1;
    gfx::IBufferResource *resultBuffer;
    slangCheck(gDevice->createBufferResource(bufferDesc, initData.data(), &inputBuffer0));
    slangCheck(gDevice->createBufferResource(bufferDesc, initData.data(), &inputBuffer1));
    bufferDesc.defaultState = gfx::ResourceState::UnorderedAccess;
    slangCheck(gDevice->createBufferResource(bufferDesc, nullptr, &resultBuffer));

    gfx::IPipelineState *pipelineState;
    createComputePipelineFromShader(pipelineState);

    gfx::IComputeCommandEncoder *encoder = commandBuffer->encodeComputeCommands();
    gfx::IShaderObject *rootObject = encoder->bindPipeline(pipelineState);

    gfx::IResourceView *buffer0View;
    {
        gfx::IResourceView::Desc viewDesc{
            .type = gfx::IResourceView::Type::ShaderResource,
            .format = gfx::Format::Unknown,
        };
        slangCheck(gDevice->createBufferView(inputBuffer0, nullptr, viewDesc, &buffer0View));
    }
    rootObject->setResource(gfx::ShaderOffset{0, 0, 0}, buffer0View);

    gfx::IResourceView *buffer1View;
    {
        gfx::IResourceView::Desc viewDesc{
            .type = gfx::IResourceView::Type::ShaderResource,
            .format = gfx::Format::Unknown,
        };
        slangCheck(gDevice->createBufferView(inputBuffer1, nullptr, viewDesc, &buffer1View));
    }
    rootObject->setResource(gfx::ShaderOffset{0, 1, 0}, buffer1View);

    gfx::IResourceView *resultView;
    {
        gfx::IResourceView::Desc viewDesc{
            .type = gfx::IResourceView::Type::UnorderedAccess,
            .format = gfx::Format::Unknown,
        };
        slangCheck(gDevice->createBufferView(resultBuffer, nullptr, viewDesc, &resultView));
    }
    rootObject->setResource(gfx::ShaderOffset{0, 2, 0}, resultView);

    encoder->dispatchCompute(32, 1, 1);
    encoder->endEncoding();
    commandBuffer->close();
    gQueue->executeCommandBuffer(commandBuffer);
    gQueue->waitOnHost();

    // Download to host
    std::array<float, numElt> result;
    ComPtr<ISlangBlob> resultBlob;
    gDevice->readBufferResource(resultBuffer, 0, numElt * sizeof(float), resultBlob.writeRef());
    std::memcpy(result.data(), resultBlob->getBufferPointer(), numElt * sizeof(float));

    for (int i = 0; i < numElt; ++i)
        std::printf("%f\n", result[i]);

    return 0;
} catch (std::exception const &e) { kira::LogError("{}", e.what()); }

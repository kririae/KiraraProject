#include <kira/Anyhow.h>
#include <kira/Logger.h>
#include <kira/SmallVector.h>
#include <slang-com-ptr.h>
#include <slang-gfx.h>
#include <slang.h>

#include "Core/KIRA.h"
#include "Core/ShaderCursor.h"

using krd::slangCheck;
using Slang::ComPtr;

struct App {
public:
    ComPtr<gfx::IDevice> gDevice;
    ComPtr<gfx::ICommandQueue> gQueue;
    ComPtr<gfx::ITransientResourceHeap> gTransientHeap;

public:
    SlangResult createComputePipelineFromShader(gfx::IPipelineState **outPipelineState) {
        ComPtr<slang::ISession> session;
        session = gDevice->getSlangSession();

        slang::IModule *slangModule = nullptr;
        slangModule =
            session->loadModule("/home/krr/Projects/KiraraProject/kirara-dance/Shader/shaders.slang"
            );
        if (!slangModule)
            return SLANG_FAIL;

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

        gfx::IShaderProgram::Desc shaderDesc{
            .slangGlobalScope = composedProgram.get(),
        };
        ComPtr<gfx::IShaderProgram> shaderProgram;
        slangCheck(gDevice->createProgram(shaderDesc, shaderProgram.writeRef()));

        gfx::ComputePipelineStateDesc pipelineDesc{.program = shaderProgram};
        slangCheck(gDevice->createComputePipelineState(pipelineDesc, outPipelineState));

        return SLANG_OK;
    }

    void run() {
        gfx::gfxEnableDebugLayer();
        slangCheck(gfx::gfxSetDebugCallback(&krd::gfxDebugCallback));

        // Create GFX device.
        gfx::IDevice::Desc deviceDesc{};
        slangCheck(gfxCreateDevice(&deviceDesc, gDevice.writeRef()));

        // Create GFX CommandQueue.
        gfx::ICommandQueue::Desc queueDesc{.type = gfx::ICommandQueue::QueueType::Graphics};
        slangCheck(gDevice->createCommandQueue(queueDesc, gQueue.writeRef()));

        // Allocate GFX CommandBuffer.
        gfx::ITransientResourceHeap::Desc transientHeapDesc{.constantBufferSize = 4096};
        slangCheck(
            gDevice->createTransientResourceHeap(transientHeapDesc, gTransientHeap.writeRef())
        );

        ComPtr<gfx::ICommandBuffer> commandBuffer;
        slangCheck(gTransientHeap->createCommandBuffer(commandBuffer.writeRef()));

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

        ComPtr<gfx::IBufferResource> inputBuffer0;
        ComPtr<gfx::IBufferResource> inputBuffer1;
        ComPtr<gfx::IBufferResource> resultBuffer;
        slangCheck(
            gDevice->createBufferResource(bufferDesc, initData.data(), inputBuffer0.writeRef())
        );
        slangCheck(
            gDevice->createBufferResource(bufferDesc, initData.data(), inputBuffer1.writeRef())
        );
        bufferDesc.defaultState = gfx::ResourceState::UnorderedAccess;
        slangCheck(gDevice->createBufferResource(bufferDesc, nullptr, resultBuffer.writeRef()));

        ComPtr<gfx::IPipelineState> pipelineState;
        slangCheck(createComputePipelineFromShader(pipelineState.writeRef()));

        auto *encoder = commandBuffer->encodeComputeCommands();
        auto *rootObject = encoder->bindPipeline(pipelineState);

        gfx::ShaderCursor rootCursor(rootObject);

        ComPtr<gfx::IResourceView> buffer0View;
        {
            gfx::IResourceView::Desc viewDesc{
                .type = gfx::IResourceView::Type::ShaderResource,
                .format = gfx::Format::Unknown,
            };
            slangCheck(
                gDevice->createBufferView(inputBuffer0, nullptr, viewDesc, buffer0View.writeRef())
            );
        }
        slangCheck(rootCursor["buffer0"].setResource(buffer0View));

        ComPtr<gfx::IResourceView> buffer1View;
        {
            gfx::IResourceView::Desc viewDesc{
                .type = gfx::IResourceView::Type::ShaderResource,
                .format = gfx::Format::Unknown,
            };
            slangCheck(
                gDevice->createBufferView(inputBuffer1, nullptr, viewDesc, buffer1View.writeRef())
            );
        }
        slangCheck(rootCursor["buffer1"].setResource(buffer1View));

        ComPtr<gfx::IResourceView> resultView;
        {
            gfx::IResourceView::Desc viewDesc{
                .type = gfx::IResourceView::Type::UnorderedAccess,
                .format = gfx::Format::Unknown,
            };
            slangCheck(
                gDevice->createBufferView(resultBuffer, nullptr, viewDesc, resultView.writeRef())
            );
        }
        slangCheck(rootCursor["result"].setResource(resultView));

        encoder->dispatchCompute(numElt, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        gQueue->executeCommandBuffer(commandBuffer);
        gQueue->waitOnHost();

        // Download to host
        std::array<float, numElt> result;
        ComPtr<ISlangBlob> resultBlob;
        slangCheck(gDevice->readBufferResource(
            resultBuffer, 0, numElt * sizeof(float), resultBlob.writeRef()
        ));
        std::memcpy(result.data(), resultBlob->getBufferPointer(), numElt * sizeof(float));

        for (int i = 0; i < numElt; ++i)
            fmt::print("{}\n", result[i]);
    }
};

int main() try {
    App app;
    app.run();
} catch (std::exception const &e) { kira::LogError("{}", e.what()); }

#include "Core/SlangContext.h"

#include "Core/SlangUtils.h"

namespace krd {
SlangContext::SlangContext() {
    gfx::gfxEnableDebugLayer();
    slangCheck(gfx::gfxSetDebugCallback(&krd::gfxDebugCallback));

    gfx::IDevice::Desc deviceDesc{.deviceType = gfx::DeviceType::Default};
    slangCheck(gfx::gfxCreateDevice(&deviceDesc, gDevice.writeRef()));

    gfx::ICommandQueue::Desc queueDesc{.type = gfx::ICommandQueue::QueueType::Graphics};
    slangCheck(gDevice->createCommandQueue(queueDesc, gQueue.writeRef()));
}

} // namespace krd

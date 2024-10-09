#include "Core/SlangContext.h"

#include "Core/SlangUtils.h"

namespace krd {
SlangContext::SlangContext() {
#ifndef NDEBUG
    gfx::gfxEnableDebugLayer();
#endif
    slangCheck(gfx::gfxSetDebugCallback(&krd::gfxDebugCallback));

    gfx::IDevice::Desc deviceDesc{.deviceType = gfx::DeviceType::Default};
    slangCheck(gfx::gfxCreateDevice(&deviceDesc, gDevice.writeRef()));

    auto deviceInfo = gDevice->getDeviceInfo();
    LogInfo("Graphics device information");
    LogInfo("    device:  {:s}", deviceInfo.adapterName);
    LogInfo("    backend: {:s}", deviceInfo.apiName);

    gfx::ICommandQueue::Desc queueDesc{.type = gfx::ICommandQueue::QueueType::Graphics};
    slangCheck(gDevice->createCommandQueue(queueDesc, gQueue.writeRef()));
}
} // namespace krd

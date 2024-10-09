#pragma once

#include <kira/SmallVector.h>

#include <slang-gfx.h>

#include "Core/Object.h"

using Slang::ComPtr;

namespace krd {
class SlangContext : public Object {
protected:
    /// Setup necessary GFX global objects above.
    SlangContext();

public:
    /// Slang's workaround makes it not necessary to define a destructor like OptiX: any destruction
    /// order is allowed by double reference counting.
    ///
    /// \see https://github.com/shader-slang/slang/pull/1788
    ~SlangContext() override {
        gQueue->waitOnHost();
        LogTrace("SlangContext: destructed");
    }

    /// Get the device that this context is associated with.
    [[nodiscard]] auto getDevice() const { return gDevice; }

    /// Get the command queue that this context is associated with.
    [[nodiscard]] auto getCommandQueue() const { return gQueue; }

protected:
    ComPtr<gfx::IDevice> gDevice{};
    ComPtr<gfx::ICommandQueue> gQueue{};
};
} // namespace krd

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
    virtual ~SlangContext() = default;

protected:
    ComPtr<gfx::IDevice> gDevice;
    ComPtr<gfx::ICommandQueue> gQueue;
};
} // namespace krd

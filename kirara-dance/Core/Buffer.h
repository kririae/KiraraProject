#pragma once

#include <slang-gfx.h>

using Slang::ComPtr;

namespace krd {
class Buffer {
public:
    ///
    Buffer();

    ///
    ~Buffer();

protected:
    ComPtr<gfx::IDevice> gDevice;
};
} // namespace krd

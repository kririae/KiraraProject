#pragma once

#include <slang-gfx.h>

namespace krd {
/// Check if the device supports the given shader model.
bool isShaderModelSupported(gfx::IDevice *device, uint8_t major, uint8_t minor);
} // namespace krd

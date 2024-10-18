#include "Core/DeviceUtils.h"

#include <fmt/format.h>

namespace krd {
bool isShaderModelSupported(gfx::IDevice *device, uint8_t major, uint8_t minor) {
    return device->hasFeature(fmt::format("sm_{:d}_{:d}", major, minor).c_str());
}
} // namespace krd

#include "flux/Optix/OptixMegaKernelIr.h"

#include "flux/Optix/OptixMegaKernelIrFiles.inc"

namespace flux {
std::span<char const> getOptixMegaKernelIr() noexcept {
    return optixMegaKernelIrFiles.front().second;
}
} // namespace flux

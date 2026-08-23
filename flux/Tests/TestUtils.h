#pragma once

#include <cuda_runtime_api.h>

namespace flux::test {
/// \brief Returns whether the CUDA Runtime API can access a device.
[[nodiscard]] inline bool hasCudaDevice() {
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

/// \brief Returns whether the current CUDA device supports stream-ordered allocation.
[[nodiscard]] inline bool hasCudaMemoryPoolSupport() {
    if (!hasCudaDevice())
        return false;

    int device = 0;
    int supported = 0;
    return cudaGetDevice(&device) == cudaSuccess &&
           cudaDeviceGetAttribute(&supported, cudaDevAttrMemoryPoolsSupported, device) ==
               cudaSuccess &&
           supported != 0;
}
} // namespace flux::test

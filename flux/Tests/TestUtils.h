#pragma once

#include <cuda_runtime_api.h>

namespace flux::test {
/// \brief Returns whether the CUDA Runtime API can access a device.
[[nodiscard]] inline bool hasCudaDevice() {
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}
} // namespace flux::test

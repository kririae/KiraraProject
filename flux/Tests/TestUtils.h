#pragma once

#include <cuda_runtime_api.h>

#include <concepts>
#include <cstddef>
#include <memory>
#include <utility>

#include "flux/Core/HostBuffer.h"

namespace flux::test {
/// \brief Returns a shared buffer of the arguments, with one spare element.
template <typename T, std::convertible_to<T>... Rest>
[[nodiscard]] Shared<HostBuffer<T>> sharedBuffer(T first, Rest... rest) {
    auto buffer = std::make_shared<HostBuffer<T>>();
    auto constexpr count = 1 + sizeof...(Rest);
    buffer->resize(count, count + 1);

    // Write the arguments in order.
    auto *out = buffer->data();
    *out = std::move(first);
    ((*++out = static_cast<T>(rest)), ...);
    return buffer;
}

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

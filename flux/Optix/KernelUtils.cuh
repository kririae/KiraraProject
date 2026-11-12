#pragma once

/// \file flux/Optix/KernelUtils.cuh
/// \brief Shared machinery for one-dimensional CUDA kernels.

#include <cuda_runtime_api.h>

#include <concepts>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "flux/Optix/OptixUtils.h"

namespace flux {
namespace detail {
template <typename Functor>
concept LinearIndexFunctor =
    std::is_trivially_copyable_v<Functor> && requires(Functor const functor, std::size_t index) {
        { functor(index) } -> std::same_as<void>;
    };

template <int BlockSize, LinearIndexFunctor Functor>
__global__ __launch_bounds__(BlockSize) void linearKernel(
    std::size_t elementCount, Functor const __grid_constant__ functor
) {
    auto const index =
        static_cast<std::size_t>(blockIdx.x) * static_cast<std::size_t>(blockDim.x) + threadIdx.x;
    if (index < elementCount)
        functor(index);
}
} // namespace detail

/// \brief Invokes \p functor once for each index in \p elementCount.
///
/// The launch is enqueued on \p stream. This function only checks whether CUDA
/// accepted the launch.
///
/// \tparam BlockSize Positive number of threads in each CUDA block.
/// \throw std::invalid_argument If the range needs more blocks than CUDA can
/// represent.
/// \throw kira::Anyhow If CUDA rejects the launch.
template <int BlockSize = 128, detail::LinearIndexFunctor Functor>
void launchLinearKernel(std::size_t elementCount, Functor functor, cudaStream_t stream) {
    static_assert(BlockSize > 0, "BlockSize must be positive");

    if (elementCount == 0)
        return;

    constexpr auto blockSize = static_cast<std::size_t>(BlockSize);
    constexpr auto maximumElementCount =
        static_cast<std::size_t>(std::numeric_limits<int>::max()) * blockSize;
    if (elementCount > maximumElementCount)
        throw std::invalid_argument("CUDA linear kernel range exceeds the supported grid");

    auto const blockCount = (elementCount - 1) / blockSize + 1;
    auto const grid = dim3{static_cast<unsigned int>(blockCount), 1U, 1U};
    auto const block = dim3{static_cast<unsigned int>(blockSize), 1U, 1U};
    detail::linearKernel<BlockSize><<<grid, block, 0, stream>>>(elementCount, functor);
    cudaCheck(cudaGetLastError());
}
} // namespace flux

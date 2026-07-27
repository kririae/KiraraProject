#pragma once

#include <unordered_map>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Scene/Film.h"

namespace flux {
class RenderProduct;

/// \brief Owns OptiX runtime storage for render products.
///
/// Entries are keyed by stable render-product context ID. Their device
/// storage is resized lazily when a product is acquired for a launch.
class OptixRenderProductPool final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Creates an empty pool bound to \p stream.
    explicit OptixRenderProductPool(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    /// \brief Acquires the current device view of \p product.
    ///
    /// The view remains valid until this product is acquired with a different
    /// resolution or the pool is destroyed.
    /// \throw std::invalid_argument If the film is too large for device
    /// storage.
    /// \throw kira::Anyhow If CUDA cannot enqueue an allocation.
    [[nodiscard]] Film::DeviceImpl acquire(RenderProduct const &product);

private:
    struct Entry {
        explicit Entry(cudaStream_t stream) noexcept : normal(stream) {}

        DeviceBuffer<Vec3f> normal;
    };

    std::unordered_map<std::size_t, Entry> entries_;
};
} // namespace flux

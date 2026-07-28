#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

namespace flux {
class OptixHandler;
class RenderProduct;

/// \brief Owns OptiX runtime storage for render products.
///
/// One entry follows each render-product identity. Film resources are updated
/// in place, so camera animation and film changes do not create new entries.
class OptixRenderProductPool final : private Noncopyable, private CudaStreamMixin {
    friend class OptixHandler;

public:
    /// \brief Creates an empty pool bound to \p stream.
    explicit OptixRenderProductPool(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    /// \brief Releases all backend state associated with \p product.
    void erase(RenderProduct const &product) noexcept;

    /// \brief Clears accumulation while preserving target allocations.
    void resetAccumulation() noexcept;

private:
    /// \brief Pixel history produced for one camera payload.
    struct AccumulationState {
        /// Camera payload used to produce the stored pixels.
        Camera::DeviceImpl camera;

        /// Samples represented by the stored pixels.
        std::uint64_t samples;
    };

    /// \brief Backend state attached to one render-product identity.
    struct Entry {
        Entry(RenderProduct const &product, cudaStream_t stream) noexcept
            : product(&product), normal(stream) {}

        /// Keeps the identity key alive until explicit release.
        Ref<RenderProduct const> product;

        /// World-space geometric-normal channel.
        DeviceBuffer<Vec3f> normal;

        /// Device view rebuilt whenever the film layout changes.
        Film::DeviceImpl film;

        /// Valid pixel history, or empty after invalidation or failure.
        std::optional<AccumulationState> accumulation;
    };

    /// \brief Returns the entry for \p product, creating it if absent.
    ///
    /// Existing film storage is resized to match the current product layout.
    /// \throw std::invalid_argument If the film is too large for device
    /// storage.
    /// \throw kira::Anyhow If CUDA cannot allocate the film resources.
    [[nodiscard]] Entry &getOrCreate(RenderProduct const &product);

    /// \brief Returns the existing entry for \p product, if any.
    [[nodiscard]] Entry const *find(RenderProduct const &product) const noexcept;

    std::unordered_map<RenderProduct const *, Entry> entries_;
};
} // namespace flux

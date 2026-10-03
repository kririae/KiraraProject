#pragma once

#include <optix_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
/// \brief Builds and owns the top-level IAS over the GASes of the scene.
class OptixIas final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Describes one IAS instance.
    ///
    /// The descriptor's position in the input array is its instance index. Its
    /// \c primitiveIndex becomes its OptiX instance ID.
    struct InstanceDesc {
        /// Primitive index reported as the OptiX instance ID.
        std::uint32_t primitiveIndex{};

        /// GAS of the instanced geometry. Zero is rejected.
        OptixTraversableHandle gas{};

        /// Base hitgroup record selected for this instance.
        std::uint32_t sbtOffset{};

        /// Row-major object-to-world affine transform.
        std::array<float, 12> transform{
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
        };
    };

    explicit OptixIas(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    /// \brief Rebuilds the IAS from \p instances.
    ///
    /// \param deviceContext OptiX context used for the build.
    /// \param instances Complete visible primitive list. Primitive indices are unique.
    /// \throw kira::Anyhow If an instance has no GAS or exceeds a device limit, or
    ///        OptiX setup fails.
    void build(OptixDeviceContext deviceContext, std::span<InstanceDesc const> instances);

    /// \brief Returns the current IAS handle, or zero when empty.
    [[nodiscard]] OptixTraversableHandle getHandle() const noexcept { return handle_; }

    /// \brief Returns the radius of the current IAS bounds.
    ///
    /// An empty IAS uses radius one.
    [[nodiscard]] float getSceneRadius() const noexcept;

private:
    /// \brief Releases the IAS.
    void clear() noexcept;

    std::vector<OptixInstance> instanceStaging_;
    DeviceBuffer<OptixInstance> instances_{getStream()};
    DeviceBuffer<std::byte> ias_{getStream()};
    DeviceBuffer<OptixAabb> iasBounds_{getStream()};
    OptixAabb iasBoundsStaging_{};
    OptixTraversableHandle handle_{};
};
} // namespace flux

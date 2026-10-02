#pragma once

#include <optix_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
/// \brief Builds and owns triangle GASes and their top-level IAS.
class OptixAccel final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Describes one IAS instance.
    ///
    /// The descriptor's position in the input array is its instance index. Its
    /// \c primitiveIndex becomes its OptiX instance ID.
    struct InstanceDesc {
        /// Primitive index reported as the OptiX instance ID.
        std::uint32_t primitiveIndex{};

        /// Index of the referenced GAS, which must not be a hole.
        std::uint32_t geometryIndex{};

        /// Base hitgroup record selected for this instance.
        std::uint32_t sbtOffset{};

        /// Row-major object-to-world affine transform.
        std::array<float, 12> transform{
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
        };
    };

    explicit OptixAccel(cudaStream_t stream) noexcept : CudaStreamMixin(stream) {}

    /// \brief Rebuilds and compacts one GAS for every input of \p inputs.
    ///
    /// Existing GAS and IAS storage is released before rebuilding. The GAS of
    /// input \c i is the one that an instance with geometry index \c i references.
    /// \param deviceContext OptiX context used for the build.
    /// \param inputs Triangle build inputs whose device storage remains alive. An
    ///        empty entry is a hole, which gets no GAS.
    /// \throw kira::Anyhow If OptiX or CUDA setup fails.
    void buildGas(
        OptixDeviceContext deviceContext, std::span<std::optional<OptixBuildInput> const> inputs
    );

    /// \brief Rebuilds the IAS from \p instances.
    ///
    /// \param deviceContext OptiX context used for the build.
    /// \param instances Complete visible primitive list. Primitive indices are unique.
    /// \throw kira::Anyhow If an index is invalid or a hole, or OptiX setup fails.
    void buildIas(OptixDeviceContext deviceContext, std::span<InstanceDesc const> instances);

    /// \brief Returns the current IAS handle, or zero when empty.
    [[nodiscard]] OptixTraversableHandle getHandle() const noexcept { return handle_; }

    /// \brief Returns the radius of the current IAS bounds.
    ///
    /// An empty IAS uses radius one.
    [[nodiscard]] float getSceneRadius() const noexcept;

private:
    /// GAS of one geometry index. A hole has a zero handle.
    struct GasEntry {
        explicit GasEntry(cudaStream_t stream) noexcept : storage(stream) {}

        OptixTraversableHandle handle{};
        DeviceBuffer<std::byte> storage;
    };

    std::vector<GasEntry> gasEntries_;
    std::vector<OptixInstance> instanceStaging_;
    DeviceBuffer<OptixInstance> instances_{getStream()};
    DeviceBuffer<std::byte> ias_{getStream()};
    DeviceBuffer<OptixAabb> iasBounds_{getStream()};
    OptixAabb iasBoundsStaging_{};
    OptixTraversableHandle handle_{};
};
} // namespace flux

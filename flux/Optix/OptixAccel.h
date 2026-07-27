#pragma once

#include <optix_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
/// \brief Builds and owns triangle GASes and their top-level IAS.
class OptixAccel final : private Noncopyable, private CudaStreamMixin {
public:
    /// \brief Describes one IAS instance.
    ///
    /// The descriptor's position in the input array becomes its OptiX
    /// instance ID.
    struct InstanceDesc {
        /// Dense index of the referenced GAS.
        std::uint32_t geometryIndex{};

        /// Row-major object-to-world affine transform.
        std::array<float, 12> transform{
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
        };
    };

    /// \brief Creates an empty acceleration structure bound to \p stream.
    explicit OptixAccel(cudaStream_t stream) noexcept
        : CudaStreamMixin(stream), instances_(stream), ias_(stream) {}

    /// \brief Rebuilds and compacts one GAS for every element of \p inputs.
    ///
    /// Existing GAS and IAS storage is released before rebuilding.
    /// \param deviceContext OptiX context used for the build.
    /// \param inputs Triangle build inputs whose device storage remains alive.
    /// \throw kira::Anyhow If OptiX or CUDA setup fails.
    void buildGas(OptixDeviceContext deviceContext, std::span<OptixBuildInput const> inputs);

    /// \brief Rebuilds the IAS from \p instances.
    ///
    /// \param deviceContext OptiX context used for the build.
    /// \param instances Complete visible primitive list in device-table order.
    /// \throw kira::Anyhow If an index is invalid or OptiX setup fails.
    void buildIas(OptixDeviceContext deviceContext, std::span<InstanceDesc const> instances);

    /// \brief Returns the current IAS handle, or zero when empty.
    [[nodiscard]] OptixTraversableHandle getHandle() const noexcept { return handle_; }

private:
    struct GasEntry {
        explicit GasEntry(cudaStream_t stream) noexcept : storage(stream) {}

        OptixTraversableHandle handle{};
        DeviceBuffer<std::byte> storage;
    };

    std::vector<GasEntry> gasEntries_;
    std::vector<OptixInstance> instanceStaging_;
    DeviceBuffer<OptixInstance> instances_;
    DeviceBuffer<std::byte> ias_;
    OptixTraversableHandle handle_{};
};
} // namespace flux

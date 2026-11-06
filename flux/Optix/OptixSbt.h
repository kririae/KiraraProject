#pragma once

#include <optix_types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "flux/Core/Object.h"
#include "flux/Optix/DeletionQueue.h"
#include "flux/Optix/DeviceBuffer.h"
#include "flux/Scene/Geometry.h"
#include "flux/Shading/BSDF.h"

namespace flux {
class OptixMegaKernelModule;
class OptixShaderModule;

/// \brief Owns the records for the current OptiX pipeline.
class OptixSbt final : private Noncopyable {
public:
    /// \brief Creates an empty shader binding table.
    /// \param deletionQueue All copies run on its stream.
    explicit OptixSbt(DeletionQueue &deletionQueue) noexcept
        : deletionQueue_(deletionQueue), records_(deletionQueue.getStream()) {}

    /// \brief Rebuilds the table for the groups of \p module and \p shaderModules.
    ///
    /// \param shaderModules In slot order: slot \c s is callables record \c s.
    void build(
        OptixMegaKernelModule const &module,
        std::span<std::shared_ptr<OptixShaderModule const> const> shaderModules
    );

    /// \brief Returns the populated shader binding table.
    [[nodiscard]] OptixShaderBindingTable const &getTable() const noexcept { return table_; }

    /// \brief Returns the hitgroup record index for one BSDF and geometry pair.
    [[nodiscard]] static constexpr std::size_t
    getHitgroupRecordIndex(BSDFType bsdf, GeometryType geometry) noexcept {
        return static_cast<std::size_t>(bsdf) * numGeometryTypes +
               static_cast<std::size_t>(geometry);
    }

    /// \brief Returns the IAS SBT offset for the selected record.
    [[nodiscard]] static constexpr std::uint32_t
    getInstanceOffset(BSDFType bsdf, GeometryType geometry) noexcept {
        return static_cast<std::uint32_t>(getHitgroupRecordIndex(bsdf, geometry));
    }

    /// \brief Returns the number of records in the hitgroup section.
    [[nodiscard]] static constexpr std::size_t getNumHitgroupRecords() noexcept {
        return numHitgroupRecords;
    }

private:
    static constexpr std::size_t numBSDFTypes = static_cast<std::size_t>(BSDFType::Count);
    static constexpr std::size_t numGeometryTypes = static_cast<std::size_t>(GeometryType::Count);
    static constexpr std::size_t numHitgroupRecords = numBSDFTypes * numGeometryTypes;
    static constexpr std::size_t raygenRecord = 0;
    static constexpr std::size_t missRecord = raygenRecord + 1;
    static constexpr std::size_t hitgroupRecords = missRecord + 1;
    static constexpr std::size_t callablesRecords = hitgroupRecords + numHitgroupRecords;

    struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) Record {
        std::array<char, OPTIX_SBT_RECORD_HEADER_SIZE> header;
    };
    static_assert(sizeof(Record) % OPTIX_SBT_RECORD_ALIGNMENT == 0);

    DeletionQueue &deletionQueue_;
    DeviceBuffer<Record> records_;
    OptixShaderBindingTable table_{};
};
} // namespace flux

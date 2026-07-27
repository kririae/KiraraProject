#pragma once

#include <optix_types.h>

#include <array>
#include <cstddef>

#include "flux/Core/Object.h"
#include "flux/Optix/DeviceBuffer.h"

namespace flux {
class OptixProgram;

/// \brief Owns the header-only records for the current OptiX pipeline.
class OptixSbt final : private Noncopyable {
public:
    /// \brief Creates an empty shader binding table bound to \p stream.
    explicit OptixSbt(cudaStream_t stream) noexcept : records_(stream) {}

    /// \brief Rebuilds the raygen, miss, and triangle-hit records.
    void build(OptixProgram const &program);

    /// \brief Returns the populated shader binding table.
    [[nodiscard]] OptixShaderBindingTable const &getTable() const noexcept { return table_; }

private:
    enum RecordIndex : std::size_t {
        RaygenRecord,
        MissRecord,
        HitgroupRecord,
        NumRecords,
    };

    struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) Record {
        std::array<char, OPTIX_SBT_RECORD_HEADER_SIZE> header;
    };
    static_assert(sizeof(Record) == OPTIX_SBT_RECORD_HEADER_SIZE);

    std::array<Record, NumRecords> staging_{};
    DeviceBuffer<Record> records_;
    OptixShaderBindingTable table_{};
};
} // namespace flux

#pragma once

#include <cstdint>
#include <type_traits>

namespace flux {
/// \brief Where the device finds the callable and the data of a shader BSDF or a shader EDF.
struct OptixShaderBinding {
    /// The index of the callable record of the shader.
    std::uint32_t slot;

    /// The index of the first parameter word of the shader in
    /// \c OptixCallableLaunchParams::words.
    std::uint32_t wordOffset;

    /// The index of the first texture of the shader in
    /// \c OptixCallableLaunchParams::textureIndices.
    std::uint32_t textureOffset;
};

static_assert(std::is_standard_layout_v<OptixShaderBinding>);
static_assert(std::is_trivially_copyable_v<OptixShaderBinding>);
} // namespace flux

#pragma once

#include <cstdint>
#include <type_traits>

namespace flux {
/// \brief Where the device finds the callable and the data of a shader BSDF.
struct OptixShaderBSDFBinding {
    /// The dense index of the shader asset among the live ones, which is the index of its
    /// callable record.
    std::uint32_t slot;

    /// The index of the first parameter word of the BSDF in \c OptixCallableLaunchParams::words.
    std::uint32_t wordOffset;

    /// The index of the first texture of the BSDF in
    /// \c OptixCallableLaunchParams::textureIndices.
    std::uint32_t textureOffset;
};

static_assert(std::is_standard_layout_v<OptixShaderBSDFBinding>);
static_assert(std::is_trivially_copyable_v<OptixShaderBSDFBinding>);
} // namespace flux

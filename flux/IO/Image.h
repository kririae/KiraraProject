#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "flux/Core/Math.h"

namespace flux {
/// \brief Storage type of one image component.
enum class ImageComponentType : std::uint8_t {
    UNorm8,
    Float16,
    Float32,
};

/// \brief Non-owning view of tightly packed image pixels.
///
/// Components are interleaved. Rows run from top to bottom.
struct ImageView {
    std::span<std::byte const> pixels;
    Vec2u extent;
    ImageComponentType componentType;
    std::uint8_t componentCount;
};
} // namespace flux

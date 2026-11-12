#pragma once

#include <concepts>
#include <stdexcept>
#include <vector>

#include "flux/Shading/Texture.h"

namespace flux {
/// \brief Throws unless each texture of a shader is set, and \p belongs holds for it.
///
/// \throw std::invalid_argument If a texture is null, or \p belongs does not hold for it.
void checkShaderTextures(
    std::vector<Ref<ImageTexture const>> const &textures,
    std::predicate<ImageTexture const &> auto const &belongs
) {
    for (auto const &texture : textures) {
        if (!texture)
            throw std::invalid_argument("shader: texture must not be null");
        if (!belongs(*texture))
            throw std::invalid_argument("shader: texture belongs to another context");
    }
}
} // namespace flux

#pragma once

/// \file
/// \brief OIIO implementation of ImageAsset and ImageAssetPool.

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/texture.h>

#include <memory>
#include <span>
#include <string_view>

#include "flux/Scene/ImageAsset.h"

namespace flux {
/// \brief Returns whether \p orientation swaps the axes of the file to get those of the image.
[[nodiscard]] constexpr bool transposesAxes(std::uint8_t orientation) noexcept {
    return orientation >= 5 && orientation <= 8;
}

/// \brief Complete image pixels in host memory.
struct ImageAsset::ImageBuffer {
    OIIO::ImageBuf image;
    Vec2u extent;
    std::uint8_t componentCount;
    ImageComponentType componentType;

    /// Color space of the pixel color components.
    ImageColorSpace colorSpace;

    [[nodiscard]] std::span<std::byte const> getPixels() const noexcept {
        auto const size = static_cast<std::size_t>(image.spec().image_bytes());
        return {static_cast<std::byte const *>(image.localpixels()), size};
    }
};

/// \brief OIIO texture handle and image layout for one ImageAsset.
struct ImageAsset::pImpl {
    pImpl(
        std::shared_ptr<OIIO::TextureSystem> textureSystem, std::filesystem::path const &path,
        std::string_view colorSpace
    );

    std::shared_ptr<OIIO::TextureSystem> textureSystem;

    /// \c textureSystem owns this handle.
    OIIO::TextureSystem::TextureHandle *textureHandle{};
    OIIO::ustring filename;

    /// OIIO color transform used by texture lookups.
    int colorTransformId{};

    /// Color space of the color components that \c readMipChain returns.
    ImageColorSpace storedColorSpace{ImageColorSpace::Linear};

    /// Image layout exposed by ImageAsset.
    Vec2u extent;
    std::uint8_t componentCount;
    ImageComponentType componentType;
    ImageComponentMapping defaultComponentMapping;

    /// File layout used by OIIO texture lookups.
    std::uint8_t orientation;
    std::uint8_t fileComponentCount;
};

/// \brief OIIO texture systems shared by one ImageAssetPool.
struct ImageAssetPool::pImpl {
    pImpl();

    /// OIIO applies one component type policy to every image in a cache. The
    /// converting system uses Float32 to preserve converted values before filtering.
    /// The linear system keeps UNorm8 and Float16 tiles compact.
    std::shared_ptr<OIIO::TextureSystem> linearTextureSystem;
    std::shared_ptr<OIIO::TextureSystem> convertingTextureSystem;
};
} // namespace flux

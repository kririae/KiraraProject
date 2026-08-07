#pragma once

/// \file
/// \brief OIIO-backed image implementation.

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagecache.h>

#include <memory>
#include <optional>
#include <span>

#include "flux/Scene/ImageAsset.h"

namespace flux {
struct ImageAsset::ImageBuffer {
    OIIO::ImageBuf storage;
    Vec2u extent;
    std::uint8_t componentCount;
    ImageComponentType componentType;

    /// Color transform left for the backend.
    ImageTransform pendingTransform;

    [[nodiscard]] std::span<std::byte const> getPixels() const noexcept {
        auto const size = static_cast<std::size_t>(storage.spec().image_bytes());
        return {static_cast<std::byte const *>(storage.localpixels()), size};
    }
};

struct ImageAsset::pImpl {
    pImpl(
        std::shared_ptr<OIIO::ImageCache> imageCache, std::filesystem::path const &path,
        ImageTransform transform
    );

    std::shared_ptr<OIIO::ImageCache> imageCache;
    OIIO::ImageSpec readConfig;
    OIIO::ustring filename;

    /// Metadata available before the pixels are read.
    Vec2u extent;
    std::uint8_t componentCount;
    ImageComponentType componentType;
    ImageComponentMapping defaultComponentMapping;

    /// Source component mapping used when read() creates RGBA storage.
    std::optional<ImageComponentMapping> sourceToRGBA;

    /// Color transform requested by the asset. read() applies it or returns it
    /// to the backend.
    ImageTransform requestedTransform;
};

struct ImageAssetPool::pImpl {
    pImpl();

    std::shared_ptr<OIIO::ImageCache> imageCache;
};
} // namespace flux

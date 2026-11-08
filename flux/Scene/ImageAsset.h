#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "flux/Core/ConcurrentPool.h"
#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/IO/Image.h"
#include "flux/Scene/ImageComponentMapping.h"

namespace flux {
class EmbreeImageTexturePool;
class OptixImagePool;

/// \brief Color space of the image color components.
///
/// Alpha keeps its stored value.
enum class ImageColorSpace : std::uint8_t {
    Linear,
    SRGB,
};

/// \brief OCIO name of the color space that ImageAsset converts every image to.
inline constexpr std::string_view linearRec709{"lin_rec709"};

/// \brief OCIO name of the color space that an 8-bit image of this color space stays stored in.
inline constexpr std::string_view srgbTexture{"srgb_texture"};

/// \brief Image file and the OCIO name of the color space of its color components.
///
/// An empty name is \c linearRec709. A name that the OCIO config does not know becomes
/// \c linearRec709, and the pool warns.
struct ImageAssetSpec {
    std::filesystem::path path;
    std::string colorSpace;

    [[nodiscard]] friend bool operator==(ImageAssetSpec const &, ImageAssetSpec const &) = default;
};

/// \brief Image file shared by image textures.
///
/// A well-formed image is flat and 2D, with one to four channels, and its data
/// window is its display window at the origin. An image of a color space other than
/// \c linearRec709 has three or four channels.
///
/// The color components are \c linearRec709. They are converted from the color space of the spec
/// when the image is read, before filtering and before the mip levels. An 8-bit image of
/// \c srgbTexture is the exception, and reads back sRGB encoded.
///
/// By position, the channels are a value, a value and alpha, RGB, or RGBA.
/// Alpha is straight. The file must not change while the asset is in use.
class ImageAsset final : public RefCountedBase<ImageAsset> {
    friend class EmbreeImageTexturePool;
    friend class ImageAssetPool;
    friend class OptixImagePool;

public:
    ~ImageAsset();

    /// \brief Returns the ID of this asset. IDs only increase, so no other asset shares it.
    [[nodiscard]] std::size_t getId() const noexcept { return id_; }

    /// Extent after applying the file orientation.
    [[nodiscard]] Vec2u getExtent() const noexcept;

    /// Number of components exposed to image textures: one, two, or four.
    [[nodiscard]] std::uint8_t getComponentCount() const noexcept;

    /// Component type used when the image is read into host memory.
    [[nodiscard]] ImageComponentType getComponentType() const noexcept;

    /// Component mapping used when ImageTexture does not provide one.
    ///
    /// One component is a value with alpha one. Two components are a value and
    /// alpha. Four components map to themselves.
    [[nodiscard]] ImageComponentMapping getDefaultComponentMapping() const noexcept;

    /// \brief Returns whether this is the magenta placeholder of a file that failed to load.
    [[nodiscard]] bool isPlaceholder() const noexcept { return placeholder_; }

private:
    struct ImageBuffer;
    struct pImpl;

    ImageAsset(std::unique_ptr<pImpl> pImpl, bool placeholder) noexcept;

    /// \brief Reads the mip chain into bottom-up host buffers.
    ///
    /// OIIO converts the color components before it generates the mip levels.
    [[nodiscard]] std::vector<ImageBuffer> readMipChain() const;

    std::unique_ptr<pImpl> pImpl_;
    std::size_t id_;
    bool placeholder_;
};

/// \brief Shares ImageAsset objects by canonical path and color space.
///
/// A file that is missing, unreadable, or not a well-formed image gets a 1x1
/// magenta placeholder asset, and the pool warns once for the spec. An empty path means no image,
/// so it gets the placeholder with no warning. Concurrent calls share one creation for each
/// canonical path and color space.
class ImageAssetPool {
public:
    ImageAssetPool();
    ~ImageAssetPool();

    /// \brief Returns the shared asset for \p spec.
    [[nodiscard]] Ref<ImageAsset const> getOrCreate(ImageAssetSpec const &spec);

    /// \brief Returns the color space that the header of the file at \p path implies.
    ///
    /// An image of 8-bit components with at least three channels is \c srgbTexture. Any other
    /// image, and a file with no readable header, has no color space, which is the empty name.
    [[nodiscard]] std::string chooseColorSpace(std::filesystem::path const &path) const;

    /// \brief Drops every asset that only this pool references.
    ///
    /// \pre No call to \c getOrCreate runs concurrently.
    void collectGarbage();

    /// \brief Returns the number of assets in the pool.
    [[nodiscard]] std::size_t size() const { return assets_.size(); }

private:
    struct pImpl;

    struct SpecHash {
        [[nodiscard]] std::size_t operator()(ImageAssetSpec const &spec) const noexcept {
            auto const pathHash = std::filesystem::hash_value(spec.path);
            auto const colorSpaceHash = std::hash<std::string>{}(spec.colorSpace);
            return pathHash * 31U + colorSpaceHash;
        }
    };

    ConcurrentPool<ImageAssetSpec, Ref<ImageAsset const>, SpecHash> assets_;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux

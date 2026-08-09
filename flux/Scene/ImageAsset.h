#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "flux/Core/ConcurrentPool.h"
#include "flux/Core/Math.h"
#include "flux/Core/Object.h"
#include "flux/IO/Image.h"
#include "kira/Compiler.h"

namespace flux {
class EmbreeImageTexturePool;
class OptixImageTexturePool;

/// \brief Color space of the image color components.
///
/// Alpha keeps its stored value.
enum class ImageColorSpace : std::uint8_t {
    Linear,
    SRGB,
};

/// \brief Input for one component of a texture value.
enum class ImageComponentSource : std::uint8_t {
    X,
    Y,
    Z,
    W,
    Zero,
    One,
};

/// \brief Maps image components to a four-component texture value.
struct ImageComponentMapping {
    ImageComponentSource r{ImageComponentSource::X};
    ImageComponentSource g{ImageComponentSource::Y};
    ImageComponentSource b{ImageComponentSource::Z};
    ImageComponentSource a{ImageComponentSource::W};

    [[nodiscard]] constexpr bool isValid(std::uint8_t componentCount) const noexcept {
        auto const sourceIsValid = [componentCount](ImageComponentSource source) {
            auto const value = static_cast<std::uint8_t>(source);
            return value < componentCount || source == ImageComponentSource::Zero ||
                   source == ImageComponentSource::One;
        };
        return sourceIsValid(r) && sourceIsValid(g) && sourceIsValid(b) && sourceIsValid(a);
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec4f apply(Vec4f const &value) const noexcept {
        auto const select = [&](ImageComponentSource source) {
            if (source == ImageComponentSource::X)
                return value.x();
            if (source == ImageComponentSource::Y)
                return value.y();
            if (source == ImageComponentSource::Z)
                return value.z();
            if (source == ImageComponentSource::W)
                return value.w();
            if (source == ImageComponentSource::Zero)
                return 0.0F;
            return 1.0F;
        };
        return {select(r), select(g), select(b), select(a)};
    }

    [[nodiscard]] friend bool
    operator==(ImageComponentMapping const &, ImageComponentMapping const &) = default;
};

/// \brief Image file and its color space.
struct ImageAssetRequest {
    std::filesystem::path path;
    ImageColorSpace colorSpace{ImageColorSpace::Linear};

    [[nodiscard]] friend bool
    operator==(ImageAssetRequest const &, ImageAssetRequest const &) = default;
};

/// \brief Image file shared by image textures.
///
/// Source pixels must use straight alpha. Data and display windows must match
/// and start at the origin. The source file must remain available and unchanged
/// while the asset is in use.
class ImageAsset final : public RefCountedBase<ImageAsset> {
    friend class EmbreeImageTexturePool;
    friend class ImageAssetPool;
    friend class OptixImageTexturePool;

public:
    ~ImageAsset();

    /// Extent after applying the file orientation.
    [[nodiscard]] Vec2u getExtent() const noexcept;

    /// Number of components exposed to image textures: one, two, or four.
    [[nodiscard]] std::uint8_t getComponentCount() const noexcept;

    /// Component type used when the image is read into host memory.
    [[nodiscard]] ImageComponentType getComponentType() const noexcept;

    /// Component mapping used when ImageTexture does not provide one.
    [[nodiscard]] ImageComponentMapping getDefaultComponentMapping() const noexcept;

private:
    struct ImageBuffer;
    struct pImpl;

    explicit ImageAsset(std::unique_ptr<pImpl> pImpl) noexcept;

    /// \brief Reads the complete image into host memory.
    ///
    /// The buffer has one, two, or four components in bottom-up order.
    [[nodiscard]] ImageBuffer read() const;

    std::unique_ptr<pImpl> pImpl_;
};

/// \brief Shares ImageAsset objects by canonical path and color space.
///
/// The pool validates image metadata before adding an asset.
class ImageAssetPool {
public:
    ImageAssetPool();
    ~ImageAssetPool();

    /// \brief Returns the shared asset for \p request.
    [[nodiscard]] Ref<ImageAsset const> getOrCreate(ImageAssetRequest const &request);

private:
    struct pImpl;

    struct RequestHash {
        [[nodiscard]] std::size_t operator()(ImageAssetRequest const &request) const noexcept {
            auto const pathHash = std::filesystem::hash_value(request.path);
            auto const colorSpace = static_cast<std::size_t>(request.colorSpace);
            return pathHash * 31U + colorSpace;
        }
    };

    ConcurrentPool<ImageAssetRequest, Ref<ImageAsset const>, RequestHash> assets_;
    std::unique_ptr<pImpl> pImpl_;
};
} // namespace flux

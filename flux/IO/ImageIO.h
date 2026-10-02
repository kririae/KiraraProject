#pragma once

#include <filesystem>
#include <span>
#include <string_view>
#include <type_traits>

#include "flux/IO/Image.h"
#include "flux/Scene/Film.h"

namespace flux {
/// \brief File layout used by \c writeImage.
struct ImageWriteOptions {
    ImageComponentType outputComponentType{ImageComponentType::Float32};

    /// Component names. A component named A is stored as straight alpha.
    std::span<std::string_view const> componentNames{};

    /// TIFF/EXIF orientation in the range [1, 8].
    int orientation{1};
};

/// \brief Writes an image using the codec selected by \p path.
///
/// Creates missing parent directories. ImageView::componentType describes the
/// input bytes; ImageWriteOptions::outputComponentType selects the stored
/// type. The write completes before this function returns.
void writeImage(std::filesystem::path const &path, ImageView image, ImageWriteOptions options);

namespace detail {
void writeExr(std::filesystem::path const &path, ImageView image);
} // namespace detail

/// \brief Writes one downloaded film channel to a 32-bit float EXR image.
template <typename Channel> void writeImage(std::filesystem::path const &path, Film const &film) {
    using Value = typename Channel::Value;
    auto const pixels = film.template getChannel<Channel>();

    if constexpr (std::is_same_v<Value, float>) {
        detail::writeExr(
            path, {
                      .pixels = std::as_bytes(pixels),
                      .extent = {film.getWidth(), film.getHeight()},
                      .componentType = ImageComponentType::Float32,
                      .componentCount = 1,
                  }
        );
    } else if constexpr (std::is_same_v<Value, Vec3f>) {
        static_assert(sizeof(Vec3f) == 3 * sizeof(float));
        static_assert(std::is_trivially_copyable_v<Vec3f>);
        detail::writeExr(
            path, {
                      .pixels = std::as_bytes(pixels),
                      .extent = {film.getWidth(), film.getHeight()},
                      .componentType = ImageComponentType::Float32,
                      .componentCount = 3,
                  }
        );
    } else {
        static_assert(detail::AlwaysFalse<Channel>, "Unsupported image channel value type");
    }
}
} // namespace flux

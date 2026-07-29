#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <type_traits>

#include "flux/Scene/Film.h"

namespace flux {
namespace detail {
/// \brief Writes tightly packed 32-bit float pixels to an EXR file.
void writeExr(
    std::filesystem::path const &path, std::uint32_t width, std::uint32_t height,
    void const *pixels, std::size_t pixelCount, int components
);
} // namespace detail

/// \brief Writes one downloaded film channel to a 32-bit float EXR image.
///
/// Creates missing parent directories and preserves pixel values and film
/// raster order.
///
/// \tparam Channel Film channel with \c float or \c Vec3f values.
/// \throw kira::Anyhow If Film has no downloaded \c Channel values, if \p path
/// uses another extension, or if OpenImageIO reports an error.
/// \throw std::filesystem::filesystem_error If std::filesystem cannot create a
/// parent directory.
template <typename Channel> void writeImage(std::filesystem::path const &path, Film const &film) {
    using Value = typename Channel::Value;
    auto const pixels = film.template getChannel<Channel>();

    if constexpr (std::is_same_v<Value, float>) {
        detail::writeExr(path, film.getWidth(), film.getHeight(), pixels.data(), pixels.size(), 1);
    } else if constexpr (std::is_same_v<Value, Vec3f>) {
        static_assert(sizeof(Vec3f) == 3 * sizeof(float));
        static_assert(std::is_trivially_copyable_v<Vec3f>);
        detail::writeExr(path, film.getWidth(), film.getHeight(), pixels.data(), pixels.size(), 3);
    } else {
        static_assert(detail::AlwaysFalse<Channel>, "Unsupported image channel value type");
    }
}
} // namespace flux

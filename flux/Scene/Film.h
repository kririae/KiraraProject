#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "flux/Core/Math.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Describes the channelized target owned by a render product.
///
/// Device storage belongs to the active renderer backend. The host object
/// stores only the target shape until readback support is introduced.
class Film final {
public:
    /// \brief Device view assembled by the active renderer backend.
    struct DeviceImpl;

    /// \brief Creates a film with the given nonzero dimensions.
    ///
    /// \throw kira::Anyhow If either dimension is zero.
    Film(std::uint32_t width, std::uint32_t height);

    /// \brief Returns the image width in pixels.
    [[nodiscard]] std::uint32_t getWidth() const noexcept { return width_; }

    /// \brief Returns the image height in pixels.
    [[nodiscard]] std::uint32_t getHeight() const noexcept { return height_; }

    /// \brief Changes the target shape.
    ///
    /// Backend storage is resized lazily on the next render.
    /// \throw kira::Anyhow If either dimension is zero.
    void setResolution(std::uint32_t width, std::uint32_t height);

private:
    std::uint32_t width_;
    std::uint32_t height_;
};

/// \brief Device view of the channels in a film.
struct Film::DeviceImpl {
    /// Image width shared by all present channels.
    std::uint32_t width{};

    /// Image height shared by all present channels.
    std::uint32_t height{};

    /// World-space geometric normal channel.
    Vec3f *normal{};

public:
    /// \brief Atomically adds \p value to the normal channel at \p pixel.
    ///
    /// Concurrent samples of one pixel may call this function.
    /// \pre The normal channel is present and \p pixel is in range.
    KIRA_DEVICE inline void accumulateNormal(Vec2u const &pixel, Vec3f const &value) const noexcept;
};

static_assert(std::is_standard_layout_v<Film::DeviceImpl>);
static_assert(std::is_trivially_copyable_v<Film::DeviceImpl>);

namespace optix {
/// Device representation of a film.
using Film = ::flux::Film::DeviceImpl;
} // namespace optix
} // namespace flux

#pragma once

/// \file flux/Scene/ImageComponentMapping.h
/// \brief The mapping from image components to texture components.
///
/// Includes only headers that NVRTC compiles: no kira and no standard library header.

namespace flux {
/// \brief Input for one component of a texture value.
enum class ImageComponentSource : unsigned char {
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

    [[nodiscard]] constexpr bool isValid(unsigned char componentCount) const noexcept {
        auto const sourceIsValid = [componentCount](ImageComponentSource source) {
            auto const value = static_cast<unsigned char>(source);
            return value < componentCount || source == ImageComponentSource::Zero ||
                   source == ImageComponentSource::One;
        };
        return sourceIsValid(r) && sourceIsValid(g) && sourceIsValid(b) && sourceIsValid(a);
    }

    /// \brief Returns the sample (\p x, \p y, \p z, \p w) after the mapping.
    template <typename Vec4>
    [[nodiscard]] constexpr Vec4 apply(float x, float y, float z, float w) const noexcept {
        return Vec4{
            select(r, x, y, z, w), select(g, x, y, z, w), select(b, x, y, z, w),
            select(a, x, y, z, w)
        };
    }

    [[nodiscard]] friend constexpr bool
    operator==(ImageComponentMapping const &, ImageComponentMapping const &) = default;

private:
    [[nodiscard]] static constexpr float
    select(ImageComponentSource source, float x, float y, float z, float w) noexcept {
        if (source == ImageComponentSource::X)
            return x;
        if (source == ImageComponentSource::Y)
            return y;
        if (source == ImageComponentSource::Z)
            return z;
        if (source == ImageComponentSource::W)
            return w;
        if (source == ImageComponentSource::Zero)
            return 0.0F;
        return 1.0F;
    }
};
} // namespace flux

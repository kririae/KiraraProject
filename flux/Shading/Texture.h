#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/ImageAsset.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
enum class TextureType : std::uint8_t {
    Constant,
    Image,
    Count,
};

enum class ImageTextureAddressMode : std::uint8_t {
    Wrap,
    Clamp,
    Mirror,
    Border,
};

enum class ImageTextureFilterMode : std::uint8_t {
    Linear,
    Point,
};

/// \brief Defines all texture evaluation methods for \p Derived.
///
/// \p Derived implements \c eval4f_. It may also implement \c eval1f_,
/// \c eval2f_, or \c eval3f_. Missing methods use a method with more components.
template <typename Derived> class TextureMixin {
private:
    [[nodiscard]] KIRA_HOST_DEVICE Derived const &derived_() const noexcept {
        return *static_cast<Derived const *>(this);
    }

public:
    [[nodiscard]] KIRA_HOST_DEVICE float eval1f(SurfaceInteraction const &isect) const noexcept {
        if constexpr (requires(Derived const &texture) {
                          { texture.eval1f_(isect) } -> std::same_as<float>;
                      })
            return derived_().eval1f_(isect);
        else
            return eval2f(isect).x();
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec2f eval2f(SurfaceInteraction const &isect) const noexcept {
        if constexpr (requires(Derived const &texture) {
                          { texture.eval2f_(isect) } -> std::same_as<Vec2f>;
                      })
            return derived_().eval2f_(isect);
        else {
            auto const value = eval3f(isect);
            return {value.x(), value.y()};
        }
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec3f eval3f(SurfaceInteraction const &isect) const noexcept {
        if constexpr (requires(Derived const &texture) {
                          { texture.eval3f_(isect) } -> std::same_as<Vec3f>;
                      })
            return derived_().eval3f_(isect);
        else {
            auto const value = eval4f(isect);
            return {value.x(), value.y(), value.z()};
        }
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec4f eval4f(SurfaceInteraction const &isect) const noexcept {
        return derived_().eval4f_(isect);
    }
};

/// \brief Host-side base for texture data.
class Texture : public RenderObject {
    friend class TXContext;

protected:
    explicit Texture(TXContext &tx) : RenderObject(tx) {}

public:
    struct Impl;

    [[nodiscard]] virtual Impl getImpl() const = 0;

    /// \brief Creates and registers the texture stored at \p name.
    ///
    /// Scalars broadcast to RGB. Colors keep their channels. Inline tables
    /// select a concrete texture type. The resulting texture belongs to \p tx.
    ///
    /// \pre \p parent contains \p name.
    [[nodiscard]] static Ref<Texture const>
    resolve(TXContext &tx, kira::Properties const &parent, std::string_view name);

    /// \brief Creates and registers the texture stored at \p name with a scalar fallback.
    ///
    /// The property follows the conversion rules of the required overload.
    /// \param defaultValue Scalar fallback stored in a constant texture.
    [[nodiscard]] static Ref<Texture const> resolve(
        TXContext &tx, kira::Properties const &parent, std::string_view name, float defaultValue
    );

    /// \brief Creates and registers the texture stored at \p name with a color fallback.
    ///
    /// The property follows the conversion rules of the required overload.
    /// \param defaultValue Color fallback stored in a constant texture.
    [[nodiscard]] static Ref<Texture const> resolve(
        TXContext &tx, kira::Properties const &parent, std::string_view name,
        Spectrum const &defaultValue
    );

private:
    [[nodiscard]] static Ref<Texture> create(TXContext &tx, kira::Properties const &props);
};

/// \brief Texture with one RGB value at every surface interaction.
class ConstantTexture final : public Texture {
    friend class TXContext;

public:
    struct Impl;

    [[nodiscard]] Spectrum const &getValue() const noexcept { return value_; }
    [[nodiscard]] Texture::Impl getImpl() const override;

private:
    ConstantTexture(TXContext &tx, kira::Properties const &props);

    Spectrum value_;
};

/// \brief Image-based texture.
///
/// Color components are converted before filtering and component mapping.
/// Alpha keeps its stored value.
class ImageTexture final : public Texture {
    friend class TXContext;

public:
    struct Impl;

    [[nodiscard]] Ref<ImageAsset const> const &getImageAsset() const noexcept {
        return imageAsset_;
    }
    [[nodiscard]] ImageTextureAddressMode getAddressMode() const noexcept { return addressMode_; }
    [[nodiscard]] ImageTextureFilterMode getFilterMode() const noexcept { return filterMode_; }
    [[nodiscard]] ImageComponentMapping getComponentMapping() const noexcept {
        return componentMapping_;
    }
    [[nodiscard]] Texture::Impl getImpl() const override;

private:
    ImageTexture(TXContext &tx, kira::Properties const &props);

    Ref<ImageAsset const> imageAsset_;
    ImageTextureAddressMode addressMode_;
    ImageTextureFilterMode filterMode_;
    ImageComponentMapping componentMapping_;
};

struct ConstantTexture::Impl : TextureMixin<Impl> {
    KIRA_HOST_DEVICE explicit Impl(Spectrum const &value = {}) noexcept : value(value) {}

    Spectrum value;

    [[nodiscard]] KIRA_HOST_DEVICE Vec4f eval4f_(SurfaceInteraction const &) const noexcept {
        return {value.x(), value.y(), value.z(), 1.0F};
    }
};

struct ImageTexture::Impl {
    std::uint32_t imageTextureIndex;
};

struct Texture::Impl {
    TextureType type;

    union Storage {
        ConstantTexture::Impl constant;
        ImageTexture::Impl image;
    } storage;

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE float eval1f(SurfaceInteraction const &isect) const noexcept {
        return eval4f<Evaluator>(isect).x();
    }

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Vec2f eval2f(SurfaceInteraction const &isect) const noexcept {
        auto const value = eval4f<Evaluator>(isect);
        return {value.x(), value.y()};
    }

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Vec3f eval3f(SurfaceInteraction const &isect) const noexcept {
        auto const value = eval4f<Evaluator>(isect);
        return {value.x(), value.y(), value.z()};
    }

    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Vec4f eval4f(SurfaceInteraction const &isect) const noexcept {
        switch (type) {
        case TextureType::Constant: return storage.constant.eval4f(isect);
        case TextureType::Image:
            return Evaluator::eval4f(storage.image.imageTextureIndex, isect.uv);
        case TextureType::Count: break;
        }
        KIRA_UNREACHABLE();
    }
};

static_assert(std::is_standard_layout_v<ConstantTexture::Impl>);
static_assert(std::is_trivially_copyable_v<ConstantTexture::Impl>);
static_assert(std::is_standard_layout_v<ImageTexture::Impl>);
static_assert(std::is_trivially_copyable_v<ImageTexture::Impl>);
static_assert(std::is_standard_layout_v<Texture::Impl>);
static_assert(std::is_trivially_copyable_v<Texture::Impl>);
} // namespace flux

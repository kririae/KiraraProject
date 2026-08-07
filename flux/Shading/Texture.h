#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
enum class TextureType : std::uint8_t {
    Constant,
    Count,
};

/// \brief Defines all texture evaluation methods for \p Derived.
///
/// \p Derived provides \c eval4f_. It may provide \c eval1f_ and \c eval3f_
/// for direct evaluation. \c eval1f prefers \c eval1f_, \c eval3f_, and
/// \c eval4f_ in that order. \c eval3f prefers \c eval3f_, then \c eval4f_.
/// \c eval4f forwards to \c eval4f_.
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
        else if constexpr (requires(Derived const &texture) {
                               { texture.eval3f_(isect) } -> std::same_as<Vec3f>;
                           })
            return derived_().eval3f_(isect).x();
        else
            return derived_().eval4f_(isect).x();
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec3f eval3f(SurfaceInteraction const &isect) const noexcept {
        if constexpr (requires(Derived const &texture) {
                          { texture.eval3f_(isect) } -> std::same_as<Vec3f>;
                      })
            return derived_().eval3f_(isect);
        else {
            auto const value = derived_().eval4f_(isect);
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

struct ConstantTexture::Impl : TextureMixin<Impl> {
    KIRA_HOST_DEVICE explicit Impl(Spectrum const &value = {}) noexcept : value(value) {}

    Spectrum value;

    [[nodiscard]] KIRA_HOST_DEVICE Vec4f eval4f_(SurfaceInteraction const &) const noexcept {
        return {value.x(), value.y(), value.z(), 1.0F};
    }
};

struct Texture::Impl {
    TextureType type;

    union Storage {
        ConstantTexture::Impl constant;
    } storage;

    template <typename Function>
    [[nodiscard]] KIRA_HOST_DEVICE decltype(auto) dispatch(Function const &function) const {
        switch (type) {
        case TextureType::Constant: return function(storage.constant);
        case TextureType::Count: break;
        }
        KIRA_UNREACHABLE();
    }

    [[nodiscard]] KIRA_HOST_DEVICE float eval1f(SurfaceInteraction const &isect) const noexcept {
        return dispatch([&](auto const &texture) { return texture.eval1f(isect); });
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec3f eval3f(SurfaceInteraction const &isect) const noexcept {
        return dispatch([&](auto const &texture) { return texture.eval3f(isect); });
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec4f eval4f(SurfaceInteraction const &isect) const noexcept {
        return dispatch([&](auto const &texture) { return texture.eval4f(isect); });
    }
};

static_assert(std::is_standard_layout_v<ConstantTexture::Impl>);
static_assert(std::is_trivially_copyable_v<ConstantTexture::Impl>);
static_assert(std::is_standard_layout_v<Texture::Impl>);
static_assert(std::is_trivially_copyable_v<Texture::Impl>);
} // namespace flux

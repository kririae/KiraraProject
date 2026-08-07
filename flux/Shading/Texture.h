#pragma once

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

template <typename Derived> class TextureMixin {
private:
    [[nodiscard]] KIRA_HOST_DEVICE Derived const &derived_() const noexcept {
        return *static_cast<Derived const *>(this);
    }

public:
    [[nodiscard]] KIRA_HOST_DEVICE float eval1f(SurfaceInteraction const &isect) const noexcept {
        return derived_().eval4f(isect).x();
    }

    [[nodiscard]] KIRA_HOST_DEVICE Vec3f eval3f(SurfaceInteraction const &isect) const noexcept {
        auto const value = derived_().eval4f(isect);
        return {value.x(), value.y(), value.z()};
    }
};

class Texture : public RenderObject {
    friend class TXContext;

protected:
    explicit Texture(TXContext &tx) : RenderObject(tx) {}

public:
    struct Impl;

    [[nodiscard]] virtual Impl getImpl() const = 0;

    [[nodiscard]] static Ref<Texture const>
    resolve(TXContext &tx, kira::Properties const &parent, std::string_view name);

    [[nodiscard]] static Ref<Texture const> resolve(
        TXContext &tx, kira::Properties const &parent, std::string_view name, float defaultValue
    );

    [[nodiscard]] static Ref<Texture const> resolve(
        TXContext &tx, kira::Properties const &parent, std::string_view name,
        Spectrum const &defaultValue
    );

private:
    [[nodiscard]] static Ref<Texture> create(TXContext &tx, kira::Properties const &props);
};

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

    [[nodiscard]] KIRA_HOST_DEVICE Vec4f eval4f(SurfaceInteraction const &) const noexcept {
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

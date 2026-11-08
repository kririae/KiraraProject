#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "flux/Core/EnumFlags.h"
#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Scene/ShaderAsset.h"
#include "flux/Shading/Interaction.h"
#include "flux/Shading/Texture.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Identifies a concrete EDF implementation.
enum class EDFType : std::uint8_t {
    Constant,
    Shader,
    Count,
};

/// \brief Bit mask of concrete EDF implementations.
using EDFTypeMask = std::uint32_t;

static_assert(static_cast<std::uint8_t>(EDFType::Count) < std::numeric_limits<EDFTypeMask>::digits);

[[nodiscard]] constexpr EDFTypeMask edfTypeBit(EDFType type) noexcept {
    return EDFTypeMask{1} << static_cast<std::uint8_t>(type);
}

/// \brief World-space surface orientation used to evaluate emission.
///
/// \c wo points away from the surface in world space.
struct EDFQuery {
    Vec3f geometricNormal{};
    Vec3f wo{};
};

/// \brief Host-side base for surface emission models.
class EDF : public RenderObject {
    friend class TXContext;

public:
    struct Impl;
    struct Dispatcher;

    /// Kind whose index map holds EDFs.
    static constexpr IndexedKind indexedKind = IndexedKind::EDF;

    /// \brief Returns the concrete implementation type.
    [[nodiscard]] EDFType getType() const noexcept { return type_; }

    [[nodiscard]] std::optional<IndexedKind> getIndexedKind() const noexcept override {
        return indexedKind;
    }

    [[nodiscard]] virtual Impl getImpl() const = 0;
    /// \brief Estimates emitted luminance for light selection.
    ///
    /// Returns 0 only if the EDF emits nothing, because a primitive with such an EDF is no light
    /// and never reaches a device.
    [[nodiscard]] virtual float estimateLuminance() const noexcept = 0;

protected:
    /// \brief Creates an EDF of \p type in \p tx.
    EDF(TXContext &tx, EDFType type) : RenderObject(tx), type_(type) {}

private:
    [[nodiscard]] static Ref<EDF> create(TXContext &tx, kira::Properties const &props);

    EDFType type_;
};

/// \brief One-sided constant radiance emission.
///
/// \par Properties
/// - \c radiance: optional nonnegative RGB radiance; defaults to one.
class ConstantEDF final : public EDF {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Properties of a constant EDF that a setter can change.
    ///
    /// One bit per setter. A setter records its bit only when the value changes.
    enum class DirtyBits : std::uint32_t {
        None = 0,
        Radiance = 1U << 0U,
    };

    [[nodiscard]] Spectrum const &getRadiance() const noexcept { return radiance_; }
    void setRadiance(Spectrum const &radiance);

    [[nodiscard]] EDF::Impl getImpl() const override;
    [[nodiscard]] float estimateLuminance() const noexcept override;

private:
    ConstantEDF(TXContext &tx, kira::Properties const &props);

    Spectrum radiance_{1.0F, 1.0F, 1.0F};
};

/// \brief The EDF that a shader gives.
///
/// Its emission is two-sided. Texture k is the texture that slot k of the source samples.
class ShaderEDF final : public EDF {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Parameters of a shader EDF that a setter can change.
    enum class DirtyBits : std::uint32_t {
        None = 0,
        Shader = 1U << 0U,
    };

    [[nodiscard]] Ref<ShaderAsset const> const &getAsset() const noexcept { return asset_; }

    [[nodiscard]] std::vector<std::uint32_t> const &getWords() const noexcept { return words_; }

    [[nodiscard]] std::vector<Ref<ImageTexture const>> const &getTextures() const noexcept {
        return textures_;
    }

    /// \brief Replaces the shader of the EDF, with the words and textures it reads.
    ///
    /// \throw std::invalid_argument If a texture is null or not of the context of the EDF.
    void setShader(
        ShaderAssetSpec const &spec, std::vector<std::uint32_t> words,
        std::vector<Ref<ImageTexture const>> textures
    );

    [[nodiscard]] EDF::Impl getImpl() const override;

    /// \brief Returns false only if the source of the shader provably gives no emission.
    [[nodiscard]] bool mayEmit() const noexcept { return asset_->getSpec().mayEmit; }

    /// Does not estimate the emission, so every shader EDF that may emit reports luminance 1.
    [[nodiscard]] float estimateLuminance() const noexcept override {
        return mayEmit() ? 1.0F : 0.0F;
    }

private:
    /// \brief Creates an EDF of the shader that \p spec describes.
    ///
    /// \throw std::invalid_argument if a texture is null, or is not an object of the context or
    ///        of \p tx.
    ShaderEDF(
        TXContext &tx, ShaderAssetSpec const &spec, std::vector<std::uint32_t> words,
        std::vector<Ref<ImageTexture const>> textures = {}
    );

    Ref<ShaderAsset const> asset_;
    std::vector<std::uint32_t> words_;
    std::vector<Ref<ImageTexture const>> textures_;
};

struct ConstantEDF::Impl {
    Spectrum radiance;

    [[nodiscard]] KIRA_HOST_DEVICE Spectrum evaluate(EDFQuery const &query) const noexcept {
        return query.geometricNormal.dot(query.wo) > 0.0F ? radiance : Spectrum{};
    }
};

/// \brief Shader emission implementation.
struct ShaderEDF::Impl {
    /// The EDF's own index in the context.
    std::uint32_t index;
};

/// \brief Stores one EDF implementation.
///
/// A value-initialized Impl is the empty EDF: a valid constant EDF with zero
/// radiance, which emits nothing.
struct EDF::Impl {
    EDFType type;

    union Storage {
        ConstantEDF::Impl constant;
        ShaderEDF::Impl shader;
    } storage;
};

/// \brief Evaluates one EDF.
struct EDF::Dispatcher {
    /// Complete set of implementations that may be passed to \c evaluate. A set without
    /// \c EDFType::Shader makes every call a constant evaluation.
    EDFTypeMask types;

public:
    /// \brief Returns the radiance that the surface at \p isect emits toward \p wo.
    ///
    /// \p wo points away from the surface in world space.
    ///
    /// \pre \p wo is normalized.
    /// \pre \c types contains \p edf's implementation type, or is empty.
    ///
    /// A shader EDF runs through \c Evaluator::evaluate, which takes the EDF index of the shader
    /// EDF, the uv and the shading normal of \p isect, and \p wo.
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE Spectrum evaluate(
        EDF::Impl const &edf, SurfaceInteraction const &isect, Vec3f const &wo
    ) const noexcept {
        auto selectedType = edf.type;
        if (types == edfTypeBit(EDFType::Shader))
            selectedType = EDFType::Shader;
        else if ((types & edfTypeBit(EDFType::Shader)) == 0)
            selectedType = EDFType::Constant;

        if (selectedType == EDFType::Constant)
            return edf.storage.constant.evaluate(
                {.geometricNormal = isect.geometricNormal, .wo = wo}
            );
        else if (selectedType == EDFType::Shader)
            return Evaluator::evaluate(edf.storage.shader.index, isect.uv, isect.shadingNormal, wo);
        KIRA_UNREACHABLE();
    }
};

template <> inline constexpr bool isEnumFlags<ConstantEDF::DirtyBits> = true;
template <> inline constexpr bool isEnumFlags<ShaderEDF::DirtyBits> = true;

static_assert(std::is_standard_layout_v<ConstantEDF::Impl>);
static_assert(std::is_trivially_copyable_v<ConstantEDF::Impl>);
static_assert(std::is_standard_layout_v<ShaderEDF::Impl>);
static_assert(std::is_trivially_copyable_v<ShaderEDF::Impl>);
static_assert(std::is_trivially_copyable_v<EDF::Impl>);
static_assert(std::is_standard_layout_v<EDF::Dispatcher>);
static_assert(std::is_trivially_copyable_v<EDF::Dispatcher>);
} // namespace flux

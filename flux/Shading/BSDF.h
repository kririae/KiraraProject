#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "flux/Core/EnumFlags.h"
#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Scene/ShaderAsset.h"
#include "flux/Shading/Interaction.h"
#include "flux/Shading/Texture.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Identifies a concrete BSDF implementation.
enum class BSDFType : std::uint8_t {
    Diffuse,
    Principled,
    Shader,
    Count,
};

/// \brief Bit mask of concrete BSDF implementations.
using BSDFTypeMask = std::uint32_t;

static_assert(
    static_cast<std::uint8_t>(BSDFType::Count) < std::numeric_limits<BSDFTypeMask>::digits
);

[[nodiscard]] constexpr BSDFTypeMask bsdfTypeBit(BSDFType type) noexcept {
    return BSDFTypeMask{1} << static_cast<std::uint8_t>(type);
}

inline constexpr BSDFTypeMask allBSDFTypes = bsdfTypeBit(BSDFType::Count) - 1;

/// \brief Identifies the lobe represented by a BSDF sample.
enum class BSDFLobe : std::uint8_t {
    None,
    DiffuseReflection,
    DiffuseTransmission,
    GlossyReflection,
    GlossyTransmission,
    DeltaReflection,
    DeltaTransmission,
};

/// \brief Joint result of BSDF evaluation and PDF calculation.
struct BSDFEvaluation {
    /// BSDF value multiplied by the shading cosine.
    Spectrum value{};

    /// Solid-angle PDF for the queried direction.
    float pdf{};
};

/// \brief Result of sampling one BSDF lobe.
struct BSDFSample {
    /// Throughput multiplier for the sampled direction.
    Spectrum weight{};

    /// Shading-local direction toward the next path vertex.
    Vec3f wi{};

    /// Solid-angle PDF for continuous samples or probability mass for delta samples.
    float pdf{};

    /// Index of refraction on the next side divided by that on the current side.
    float eta{1.0F};

    /// Lobe selected by the sample.
    BSDFLobe lobe{BSDFLobe::None};

public:
    [[nodiscard]] KIRA_HOST_DEVICE bool isDelta() const noexcept {
        return lobe == BSDFLobe::DeltaReflection || lobe == BSDFLobe::DeltaTransmission;
    }
};

/// \brief Results produced by one BSDF execution.
struct BSDFResult {
    /// Evaluation for \c wi when requested.
    BSDFEvaluation evaluation{};

    /// Sample produced by every execution.
    BSDFSample sample{};
};

/// \brief Implements \c execute() with \c evalAndPdf() and \c sample().
///
/// Derived provides \c evalAndPdf and \c sample. It may define \c execute
/// directly when evaluation and sampling share work.
template <typename Derived> class BSDFMixin { // NOLINT
private:
    [[nodiscard]] KIRA_HOST_DEVICE Derived const &derived_() const noexcept {
        return *static_cast<Derived const *>(this);
    }

public:
    /// \brief Samples the BSDF and optionally evaluates \p wi.
    ///
    /// Directions point away from the surface and use shading-local space.
    /// Set \p eval to evaluate \p wi together with sampling.
    ///
    /// \pre \p wo is normalized; \p wi is normalized when \p eval is true.
    /// \pre Samples are in \f$[0,1)\f$.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFResult execute(
        ShadingContext const &ctx, Vec3f const &wo, Vec3f const &wi, bool eval, float u1,
        Vec2f const &u2
    ) const noexcept {
        auto result = BSDFResult{};
        if (eval)
            result.evaluation = derived_().evalAndPdf(ctx, wo, wi);
        result.sample = derived_().sample(ctx, wo, u1, u2);
        return result;
    }
};

/// \brief Host-side base for surface scattering models.
///
/// A parameter outside the range that a BSDF supports is bounded to the nearest supported value.
class BSDF : public RenderObject {
    friend class TXContext;

protected:
    /// \brief Creates a BSDF of \p type in \p tx.
    BSDF(TXContext &tx, BSDFType type);

public:
    struct Impl;
    struct Dispatcher;

    /// Kind whose index map holds BSDFs.
    static constexpr IndexedKind indexedKind = IndexedKind::BSDF;

    /// \brief Returns the concrete implementation type.
    [[nodiscard]] BSDFType getType() const noexcept { return type_; }

    [[nodiscard]] std::optional<IndexedKind> getIndexedKind() const noexcept override {
        return indexedKind;
    }

    /// \brief Builds this BSDF's scattering implementation.
    [[nodiscard]] Impl getImpl() const;

private:
    [[nodiscard]] static Ref<BSDF> create(TXContext &tx, kira::Properties const &props);

    BSDFType type_;
};

/// \brief Lambertian reflection with textured reflectance.
///
/// \par Properties
/// - \c R: optional RGB reflectance in \f$[0,1]\f$ per channel; defaults to 0.5.
class DiffuseBSDF final : public BSDF {
    friend class TXContext;

public:
    /// \brief Lambertian scattering implementation.
    struct Impl;

    [[nodiscard]] Ref<Texture const> getR() const noexcept { return R_; }

    /// \brief Builds the scattering implementation.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    DiffuseBSDF(TXContext &tx, kira::Properties const &props);

    Ref<Texture const> R_;
};

/// \brief Disney Principled BSDF with textured parameters.
///
/// \par Properties
/// - \c base_color: optional RGB base color in \f$[0,1]\f$ per channel; defaults to 0.5.
/// - \c roughness: optional surface roughness in \f$[0,1]\f$; defaults to 0.5.
/// - \c metallic: optional metallic weight in \f$[0,1]\f$; defaults to zero.
/// - \c spec_trans: optional transmission weight in \f$[0,1]\f$; defaults to zero.
/// - \c spec_tint: optional specular tint in \f$[0,1]\f$; defaults to zero.
/// - \c sheen and \c sheen_tint: optional sheen controls in \f$[0,1]\f$.
/// - \c flatness: optional fake-subsurface weight in \f$[0,1]\f$.
/// - \c clearcoat and \c clearcoat_roughness: optional clearcoat controls in \f$[0,1]\f$.
/// - \c eta or \c specular: dielectric reflectance control. They are mutually exclusive, and \c eta
///   is positive.
///
/// A texture setter requires a texture that is not empty and belongs to the same Context.
class PrincipledBSDF final : public BSDF {
    friend class TXContext;

public:
    struct Impl;

    /// \brief Parameters of a Principled BSDF that a setter can change.
    ///
    /// One bit per setter. A setter records its bit only when the value changes.
    enum class DirtyBits : std::uint32_t {
        None = 0,
        BaseColor = 1U << 0U,
        Roughness = 1U << 1U,
        Metallic = 1U << 2U,
        SpecTrans = 1U << 3U,
        SpecTint = 1U << 4U,
        Sheen = 1U << 5U,
        SheenTint = 1U << 6U,
        Flatness = 1U << 7U,
        Clearcoat = 1U << 8U,
        ClearcoatRoughness = 1U << 9U,
        Eta = 1U << 10U,
    };

    [[nodiscard]] Ref<Texture const> const &getBaseColor() const noexcept { return baseColor_; }
    void setBaseColor(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getRoughness() const noexcept { return roughness_; }
    void setRoughness(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getMetallic() const noexcept { return metallic_; }
    void setMetallic(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getSpecTrans() const noexcept { return specTrans_; }
    void setSpecTrans(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getSpecTint() const noexcept { return specTint_; }
    void setSpecTint(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getSheen() const noexcept { return sheen_; }
    void setSheen(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getSheenTint() const noexcept { return sheenTint_; }
    void setSheenTint(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getFlatness() const noexcept { return flatness_; }
    void setFlatness(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getClearcoat() const noexcept { return clearcoat_; }
    void setClearcoat(Ref<Texture const> texture);

    [[nodiscard]] Ref<Texture const> const &getClearcoatRoughness() const noexcept {
        return clearcoatRoughness_;
    }
    void setClearcoatRoughness(Ref<Texture const> texture);

    [[nodiscard]] float getEta() const noexcept { return eta_; }
    void setEta(float eta);

    /// \brief Builds the scattering implementation.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    PrincipledBSDF(TXContext &tx, kira::Properties const &props);

    void replaceTexture(Ref<Texture const> &property, Ref<Texture const> texture, DirtyBits bits);

    Ref<Texture const> baseColor_;
    Ref<Texture const> roughness_;
    Ref<Texture const> metallic_;
    Ref<Texture const> specTrans_;
    Ref<Texture const> specTint_;
    Ref<Texture const> sheen_;
    Ref<Texture const> sheenTint_;
    Ref<Texture const> flatness_;
    Ref<Texture const> clearcoat_;
    Ref<Texture const> clearcoatRoughness_;
    float eta_;
};

/// \brief A BSDF that a generated shader computes.
class ShaderBSDF final : public BSDF {
    friend class TXContext;

public:
    struct Impl;

    [[nodiscard]] Ref<ShaderAsset const> const &getAsset() const noexcept { return asset_; }

    [[nodiscard]] std::vector<std::uint32_t> const &getWords() const noexcept { return words_; }

    /// \brief Builds the scattering implementation.
    [[nodiscard]] Impl getImpl() const;

private:
    /// \brief Creates a BSDF of the shader that \p spec describes.
    ///
    /// \throws kira::Anyhow if \p words does not hold \c ShaderAssetSpec::wordCount words.
    ShaderBSDF(TXContext &tx, ShaderAssetSpec const &spec, std::vector<std::uint32_t> words);

    Ref<ShaderAsset const> asset_;
    std::vector<std::uint32_t> words_;
};

/// \brief Lambertian reflection implementation.
struct DiffuseBSDF::Impl {
    Texture::Impl R;

public:
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE BSDFResult execute(
        ShadingContext const &ctx, Vec3f const &wo, Vec3f const &wi, bool eval, float u1,
        Vec2f const &u2
    ) const noexcept;
};

/// \brief Disney Principled scattering implementation.
struct PrincipledBSDF::Impl {
    Texture::Impl baseColor;
    Texture::Impl roughness;
    Texture::Impl metallic;
    Texture::Impl specTrans;
    Texture::Impl specTint;
    Texture::Impl sheen;
    Texture::Impl sheenTint;
    Texture::Impl flatness;
    Texture::Impl clearcoat;
    Texture::Impl clearcoatRoughness;
    float eta;

public:
    /// \brief Evaluates and samples the Principled model for one hit.
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE BSDFResult execute(
        ShadingContext const &ctx, Vec3f const &wo, Vec3f const &wi, bool eval, float u1,
        Vec2f const &u2
    ) const noexcept;
};

/// \brief Shader scattering implementation.
struct ShaderBSDF::Impl {
    /// The BSDF's own index in the context.
    std::uint32_t index;
};

/// \brief Stores one BSDF implementation.
///
/// A value-initialized Impl is the empty BSDF: a valid diffuse BSDF with zero
/// reflectance, which scatters nothing.
struct BSDF::Impl {
    BSDFType type;

    union Storage {
        DiffuseBSDF::Impl diffuse;
        PrincipledBSDF::Impl principled;
        ShaderBSDF::Impl shader;
    } storage;
};

/// \brief Executes one BSDF.
struct BSDF::Dispatcher {
    /// Complete set of implementations that may be passed to \c execute.
    BSDFTypeMask types;

public:
    /// \brief Samples \p bsdf and optionally evaluates \p wi.
    ///
    /// Directions point away from the surface and use shading-local space.
    /// Set \p eval to evaluate \p wi together with sampling.
    ///
    /// \pre \p wo is normalized; \p wi is normalized when \p eval is true.
    /// \pre Samples are in \f$[0,1)\f$.
    /// \pre \c types contains \p bsdf's implementation type.
    template <typename Evaluator>
    [[nodiscard]] KIRA_HOST_DEVICE BSDFResult execute(
        BSDF::Impl const &bsdf, ShadingContext const &ctx, Vec3f const &wo, Vec3f const &wi,
        bool eval, float u1, Vec2f const &u2
    ) const noexcept;
};

template <> inline constexpr bool isEnumFlags<PrincipledBSDF::DirtyBits> = true;

static_assert(std::is_standard_layout_v<DiffuseBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<DiffuseBSDF::Impl>);
static_assert(std::is_standard_layout_v<PrincipledBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<PrincipledBSDF::Impl>);
static_assert(std::is_standard_layout_v<ShaderBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<ShaderBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<BSDF::Impl>);
static_assert(std::is_standard_layout_v<BSDF::Dispatcher>);
static_assert(std::is_trivially_copyable_v<BSDF::Dispatcher>);
} // namespace flux

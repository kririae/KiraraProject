#pragma once

#include <concepts>
#include <cstdint>
#include <cuda/std/variant>
#include <type_traits>
#include <utility>

#include "flux/Core/Math.h"
#include "flux/Scene/RenderObject.h"
#include "flux/Shading/Interaction.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Identifies a concrete BSDF implementation.
enum class BSDFType : std::uint8_t {
    Diffuse,
    Count,
};

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

/// \brief Hit-local input shared by BSDF evaluation and sampling.
struct BSDFQuery {
    /// Direction toward the previous path vertex.
    Vec3f wo;
};

/// \brief Hit-local data shared by concrete BSDF states.
struct BSDFState {
    KIRA_HOST_DEVICE explicit BSDFState(Vec3f const &shadingNormal) noexcept
        : shadingNormal(shadingNormal) {}

    /// Material-local shading normal initialized from the surface hit.
    Vec3f shadingNormal;
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

    /// Sampled direction toward the next path vertex.
    Vec3f wi{};

    /// Solid-angle PDF of \c wi.
    float pdf{};

    /// Relative index of refraction across the sampled interface.
    float eta{1.0F};

    /// Lobe selected by the sample.
    BSDFLobe lobe{BSDFLobe::None};

public:
    [[nodiscard]] KIRA_HOST_DEVICE bool isDelta() const noexcept {
        return lobe == BSDFLobe::DeltaReflection || lobe == BSDFLobe::DeltaTransmission;
    }
};

/// \brief Supplies the common concrete BSDF interface.
template <typename Derived> class BSDFMixin {
private:
    [[nodiscard]] KIRA_HOST_DEVICE Derived const &derived_() const noexcept {
        return *static_cast<Derived const *>(this);
    }

public:
    /// \brief Initializes one hit and invokes \p func while its BSDF state is valid.
    template <typename F>
    KIRA_HOST_DEVICE decltype(auto)
    init(SurfaceInteraction const &isect, Vec3f const &wo, F &&func) const {
        using State = typename Derived::BSDFState;
        static_assert(std::derived_from<State, flux::BSDFState>);

        State state{isect.shadingNormal};
        if constexpr (requires(Derived const &bsdf) { bsdf.init_(state, isect, wo); })
            derived_().init_(state, isect, wo);

        return std::forward<F>(func)(derived_(), static_cast<State const &>(state));
    }

    /// \brief Jointly evaluates the BSDF and its sampling PDF.
    template <typename BSDFState>
    [[nodiscard]] KIRA_HOST_DEVICE BSDFEvaluation
    evaluateAndPdf(BSDFState const &state, BSDFQuery const &query, Vec3f const &wi) const noexcept {
        return derived_().evaluateAndPdf_(state, query, wi);
    }

    /// \brief Samples the concrete BSDF.
    template <typename BSDFState>
    [[nodiscard]] KIRA_HOST_DEVICE BSDFSample sample(
        BSDFState const &state, BSDFQuery const &query, float lobeSample,
        Vec2f const &directionSample
    ) const noexcept {
        return derived_().sample_(state, query, lobeSample, directionSample);
    }
};

/// \brief Host-side base for surface scattering models.
class BSDF : public RenderObject {
    friend class TXContext;

protected:
    /// \brief Creates a BSDF of \p type in \p tx.
    BSDF(TXContext &tx, BSDFType type);

public:
    /// \brief Heterogeneous scattering implementation.
    struct Impl;

    /// \brief Returns the concrete implementation type.
    [[nodiscard]] BSDFType getType() const noexcept { return type_; }

    /// \brief Builds this BSDF's scattering implementation.
    [[nodiscard]] Impl getImpl() const;

private:
    [[nodiscard]] static Ref<BSDF> create(TXContext &tx, kira::Properties const &props);

    BSDFType type_;
};

/// \brief Lambertian reflection with constant reflectance.
///
/// \par Properties
/// - \c R: optional RGB reflectance in \f$[0,1]^3\f$; defaults to 0.5.
class DiffuseBSDF final : public BSDF {
    friend class TXContext;

public:
    /// \brief Lambertian scattering implementation.
    struct Impl;

    /// \brief Returns the constant surface reflectance.
    [[nodiscard]] Spectrum const &getReflectance() const noexcept { return reflectance_; }

    /// \brief Builds the constant scattering implementation.
    [[nodiscard]] Impl getImpl() const noexcept;

private:
    DiffuseBSDF(TXContext &tx, kira::Properties const &props);

    Spectrum reflectance_{0.5F, 0.5F, 0.5F};
};

/// \brief Lambertian reflection implementation.
struct DiffuseBSDF::Impl : BSDFMixin<Impl> {
    /// \brief Hit-local diffuse shading data.
    struct BSDFState : flux::BSDFState {
        using flux::BSDFState::BSDFState;
    };

    /// Constant RGB reflectance.
    Spectrum reflectance;

public:
    /// \brief Samples a cosine-weighted reflection direction.
    ///
    /// \param lobeSample Reserved for selecting a lobe in multi-lobe BSDFs.
    /// \param directionSample Uniform sample in the unit square.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFSample sample_(
        BSDFState const &state, BSDFQuery const &query, float lobeSample,
        Vec2f const &directionSample
    ) const noexcept;

    /// \brief Evaluates the BSDF and sampling PDF for \p wi.
    [[nodiscard]] KIRA_HOST_DEVICE BSDFEvaluation
    evaluateAndPdf_(BSDFState const &state, BSDFQuery const &query, Vec3f const &wi) const noexcept;
};

/// \brief Heterogeneous scattering implementation.
struct BSDF::Impl : cuda::std::variant<DiffuseBSDF::Impl> {
    using Base = cuda::std::variant<DiffuseBSDF::Impl>;
    using Base::Base;

private:
    template <typename Function>
    KIRA_HOST_DEVICE decltype(auto) dispatch(Function &&function) const {
        return cuda::std::visit(std::forward<Function>(function), static_cast<Base const &>(*this));
    }

public:
    /// \brief Dispatches one hit and invokes \p func while its BSDF state is valid.
    template <typename F>
    KIRA_HOST_DEVICE decltype(auto)
    init(SurfaceInteraction const &isect, Vec3f const &wo, F &&func) const {
        return dispatch([&](auto const &bsdf) -> decltype(auto) {
            return bsdf.init(isect, wo, std::forward<F>(func));
        });
    }
};

static_assert(std::is_standard_layout_v<DiffuseBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<DiffuseBSDF::Impl>);
static_assert(std::is_trivially_copyable_v<BSDF::Impl>);
} // namespace flux
